#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define NUM_PROCESSES 10
#define TIME_QUANTUM 3
#define MAX_CPU_BURST 10
#define MIN_CPU_BURST 1
#define MAX_IO_WAIT 5
#define MIN_IO_WAIT 1

// 프로세스 상태
typedef enum {
    READY,
    RUNNING,
    SLEEPING,
    DONE
} ProcessState;

// PCB 구조체
typedef struct {
    pid_t pid;
    int quantum;
    int cpu_burst;
    int io_wait;
    ProcessState state;
} PCB;

// 전역 변수
PCB pcb[NUM_PROCESSES];
int ready_queue[NUM_PROCESSES];
int ready_front = 0, ready_rear = 0;
int sleep_queue[NUM_PROCESSES];
int sleep_count = 0;
int current_process = -1;
int done_count = 0;
int timer_tick = 0;

// 자식 프로세스의 CPU 버스트 (각 자식이 개별적으로 관리)
int my_cpu_burst = 0;

// 함수 선언
void parent_process();
void child_process(int id);
void timer_handler(int signum);
void io_request_handler(int signum);
void child_signal_handler(int signum);
void schedule();
void push_ready(int idx);
int pop_ready();
void push_sleep(int idx);
void update_sleep_queue();
int all_quantum_zero();
void reset_all_quantum();

// Ready 큐에 추가
void push_ready(int idx) {
    ready_queue[ready_rear] = idx;
    ready_rear = (ready_rear + 1) % NUM_PROCESSES;
    pcb[idx].state = READY;
}

// Ready 큐에서 꺼내기
int pop_ready() {
    if (ready_front == ready_rear) {
        return -1; // 큐가 비어있음
    }
    int idx = ready_queue[ready_front];
    ready_front = (ready_front + 1) % NUM_PROCESSES;
    return idx;
}

// Sleep 큐에 추가
void push_sleep(int idx) {
    sleep_queue[sleep_count++] = idx;
    pcb[idx].state = SLEEPING;
}

// Sleep 큐 업데이트 (대기 시간 감소)
void update_sleep_queue() {
    for (int i = 0; i < sleep_count; i++) {
        int idx = sleep_queue[i];
        pcb[idx].io_wait--;
        
        if (pcb[idx].io_wait <= 0) {
            printf("[PARENT] Process %d (PID: %d) I/O completed, moving to Ready queue\n", 
                   idx, pcb[idx].pid);
            fflush(stdout);
            
            // Ready 큐로 이동
            push_ready(idx);
            
            // Sleep 큐에서 제거
            for (int j = i; j < sleep_count - 1; j++) {
                sleep_queue[j] = sleep_queue[j + 1];
            }
            sleep_count--;
            i--;
        }
    }
}

// 모든 프로세스의 타임퀀텀이 0인지 확인
int all_quantum_zero() {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb[i].state != DONE && pcb[i].quantum > 0) {
            return 0;
        }
    }
    return 1;
}

// 모든 프로세스의 타임퀀텀 초기화
void reset_all_quantum() {
    printf("[PARENT] All quantum exhausted, resetting all processes' quantum\n");
    fflush(stdout);
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb[i].state != DONE) {
            pcb[i].quantum = TIME_QUANTUM;
        }
    }
}

// 타이머 시그널 핸들러
void timer_handler(int signum) {
    timer_tick++;
    printf("\n=== Timer Tick %d ===\n", timer_tick);
    fflush(stdout);
    
    // Sleep 큐 업데이트
    update_sleep_queue();
    
    // 현재 실행 중인 프로세스 처리
    if (current_process != -1 && pcb[current_process].state == RUNNING) {
        pcb[current_process].quantum--;
        printf("[PARENT] Process %d quantum: %d -> %d\n", 
               current_process, pcb[current_process].quantum + 1, pcb[current_process].quantum);
        fflush(stdout);
        
        if (pcb[current_process].quantum <= 0) {
            printf("[PARENT] Process %d quantum exhausted, preempting\n", current_process);
            fflush(stdout);
            
            // 프로세스를 정지시키고 Ready 큐에 추가
            kill(pcb[current_process].pid, SIGSTOP);
            push_ready(current_process);
            current_process = -1;
        } else {
            // 계속 실행 - 자식에게 시그널 전송
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    // 타임퀀텀 체크 및 초기화
    if (all_quantum_zero() && done_count < NUM_PROCESSES) {
        reset_all_quantum();
    }
    
    // 스케줄링
    schedule();
}

// I/O 요청 시그널 핸들러
void io_request_handler(int signum) {
    if (current_process != -1) {
        printf("[PARENT] Process %d (PID: %d) requested I/O\n", 
               current_process, pcb[current_process].pid);
        fflush(stdout);
        
        // I/O 대기 시간 랜덤 할당
        pcb[current_process].io_wait = (rand() % (MAX_IO_WAIT - MIN_IO_WAIT + 1)) + MIN_IO_WAIT;
        printf("[PARENT] Assigning I/O wait time: %d ticks\n", pcb[current_process].io_wait);
        fflush(stdout);
        
        // Sleep 큐에 추가
        kill(pcb[current_process].pid, SIGSTOP);
        push_sleep(current_process);
        current_process = -1;
    }
}

// 스케줄링 함수
void schedule() {
    // 현재 프로세스가 없으면 다음 프로세스 선택
    if (current_process == -1 || pcb[current_process].state != RUNNING) {
        int next = pop_ready();
        
        if (next != -1) {
            current_process = next;
            pcb[current_process].state = RUNNING;
            printf("[PARENT] Scheduling Process %d (PID: %d, Quantum: %d)\n", 
                   current_process, pcb[current_process].pid, pcb[current_process].quantum);
            fflush(stdout);
            
            // 프로세스 실행
            kill(pcb[current_process].pid, SIGCONT);
            kill(pcb[current_process].pid, SIGUSR1);
        } else {
            printf("[PARENT] No process in Ready queue\n");
            fflush(stdout);
        }
    }
    
    // 모든 프로세스가 완료되었는지 확인
    if (done_count >= NUM_PROCESSES) {
        printf("\n[PARENT] All processes completed!\n");
        fflush(stdout);
        
        // 모든 자식 프로세스 대기
        for (int i = 0; i < NUM_PROCESSES; i++) {
            waitpid(pcb[i].pid, NULL, 0);
        }
        
        printf("[PARENT] Simulation completed successfully!\n");
        fflush(stdout);
        exit(0);
    }
}

// 자식 프로세스 시그널 핸들러
void child_signal_handler(int signum) {
    if (signum == SIGUSR1) {
        // CPU 버스트 감소
        my_cpu_burst--;
        printf("  [CHILD %d] Executing... CPU burst remaining: %d\n", getpid(), my_cpu_burst);
        fflush(stdout);
        
        if (my_cpu_burst <= 0) {
            // CPU 버스트가 0이 되면 종료 또는 I/O
            int choice = rand() % 2;
            
            if (choice == 0) {
                // 프로세스 종료
                printf("  [CHILD %d] CPU burst completed, terminating\n", getpid());
                fflush(stdout);
                kill(getppid(), SIGCHLD);
                exit(0);
            } else {
                // I/O 요청
                printf("  [CHILD %d] CPU burst completed, requesting I/O\n", getpid());
                fflush(stdout);
                kill(getppid(), SIGUSR2);
                
                // I/O 완료 후 새로운 CPU 버스트 할당
                my_cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
            }
        }
    }
}

// 부모 프로세스
void parent_process() {
    printf("[PARENT] Initializing scheduler...\n");
    fflush(stdout);
    
    // 시그널 핸들러 설정
    signal(SIGALRM, timer_handler);
    signal(SIGUSR2, io_request_handler);
    signal(SIGCHLD, SIG_IGN); // 자식 종료 시그널은 무시 (좀비 프로세스 방지)
    
    // PCB 초기화 및 자식 프로세스 생성
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 자식 프로세스
            child_process(i);
            exit(0);
        } else {
            // 부모 프로세스 - PCB 초기화
            pcb[i].pid = pid;
            pcb[i].quantum = TIME_QUANTUM;
            pcb[i].cpu_burst = 0; // 자식이 초기화
            pcb[i].io_wait = 0;
            pcb[i].state = READY;
            
            printf("[PARENT] Created Process %d with PID %d\n", i, pid);
            fflush(stdout);
            
            // Ready 큐에 추가
            push_ready(i);
        }
    }
    
    printf("\n[PARENT] All processes created, starting scheduler...\n\n");
    fflush(stdout);
    
    // 초기 스케줄링
    sleep(1); // 자식들이 준비될 때까지 대기
    schedule();
    
    // 타이머 시작 (1초마다)
    alarm(1);
    
    // 무한 루프
    while (1) {
        pause(); // 시그널 대기
    }
}

// 자식 프로세스
void child_process(int id) {
    srand(time(NULL) + getpid());
    
    // CPU 버스트 초기화
    my_cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
    
    printf("  [CHILD %d] Started with CPU burst: %d\n", getpid(), my_cpu_burst);
    fflush(stdout);
    
    // 시그널 핸들러 설정
    signal(SIGUSR1, child_signal_handler);
    
    // SIGSTOP으로 대기
    kill(getpid(), SIGSTOP);
    
    // 무한 루프 (시그널 대기)
    while (1) {
        pause();
    }
}

int main() {
    srand(time(NULL));
    
    printf("===== OS Scheduling Simulation =====\n");
    printf("Number of Processes: %d\n", NUM_PROCESSES);
    printf("Time Quantum: %d\n", TIME_QUANTUM);
    printf("====================================\n\n");
    fflush(stdout);
    
    parent_process();
    
    return 0;
}