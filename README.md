# 🧠 Multi-Process Scheduler with Paging (C++)

이 프로젝트는 운영체제 핵심 개념인 **프로세스 스케줄링(Round-Robin)**과 **가상 메모리(Demand Paging)**를 직접 구현한 시뮬레이터입니다.  
`g++`로 컴파일된 단일 C++ 파일로, 다중 프로세스를 시뮬레이션하고 각 프로세스에 독립적인 페이지 테이블을 할당하여 **VA→PA 변환 및 Page Fault 처리**를 구현합니다.

---

## 📁 프로젝트 구성

```
.
├── RRscheduling.cpp      # 메인 시뮬레이터 코드
└── schedule_dump.txt     # Tick마다의 스케줄링 상태 및 메모리 접근 로그
└── README.md             # 프로젝트 설명 문서
```

---

## ⚙️ 실행 방법

```bash
g++ -std=c++11 -Wall -o RRscheduling RRscheduling.cpp
./RRscheduling
```

---

## 🌀 구현된 기능 요약

| 기능                       | 설명 |
|----------------------------|------|
| 🔁 Round-Robin 스케줄링     | 10개 프로세스, 100ms 타임 퀀텀, Run/Wait Queue 운영 |
| 💾 Demand Paging             | 프로세스당 페이지 테이블 16개 엔트리 (64KB VA), 32개 물리 프레임 |
| 🔀 VA → PA 변환 처리       | VA를 4KB 단위 페이지로 분할, 변환 시 Page Fault 발생 가능 |
| 🪵 시뮬레이션 로그 출력      | 100 Tick마다 현재 실행 중인 프로세스, 큐 상태, 메모리 접근 로그 기록 |
| 📉 최종 프로세스 상태 출력  | 각 프로세스의 총 대기 시간 정리 및 출력 |

---

## 📄 로그 파일 예시 (`schedule_dump.txt`)

```
=== Tick 100 ===
At Tick 100: Process P2 is running, Remaining CPU-Burst = 612
Run Queue: P3(CPU=684, Wait=20) P5(CPU=601, Wait=20) ...
Wait Queue: P4(IO=370) ...
Memory Access Log:
PID 2 accesses VA 12288 → PA 4096 (Page Fault)
PID 2 accesses VA 8192 → PA 8192
...

Final Process States:
Process 1: Total Wait Time = 942ms
Process 2: Total Wait Time = 1062ms
...
```

---

## 📚 학습한 개념

- **프로세스 큐 상태 관리** 및 **CPU/IO Burst 시뮬레이션**
- **시그널 기반 인터럽트 처리** (`setitimer`, `SIGALRM`)
- **페이지 테이블 및 메모리 주소 변환** 구현
- **멀티프로세스 간 통신** (`msgsnd`, `msgrcv`)
- **자원 정리 및 프로세스 종료 처리**

---

> 개발자: [@daayuun](https://github.com/daayuun)  
