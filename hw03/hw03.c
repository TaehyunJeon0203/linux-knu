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
    int creation_time;
    int first_run_time;
    int completion_time;
    int total_wait_time;
    int ready_enter_time;
    int wait_count;
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
void print_statistics();

// 성능 통계 출력
void print_statistics() {
    printf("\n========== 성능 통계 ==========\n");
    printf("P#\tWait\tResponse\tTurnaround\tWait Count\n");
    printf("--------------------------------------------\n");
    
    int total_wait = 0;
    int total_response = 0;
    int total_turnaround = 0;
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int response_time = pcb[i].first_run_time - pcb[i].creation_time;
        int turnaround_time = pcb[i].completion_time - pcb[i].creation_time;
        
        printf("%d\t%d\t%d\t\t%d\t\t%d\n",
               i,
               pcb[i].total_wait_time,
               response_time,
               turnaround_time,
               pcb[i].wait_count);
        
        total_wait += pcb[i].total_wait_time;
        total_response += response_time;
        total_turnaround += turnaround_time;
    }
    
    printf("--------------------------------------------\n");
    printf("Avg\t%.1f\t%.1f\t\t%.1f\n",
           total_wait / (float)NUM_PROCESSES,
           total_response / (float)NUM_PROCESSES,
           total_turnaround / (float)NUM_PROCESSES);
    printf("============================================\n\n");
}

// Ready 큐에 추가
void push_ready(int idx) {
    if (pcb[idx].state == DONE) return;
    
    ready_queue[ready_rear] = idx;
    ready_rear = (ready_rear + 1) % QUEUE_SIZE;
    pcb[idx].state = READY;
    
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
            printf("[Tick %d] P%d I/O completed -> Ready queue\n", timer_tick, idx);
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
    printf("[Tick %d] All quantum exhausted, resetting...\n", timer_tick);
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
                printf("[Tick %d] P%d terminated\n", timer_tick, i);
                pcb[i].state = DONE;
                pcb[i].completion_time = timer_tick;
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
    printf("\n[Tick %d]\n", timer_tick);
    
    update_sleep_queue();
    
    if (current_process != -1 && pcb[current_process].state == RUNNING) {
        pcb[current_process].quantum--;
        printf("P%d running (quantum: %d -> %d)\n", 
               current_process, 
               pcb[current_process].quantum + 1, 
               pcb[current_process].quantum);
        
        if (pcb[current_process].quantum <= 0) {
            printf("P%d quantum exhausted -> Ready queue\n", current_process);
            push_ready(current_process);
            current_process = -1;
        } else {
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    if (all_quantum_zero() && done_count < NUM_PROCESSES) {
        reset_all_quantum();
    }
    
    schedule();
    
    if (done_count < NUM_PROCESSES) {
        alarm(1);
    }
}

// I/O 요청 시그널 핸들러
void io_request_handler(int signum) {
    if (current_process != -1) {
        pcb[current_process].io_wait = (rand() % (MAX_IO_WAIT - MIN_IO_WAIT + 1)) + MIN_IO_WAIT;
        printf("[Tick %d] P%d requested I/O (wait: %d ticks)\n", 
               timer_tick, current_process, pcb[current_process].io_wait);
        
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
            
            if (pcb[current_process].first_run_time == -1) {
                pcb[current_process].first_run_time = timer_tick;
            }
            
            printf("P%d scheduled (quantum: %d)\n", current_process, pcb[current_process].quantum);
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    if (done_count >= NUM_PROCESSES) {
        printf("\n========================================\n");
        printf("All processes completed!\n");
        printf("========================================\n");
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
    printf("========================================\n");
    printf("OS Scheduling Simulation\n");
    printf("Processes: %d, Time Quantum: %d\n", NUM_PROCESSES, TIME_QUANTUM);
    printf("========================================\n\n");
    
    signal(SIGALRM, timer_handler);
    signal(SIGUSR2, io_request_handler);
    signal(SIGCHLD, child_done_handler);
    
    printf("Creating processes...\n");
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
            
            pcb[i].creation_time = 0;
            pcb[i].first_run_time = -1;
            pcb[i].completion_time = 0;
            pcb[i].total_wait_time = 0;
            pcb[i].ready_enter_time = 0;
            pcb[i].wait_count = 0;
            
            printf("P%d created (PID: %d)\n", i, pid);
            push_ready(i);
        }
    }
    
    printf("\nStarting scheduler...\n");
    sleep(2);
    
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