#include <iostream>
#include <queue>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <sys/msg.h>
#include <sys/time.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <sys/wait.h>
#include <sstream>

#define PROCESS_COUNT 10
#define TIME_QUANTUM 100
#define TICK_INTERVAL 10 // 타이머 인터벌
#define MAX_TICKS 10000 // 최대 틱 수 

using namespace std;

// 메시지 큐 구조체 정의
struct MsgBuffer {
    long mtype;             // 메시지 타입
    int pid;                // 프로세스 ID
    int remainingCPUBurst;  // 남은 CPU 버스트 시간
    int remainingIOBurst;   // 남은 IO 버스트 시간
    int quantumLeft;        // 남은 시간 할당량 (quantum)
    bool terminate;         // 종료 신호
    bool ioRequest;         // IO 요청 여부
    int vas[10];            // VA 요청
};

// 프로세스 구조체 정의
struct Process {
    int pid;                // 프로세스 ID
    int cpuBurst;           // CPU 버스트 시간
    int ioBurst;            // IO 버스트 시간
    int totalWaitTime;      // 총 대기 시간
    int quantumLeft;        // 남은 시간 할당량 (quantum)
    bool isInIO;            // IO 대기 중 여부
    int executionCount;     // 실행 횟수
};

// 페이지 테이블 구조 정의
struct PageTableEntry{
    bool valid;             // 유효한 매핑 여부
    int frameNumber;        // 실제 물리 프레임 번호
};

using PageTable = vector<PageTableEntry>; // 프로세스 1개당 16개 엔트리 (64KB 가정)
unordered_map<int, PageTable> pageTables; // pid → 해당 프로세스의 page table

vector<string> memoryAccessLog; // VA→PA 로그를 매 tick마다 임시 저장
vector<int> freeFrames;         // 아직 할당되지 않은 프레임 번호 목록
queue<Process> runQueue;        // 실행 대기 큐
queue<Process> waitQueue;       // IO 대기 큐
ofstream logFile;               // 로그 파일 출력 스트림
int msgQueueID;                 // 메시지 큐 ID
int currentTick = 0;            // 현재 틱
volatile sig_atomic_t alarmFlag = false;    // 알람 flag
bool terminateAll = false;      // 종료 flag

void initializeProcesses(vector<Process>& processes); // 프로세스 초기화 함수
void parentProcess(vector<Process>& processes); // 부모 프로세스
void childProcess(int pid); // 자식 프로세스
void timerHandler(int signum); // 타이머 interrupt handler (틱마다 호출)
void updateQueues(vector<Process>& processes); // 프로세스 큐 업데이트
void logSchedulingState(); // 스케줄링 상태 로그 작성
void cleanupResources(); // 자원 정리
void terminateChildren(); // 자식 프로세스 종료
void generateNewBursts(Process& proc); // 새로운 CPU, IO burst time 생성
int translateVAtoPA(int pid, int va, bool& pageFaultOccurred); // VA -> PA 변환 함수

int main() {
    // 프로세스 벡터 생성 및 로그 파일 열기
    vector<Process> processes(PROCESS_COUNT);
    logFile.open("schedule_dump.txt");
    
    if (!logFile.is_open()) {
        cerr << "Error opening log file" << endl;
        return 1;
    }

    // 메시지 큐 생성
    msgQueueID = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msgQueueID == -1) {
        perror("Message queue creation failed");
        return 1;
    }

    // 프로세스 초기화 (CPU, IO burst time 설정)
    initializeProcesses(processes);
    
    // 페이징 관련 초기화
    for (int i = 1; i <= PROCESS_COUNT; i++) {
        PageTable pt(16); // 16개 엔트리 (64KB VA 공간, 4KB 페이지)
        for (auto& entry : pt) entry.valid = false;
        pageTables[i] = pt;
    }
    for (int i = 0; i < 32; i++) {
        freeFrames.push_back(i); // 32개 물리 프레임 초기화
    }

    // 자식 프로세스 생성 (fork)
    for (int i = 0; i < PROCESS_COUNT; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("Fork failed");
            cleanupResources();
            return 1;
        }
        if (pid == 0) {
            childProcess(processes[i].pid);
            exit(0);
        }
    }

    // 부모 프로세스는 스케줄링 담당
    parentProcess(processes);

    // 자원 정리 및 종료
    cleanupResources();
    return 0;
}

void initializeProcesses(vector<Process>& processes) {
    srand(time(nullptr)); // 난수 초기화
    for (int i = 0; i < PROCESS_COUNT; i++) {
        processes[i].pid = i + 1;
        processes[i].cpuBurst = rand() % 500 + 500; // CPU burst time (500~999ms)
        processes[i].ioBurst = rand() % 300 + 200; // IO burst time(200~499ms)
        processes[i].totalWaitTime = 0;
        processes[i].quantumLeft = TIME_QUANTUM; // 각 프로세스의 초기 quantum
        processes[i].isInIO = false;
        processes[i].executionCount = 0;
    }
}

void parentProcess(vector<Process>& processes) {
    struct sigaction sa;
    struct itimerval timer;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &timerHandler; // 타이머 interrupt handler 설정
    sigaction(SIGALRM, &sa, nullptr);

    // 타이머 인터벌 설정
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = TICK_INTERVAL * 1000;
    timer.it_interval = timer.it_value;

    setitimer(ITIMER_REAL, &timer, nullptr);

    // 프로세스를 실행 대기 큐에 추가
    for (const auto& proc : processes) {
        runQueue.push(proc);
    }

    while (currentTick < MAX_TICKS && !terminateAll) {
        if (alarmFlag) {
            alarmFlag = false;
            currentTick += TICK_INTERVAL;

            // 프로세스 큐 업데이트
            updateQueues(processes);

            // 실행 대기 큐에서 프로세스 꺼내서 실행
            if (!runQueue.empty()) {
                Process& currentProc = runQueue.front();
                
                MsgBuffer msg;
                msg.mtype = currentProc.pid;
                msg.pid = currentProc.pid;  // 메시지 타입 (프로세스 ID)
                msg.remainingCPUBurst = currentProc.cpuBurst;
                msg.remainingIOBurst = currentProc.ioBurst;
                msg.quantumLeft = currentProc.quantumLeft;
                msg.terminate = false;
                msg.ioRequest = false;

                // 메시지를 자식 프로세스로 전송
                if (msgsnd(msgQueueID, &msg, sizeof(MsgBuffer) - sizeof(long), 0) == -1) {
                    perror("Message send failed");
                } else {
                    runQueue.pop();  // 메시지 전송 성공 시에만 pop

                    // 자식 프로세스에서 메시지 수신
                    if (msgrcv(msgQueueID, &msg, sizeof(MsgBuffer) - sizeof(long), 1, 0) != -1) {
                        currentProc.cpuBurst = msg.remainingCPUBurst;
                        currentProc.ioBurst = msg.remainingIOBurst;
                        currentProc.quantumLeft = msg.quantumLeft;
                        
                        // VA 요청 처리 및 변환
                        for (int i = 0; i < 10; ++i) {
                            bool pageFault;
                            int va = msg.vas[i];
                            int pa = translateVAtoPA(msg.pid, va, pageFault);

                            stringstream ss;
                            ss << "PID " << msg.pid << " accesses VA " << va << " → PA " << pa << (pageFault ? " (Page Fault)" : "");
                            memoryAccessLog.push_back(ss.str());
                        }

                        if (msg.ioRequest) {
                            currentProc.isInIO = true;
                            waitQueue.push(currentProc); // IO 대기 큐에 추가
                        } else if (currentProc.quantumLeft <= 0) {
                            currentProc.quantumLeft = TIME_QUANTUM;
                            runQueue.push(currentProc); // 다시 실행 대기 큐에 추가
                        } else {
                            runQueue.push(currentProc);
                        }
                    }
                }
            }

            // 스케줄링 상태 로그 작성
            if (currentTick % 100 == 0) {
                logSchedulingState();
            }
        }
    }

    // 모든 자식 프로세스 종료
    terminateChildren();
    logFile << "\nFinal Process States:\n";
    for (const auto& proc : processes) {
        logFile << "Process " << proc.pid << ": Total Wait Time = " 
                << proc.totalWaitTime << "ms\n";
    }
}

void childProcess(int pid) {
    MsgBuffer msg;
    
    while (true) {
        if (msgrcv(msgQueueID, &msg, sizeof(MsgBuffer) - sizeof(long), pid, 0) == -1) {
            break; // 메시지를 받지 못하면 종료
        }

        if (msg.terminate) {
            break; // 종료 신호
        }

        for (int i = 0; i < 10; ++i) {
            msg.vas[i] = ((rand() % 4) + 1) * 4096 + (rand() % 4096); // 매번 새로운 페이지 접근
        }
        // CPU burst가 남아있으면 실행
        if (msg.remainingCPUBurst > 0) {
            int workDone = min(msg.quantumLeft, msg.remainingCPUBurst);
            msg.remainingCPUBurst -= workDone;
            msg.quantumLeft -= workDone;

            if (msg.remainingCPUBurst <= 0 && msg.remainingIOBurst > 0) {
                msg.ioRequest = true; // IO 요청 시 flag 설정
            }
        }

        msg.mtype = 1; // 부모에게 메시지 보내기
        if (msgsnd(msgQueueID, &msg, sizeof(MsgBuffer) - sizeof(long), 0) == -1) {
            break; // 메시지 전송 실패 시 종료
        }
    }
}

void timerHandler(int signum) {
    alarmFlag = true;
}

void updateQueues(vector<Process>& processes) {
    queue<Process> tempWaitQueue;

    while (!waitQueue.empty()) {
        Process proc = waitQueue.front();
        waitQueue.pop();

        proc.ioBurst -= TICK_INTERVAL; // IO burst time 차감
        if (proc.ioBurst <= 0) {
            proc.isInIO = false;
            generateNewBursts(proc); // 새로운 CPU, IO burst 생성
            proc.quantumLeft = TIME_QUANTUM;
            runQueue.push(proc); // 다시 실행 대기 큐에 추가
        } else {
            tempWaitQueue.push(proc);
        }
    }
    waitQueue = tempWaitQueue;

    // 실행 대기 큐 처리 (대기 시간 누적)
    queue<Process> tempRunQueue;
    while (!runQueue.empty()) {
        Process proc = runQueue.front();
        runQueue.pop();

        proc.totalWaitTime += TICK_INTERVAL;  // 실행되지 않은 프로세스 대기 시간 증가
        tempRunQueue.push(proc);
    }
    runQueue = tempRunQueue;

    // `processes` 벡터 동기화 (실행 대기 큐에 있는 프로세스만 대기 시간 증가)
    for (auto& proc : processes) {
        bool inRunQueue = false;

        // 실행 대기 큐 복사본에서 현재 프로세스가 있는지 확인
        queue<Process> tempQueue = runQueue;
        while (!tempQueue.empty()) {
            if (tempQueue.front().pid == proc.pid) {
                inRunQueue = true;
                break;
            }
            tempQueue.pop();
        }

        if (inRunQueue) {
            proc.totalWaitTime += TICK_INTERVAL;  // 실제 대기 중인 프로세스의 대기 시간만 증가
        }
    }
}



void logSchedulingState() {
    logFile << "\n=== Tick " << currentTick << " ===\n";

    if (!runQueue.empty()) {
    const Process& currentProc = runQueue.front();
    logFile << "At Tick " << currentTick << ": Process P" 
            << currentProc.pid << " is running, Remaining CPU-Burst = " 
            << currentProc.cpuBurst << "\n";
    }

    logFile << "Run Queue: ";
    queue<Process> tempRunQueue = runQueue;
    while (!tempRunQueue.empty()) {
        const Process& proc = tempRunQueue.front();
        logFile << "P" << proc.pid << "(CPU=" << proc.cpuBurst
                << ", Wait=" << proc.totalWaitTime << ") ";
        tempRunQueue.pop();
    }
    logFile << "\n";

    logFile << "Wait Queue: ";
    queue<Process> tempWaitQueue = waitQueue;
    while (!tempWaitQueue.empty()) {
        const Process& proc = tempWaitQueue.front();
        logFile << "P" << proc.pid << "(IO=" << proc.ioBurst << ") ";
        tempWaitQueue.pop();
    }
    logFile << "\n";

    if (!memoryAccessLog.empty()) {
        logFile << "Memory Access Log:\n";
        for (const auto& entry : memoryAccessLog) {
            logFile << entry << "\n";
        }
        memoryAccessLog.clear(); // 다음 tick을 위해 비움
    }
}

void cleanupResources() {
    msgctl(msgQueueID, IPC_RMID, nullptr); // 메시지 큐 제거
    logFile.close(); // 로그 파일 닫기
}

void terminateChildren() {
    MsgBuffer msg;
    msg.mtype = 1;
    msg.terminate = true; // 종료 신호 전송
    for (int i = 0; i < PROCESS_COUNT; i++) {
        msgsnd(msgQueueID, &msg, sizeof(MsgBuffer) - sizeof(long), 0);
    }
    while (wait(nullptr) > 0); // 모든 자식 프로세스 종료 대기
}

void generateNewBursts(Process& proc) {
    proc.cpuBurst = rand() % 500 + 500;
    proc.ioBurst = rand() % 300 + 200;
}

int translateVAtoPA(int pid, int va, bool& pageFaultOccurred) {
    int pageNum = va / 4096;
    int offset = va % 4096;
    PageTable& pt = pageTables[pid];

    if (!pt[pageNum].valid) {
        // Page Fault 발생
        pageFaultOccurred = true;
        if (freeFrames.empty()) {
            // 구현 확장: LRU나 스왑 아웃 필요
            return -1;
        }

        int newFrame = freeFrames.back();
        freeFrames.pop_back();
        pt[pageNum].valid = true;
        pt[pageNum].frameNumber = newFrame;
    } else {
        pageFaultOccurred = false;
    }

    int pa = pt[pageNum].frameNumber * 4096 + offset;
    return pa;
}
