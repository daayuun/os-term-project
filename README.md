🧠 OS Term Project: Multi-Process Simulator with Virtual Memory
(C++ | Round-Robin Scheduling + Paging)

이 프로젝트는 운영체제 수업의 팀 프로젝트로, 실제 운영체제의 동작을 소프트웨어적으로 구현한 시뮬레이터입니다. C++로 Round-Robin 스케줄링, Demand Paging 기반 가상 메모리, Inter-Process Communication(IPC)를 직접 구현하여 다중 프로세스 환경을 시뮬레이션했습니다.

📁 프로젝트 구조

bash
복사
편집
os-term-project/
├── RRscheduling.cpp   # 메인 시뮬레이터 코드 (모든 로직 포함)
├── RRschedule_dump.txt # 실행 시 출력되는 로그 파일
└── README.md          # 프로젝트 설명 문서
⚙️ 구현 기능 요약

✅ 다중 프로세스 생성: 부모 프로세스가 10개의 자식 프로세스를 fork

✅ Round-Robin Scheduling: 타이머 인터럽트를 통해 각 프로세스를 정해진 quantum만큼 실행

✅ IPC 통신: 자식 프로세스는 메시지 큐로 CPU Burst, I/O Burst 정보를 부모에게 전달

✅ Demand Paging: 각 프로세스는 자신만의 가상 주소 공간을 가지고 있으며, 페이지 테이블을 통해 VA → PA 변환 수행

✅ Page Fault Handling: 페이지가 없을 경우 즉시 페이지 프레임을 할당하고 매핑

✅ Run Queue / Wait Queue 관리: CPU와 I/O 대기 상태의 프로세스를 큐로 관리

✅ Tick 단위 로그 기록: 메모리 접근, 페이지 폴트, 실행 큐 상태를 주기적으로 RRschedule_dump.txt에 기록

🧪 학습한 운영체제 개념

프로세스 생성 및 스케줄링

타이머 인터럽트 기반 컨텍스트 스위칭

가상 메모리와 페이지 테이블 구조

페이지 폴트 처리 및 동적 메모리 매핑

사용자-커널 간 IPC 통신 방식

🚀 컴파일 및 실행 방법

bash
복사
편집
g++ -std=c++11 -Wall -o RRscheduling RRscheduling.cpp
./RRscheduling
📄 샘플 출력 예시 (RRschedule_dump.txt 중 일부)

yaml
복사
편집
=== Tick 100 ===
At Tick 100: Process P1 is running, Remaining CPU-Burst = 816
Run Queue: P1(CPU=816, Wait=100) P2(...) ...
Memory Access Log:
PID 1 accesses VA 4633 → PA 127513 (Page Fault)
PID 1 accesses VA 18613 → PA 125109 (Page Fault)
...
