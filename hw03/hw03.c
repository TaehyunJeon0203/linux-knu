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
#define QUEUE_SIZE (NUM_PROCESSES + 1)

// ANSI 색상 코드
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"

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
    
    // 성능 측정용
    int creation_time;      // 생성 시간
    int first_run_time;     // 첫 실행 시간 (-1이면 아직 실행 안됨)
    int completion_time;    // 종료 시간
    int total_wait_time;    // 총 ready 큐 대기 시간
    int ready_enter_time;   // ready 큐 진입 시간
    int wait_count;         // ready 큐 진입 횟수
} PCB;

// 전역 변수
PCB pcb[NUM_PROCESSES];
int ready_queue[QUEUE_SIZE];
int ready_front = 0, ready_rear = 0;
int sleep_queue[NUM_PROCESSES];
int sleep_count = 0;
int current_process = -1;
int done_count = 0;
int timer_tick = 0;

// 자식 프로세스의 CPU 버스트
int my_cpu_burst = 0;

// 함수 선언
void parent_process();
void child_process(int id);
void timer_handler(int signum);
void io_request_handler(int signum);
void child_signal_handler(int signum);
void child_done_handler(int signum);
void schedule();
void push_ready(int idx);
int pop_ready();
void push_sleep(int idx);
void update_sleep_queue();
int all_quantum_zero();
void reset_all_quantum();
void print_status();
void print_statistics();
const char* state_to_string(ProcessState state);

// 상태를 문자열로 변환
const char* state_to_string(ProcessState state) {
    switch(state) {
        case READY: return COLOR_GREEN "READY  " COLOR_RESET;
        case RUNNING: return COLOR_CYAN "RUNNING" COLOR_RESET;
        case SLEEPING: return COLOR_YELLOW "SLEEP  " COLOR_RESET;
        case DONE: return COLOR_RED "DONE   " COLOR_RESET;
        default: return "UNKNOWN";
    }
}

// 전체 상태 출력
void print_status() {
    int ready_count = (ready_rear - ready_front + QUEUE_SIZE) % QUEUE_SIZE;
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║  " COLOR_CYAN "P#" COLOR_RESET "  │  " COLOR_CYAN "State" COLOR_RESET "   │ " COLOR_CYAN "Quantum" COLOR_RESET " │ " COLOR_CYAN "I/O Wait" COLOR_RESET " │");
    
    if (current_process != -1) {
        printf("  " COLOR_MAGENTA "Running: P%d" COLOR_RESET, current_process);
    }
    printf("\n");
    printf("╠════════════════════════════════════════════════════════════════════╣\n");
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        printf("║  %s%2d%s │  %s  │    %s%d%s    │    %s%d%s     │",
               pcb[i].state == RUNNING ? COLOR_MAGENTA : "",
               i,
               pcb[i].state == RUNNING ? COLOR_RESET : "",
               state_to_string(pcb[i].state),
               pcb[i].quantum > 0 ? COLOR_GREEN : COLOR_RED,
               pcb[i].quantum,
               COLOR_RESET,
               pcb[i].io_wait > 0 ? COLOR_YELLOW : "",
               pcb[i].io_wait,
               COLOR_RESET);
        
        if (i == current_process) {
            printf(" " COLOR_MAGENTA "◄" COLOR_RESET);
        }
        printf("\n");
    }
    
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf("  Ready: %s%d%s  │  Sleep: %s%d%s  │  Done: %s%d/%d%s\n",
           COLOR_GREEN, ready_count, COLOR_RESET,
           COLOR_YELLOW, sleep_count, COLOR_RESET,
           COLOR_RED, done_count, NUM_PROCESSES, COLOR_RESET);
    printf("\n");
    fflush(stdout);
}

// 성능 통계 출력
void print_statistics() {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                      " COLOR_CYAN "성능 분석 결과" COLOR_RESET "                                    ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║ " COLOR_CYAN "P#" COLOR_RESET " │ " COLOR_CYAN "Wait Time" COLOR_RESET " │ " COLOR_CYAN "Response Time" COLOR_RESET " │ " COLOR_CYAN "Turnaround Time" COLOR_RESET " │ " COLOR_CYAN "Wait Count" COLOR_RESET " ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");
    
    int total_wait = 0;
    int total_response = 0;
    int total_turnaround = 0;
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int response_time = pcb[i].first_run_time - pcb[i].creation_time;
        int turnaround_time = pcb[i].completion_time - pcb[i].creation_time;
        
        printf("║ %2d │   %4d    │      %4d       │       %4d        │    %4d    ║\n",
               i,
               pcb[i].total_wait_time,
               response_time,
               turnaround_time,
               pcb[i].wait_count);
        
        total_wait += pcb[i].total_wait_time;
        total_response += response_time;
        total_turnaround += turnaround_time;
    }
    
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║ " COLOR_YELLOW "평균" COLOR_RESET " │   %4.1f    │      %4.1f       │       %4.1f        │            ║\n",
           total_wait / (float)NUM_PROCESSES,
           total_response / (float)NUM_PROCESSES,
           total_turnaround / (float)NUM_PROCESSES);
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    
    printf("\n");
    printf("📊 " COLOR_CYAN "용어 설명:" COLOR_RESET "\n");
    printf("  • " COLOR_GREEN "Wait Time" COLOR_RESET ":       Ready 큐에서 대기한 총 시간\n");
    printf("  • " COLOR_GREEN "Response Time" COLOR_RESET ":   생성부터 첫 실행까지 시간\n");
    printf("  • " COLOR_GREEN "Turnaround Time" COLOR_RESET ": 생성부터 종료까지 총 시간\n");
    printf("  • " COLOR_GREEN "Wait Count" COLOR_RESET ":      Ready 큐에 진입한 횟수\n");
    printf("\n");
    fflush(stdout);
}

// Ready 큐에 추가
void push_ready(int idx) {
    if (pcb[idx].state == DONE) return;
    
    ready_queue[ready_rear] = idx;
    ready_rear = (ready_rear + 1) % QUEUE_SIZE;
    pcb[idx].state = READY;
    
    // 성능 측정: ready 큐 진입 시간 기록
    pcb[idx].ready_enter_time = timer_tick;
    pcb[idx].wait_count++;
}

// Ready 큐에서 꺼내기
int pop_ready() {
    if (ready_front == ready_rear) {
        return -1;
    }
    int idx = ready_queue[ready_front];
    ready_front = (ready_front + 1) % QUEUE_SIZE;
    
    // 성능 측정: ready 큐에서 대기한 시간 누적
    pcb[idx].total_wait_time += (timer_tick - pcb[idx].ready_enter_time);
    
    return idx;
}

// Sleep 큐에 추가
void push_sleep(int idx) {
    sleep_queue[sleep_count++] = idx;
    pcb[idx].state = SLEEPING;
}

// Sleep 큐 업데이트
void update_sleep_queue() {
    for (int i = 0; i < sleep_count; i++) {
        int idx = sleep_queue[i];
        pcb[idx].io_wait--;
        
        if (pcb[idx].io_wait <= 0) {
            printf("    ➜ " COLOR_GREEN "P%d I/O 완료" COLOR_RESET " → Ready Queue\n", idx);
            fflush(stdout);
            
            push_ready(idx);
            
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
    printf("\n    " COLOR_MAGENTA "⚡ 모든 프로세스 타임퀀텀 초기화" COLOR_RESET "\n");
    fflush(stdout);
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb[i].state != DONE) {
            pcb[i].quantum = TIME_QUANTUM;
        }
    }
}

// 자식 프로세스 종료 핸들러
void child_done_handler(int signum) {
    pid_t child_pid;
    int status;
    
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (pcb[i].pid == child_pid) {
                printf("    ➜ " COLOR_RED "P%d 종료" COLOR_RESET "\n", i);
                fflush(stdout);
                pcb[i].state = DONE;
                pcb[i].completion_time = timer_tick;  // 종료 시간 기록
                done_count++;
                
                if (current_process == i) {
                    current_process = -1;
                    schedule();
                }
                break;
            }
        }
    }
}

// 타이머 시그널 핸들러
void timer_handler(int signum) {
    timer_tick++;
    printf("\n" COLOR_BLUE "═══════════════════ Timer Tick %d ═══════════════════" COLOR_RESET "\n", timer_tick);
    
    // Sleep 큐 업데이트
    update_sleep_queue();
    
    // 현재 실행 중인 프로세스 처리
    if (current_process != -1 && pcb[current_process].state == RUNNING) {
        pcb[current_process].quantum--;
        printf("    ➜ " COLOR_CYAN "P%d 실행" COLOR_RESET " (Quantum: %d → %d)\n", 
               current_process, pcb[current_process].quantum + 1, pcb[current_process].quantum);
        fflush(stdout);
        
        if (pcb[current_process].quantum <= 0) {
            printf("    ➜ " COLOR_YELLOW "P%d 타임퀀텀 소진" COLOR_RESET " → Ready Queue\n", current_process);
            fflush(stdout);
            
            push_ready(current_process);
            current_process = -1;
        } else {
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    // 타임퀀텀 체크 및 초기화
    if (all_quantum_zero() && done_count < NUM_PROCESSES) {
        reset_all_quantum();
    }
    
    // 스케줄링
    schedule();
    
    // 상태 출력
    print_status();
    
    // 다음 타이머 설정
    if (done_count < NUM_PROCESSES) {
        alarm(1);
    }
}

// I/O 요청 시그널 핸들러
void io_request_handler(int signum) {
    if (current_process != -1) {
        pcb[current_process].io_wait = (rand() % (MAX_IO_WAIT - MIN_IO_WAIT + 1)) + MIN_IO_WAIT;
        
        printf("    ➜ " COLOR_YELLOW "P%d I/O 요청" COLOR_RESET " (대기 시간: %d 틱)\n", 
               current_process, pcb[current_process].io_wait);
        fflush(stdout);
        
        push_sleep(current_process);
        current_process = -1;
        
        schedule();
    }
}

// 스케줄링 함수
void schedule() {
    if (current_process == -1) {
        int next = pop_ready();
        
        if (next != -1) {
            current_process = next;
            pcb[current_process].state = RUNNING;
            
            // 첫 실행 시간 기록 (Response Time 계산용)
            if (pcb[current_process].first_run_time == -1) {
                pcb[current_process].first_run_time = timer_tick;
            }
            
            printf("    ➜ " COLOR_MAGENTA "P%d 스케줄링" COLOR_RESET " (Quantum: %d)\n", 
                   current_process, pcb[current_process].quantum);
            fflush(stdout);
            
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    if (done_count >= NUM_PROCESSES) {
        printf("\n");
        printf("╔════════════════════════════════════════════════╗\n");
        printf("║                                                ║\n");
        printf("║     " COLOR_GREEN "✓ 모든 프로세스 완료!" COLOR_RESET "                  ║\n");
        printf("║     " COLOR_CYAN "시뮬레이션 종료" COLOR_RESET "                       ║\n");
        printf("║                                                ║\n");
        printf("╚════════════════════════════════════════════════╝\n");
        fflush(stdout);
        
        // 성능 통계 출력
        print_statistics();
        
        exit(0);
    }
}

// 자식 프로세스 시그널 핸들러
void child_signal_handler(int signum) {
    if (signum == SIGUSR1) {
        my_cpu_burst--;
        
        if (my_cpu_burst <= 0) {
            int choice = rand() % 2;
            
            if (choice == 0) {
                exit(0);
            } else {
                kill(getppid(), SIGUSR2);
                my_cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
            }
        }
    }
}

// 부모 프로세스
void parent_process() {
    printf("\n");
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║                                                ║\n");
    printf("║     " COLOR_CYAN "OS 스케줄링 시뮬레이션" COLOR_RESET "                ║\n");
    printf("║                                                ║\n");
    printf("║     프로세스 수: " COLOR_YELLOW "%2d" COLOR_RESET "                        ║\n", NUM_PROCESSES);
    printf("║     타임퀀텀:    " COLOR_YELLOW "%2d" COLOR_RESET "                        ║\n", TIME_QUANTUM);
    printf("║                                                ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");
    fflush(stdout);
    
    signal(SIGALRM, timer_handler);
    signal(SIGUSR2, io_request_handler);
    signal(SIGCHLD, child_done_handler);
    
    printf(COLOR_GREEN "프로세스 생성 중..." COLOR_RESET "\n");
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            child_process(i);
            exit(0);
        } else {
            pcb[i].pid = pid;
            pcb[i].quantum = TIME_QUANTUM;
            pcb[i].cpu_burst = 0;
            pcb[i].io_wait = 0;
            pcb[i].state = READY;
            
            // 성능 측정 초기화
            pcb[i].creation_time = 0;
            pcb[i].first_run_time = -1;
            pcb[i].completion_time = 0;
            pcb[i].total_wait_time = 0;
            pcb[i].ready_enter_time = 0;
            pcb[i].wait_count = 0;
            
            printf("  ✓ P%d (PID: %d)\n", i, pid);
            fflush(stdout);
            
            push_ready(i);
        }
    }
    
    printf("\n" COLOR_GREEN "스케줄러 시작..." COLOR_RESET "\n");
    fflush(stdout);
    
    sleep(2);
    
    print_status();
    schedule();
    
    alarm(1);
    
    while (1) {
        pause();
    }
}

// 자식 프로세스
void child_process(int id) {
    srand(time(NULL) + getpid());
    my_cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
    signal(SIGUSR1, child_signal_handler);
    
    while (1) {
        pause();
    }
}

int main() {
    srand(time(NULL));
    parent_process();
    return 0;
}