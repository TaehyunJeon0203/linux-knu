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
    printf("\n========== 성능 분석 결과 ==========\n");
    printf("P#\t대기시간\t응답시간\t반환시간\t대기횟수\n");
    printf("--------------------------------------------\n");
    
    int total_wait = 0;
    int total_response = 0;
    int total_turnaround = 0;
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int response_time = pcb[i].first_run_time - pcb[i].creation_time;
        int turnaround_time = pcb[i].completion_time - pcb[i].creation_time;
        
        printf("%d\t%d\t\t%d\t\t%d\t\t%d\n",
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
    printf("평균\t%.1f\t\t%.1f\t\t%.1f\n",
           total_wait / (float)NUM_PROCESSES,
           total_response / (float)NUM_PROCESSES,
           total_turnaround / (float)NUM_PROCESSES);
    printf("========================================\n\n");
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

// Sleep 큐 업데이트 (대기 시간 감소)
void update_sleep_queue() {
    for (int i = 0; i < sleep_count; i++) {
        int idx = sleep_queue[i];
        pcb[idx].io_wait--;
        
        if (pcb[idx].io_wait <= 0) {
            printf("  [I/O완료] P%d I/O 대기시간 만료 -> ready 큐로 이동\n", idx);
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

// 전체 프로세스의 타임퀀텀 초기화
void reset_all_quantum() {
    printf("  [타임퀀텀 초기화] 모든 프로세스의 타임퀀텀 초기화\n");
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
                printf("  [프로세스 종료] P%d 종료\n", i);
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
    printf("\n==================== [타이머 틱 %d] ====================\n", timer_tick);
    
    // Sleep 큐 업데이트
    update_sleep_queue();
    
    // 실행 중인 프로세스 처리
    if (current_process != -1 && pcb[current_process].state == RUNNING) {
        // 타임퀀텀 1 감소
        pcb[current_process].quantum--;
        pcb[current_process].cpu_burst--;
        
        printf("  [실행] P%d 실행 중 (남은 타임퀀텀: %d, 남은 CPU버스트: %d)\n", 
               current_process, 
               pcb[current_process].quantum,
               pcb[current_process].cpu_burst);
        
        // 타임퀀텀이 0이 되면 다음 프로세스로 변경
        if (pcb[current_process].quantum <= 0) {
            printf("  [타임퀀텀 소진] P%d 타임퀀텀 0 -> ready 큐로 이동\n", current_process);
            push_ready(current_process);
            current_process = -1;
        } else {
            // 0이 아니면 현재 프로세스 계속 실행
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    // 모든 프로세스의 타임퀀텀이 0이 되면 전체 초기화
    if (all_quantum_zero() && done_count < NUM_PROCESSES) {
        reset_all_quantum();
    }
    
    // 스케줄링
    schedule();
    
    printf("======================================================\n");
    
    if (done_count < NUM_PROCESSES) {
        alarm(1);
    }
}

// I/O 요청 시그널 핸들러
void io_request_handler(int signum) {
    if (current_process != -1) {
        // I/O 대기시간 랜덤으로 할당 (1~5)
        pcb[current_process].io_wait = (rand() % (MAX_IO_WAIT - MIN_IO_WAIT + 1)) + MIN_IO_WAIT;
        
        // 새로운 CPU 버스트 할당
        pcb[current_process].cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
        
        printf("  [I/O 요청] P%d I/O 요청 -> sleep 큐에 삽입 (대기시간: %d, 새 CPU버스트: %d)\n", 
               current_process, pcb[current_process].io_wait, pcb[current_process].cpu_burst);
        
        push_sleep(current_process);
        current_process = -1;
        schedule();
    }
}

// 라운드로빈 스케줄링 수행
void schedule() {
    if (current_process == -1) {
        int next = pop_ready();
        
        if (next != -1) {
            current_process = next;
            pcb[current_process].state = RUNNING;
            
            if (pcb[current_process].first_run_time == -1) {
                pcb[current_process].first_run_time = timer_tick;
            }
            
            printf("  [스케줄링] P%d 스케줄링됨 (타임퀀텀: %d, CPU버스트: %d)\n", 
                   current_process, 
                   pcb[current_process].quantum,
                   pcb[current_process].cpu_burst);
            kill(pcb[current_process].pid, SIGUSR1);
        }
    }
    
    if (done_count >= NUM_PROCESSES) {
        printf("\n========================================\n");
        printf("모든 프로세스 완료!\n");
        printf("========================================\n");
        print_statistics();
        exit(0);
    }
}

// 자식 프로세스 시그널 핸들러
void child_signal_handler(int signum) {
    if (signum == SIGUSR1) {
        // 시그널을 받고 실행할 때마다 CPU 버스트 1 감소
        my_cpu_burst--;
        
        // CPU 버스트가 0이 되면 프로세스 종료 혹은 I/O 수행(랜덤)
        if (my_cpu_burst <= 0) {
            int choice = rand() % 2;
            
            if (choice == 0) {
                // 프로세스 종료
                exit(0);
            } else {
                // I/O 수행 - 부모 프로세스에 I/O 요청 시그널 보냄
                kill(getppid(), SIGUSR2);
                my_cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
            }
        }
    }
}

// 부모 프로세스 (커널 역할)
void parent_process() {
    printf("\n");
    printf("======================================================\n");
    printf("           OS 스케줄링 시뮬레이션\n");
    printf("======================================================\n");
    printf("  자식 프로세스 수: %d개\n", NUM_PROCESSES);
    printf("  타임퀀텀: %d\n", TIME_QUANTUM);
    printf("  CPU 버스트 범위: %d ~ %d\n", MIN_CPU_BURST, MAX_CPU_BURST);
    printf("  I/O 대기시간 범위: %d ~ %d\n", MIN_IO_WAIT, MAX_IO_WAIT);
    printf("======================================================\n\n");
    
    // 시그널 핸들러 설정
    signal(SIGALRM, timer_handler);
    signal(SIGUSR2, io_request_handler);
    signal(SIGCHLD, child_done_handler);
    
    printf("자식 프로세스 10개 생성 중...\n");
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // 자식 프로세스
            child_process(i);
            exit(0);
        } else {
            // 부모 프로세스 - PCB로 자식 프로세스 관리
            pcb[i].pid = pid;
            pcb[i].quantum = TIME_QUANTUM;
            pcb[i].cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
            pcb[i].io_wait = 0;
            pcb[i].state = READY;
            
            pcb[i].creation_time = 0;
            pcb[i].first_run_time = -1;
            pcb[i].completion_time = 0;
            pcb[i].total_wait_time = 0;
            pcb[i].ready_enter_time = 0;
            pcb[i].wait_count = 0;
            
            printf("  P%d 생성 (PID: %d, 초기 CPU버스트: %d)\n", i, pid, pcb[i].cpu_burst);
            push_ready(i);
        }
    }
    
    printf("\n라운드로빈 스케줄링 시작...\n");
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
    
    // 처음 생성시 CPU 버스트 랜덤 초기화 (1~10)
    my_cpu_burst = (rand() % (MAX_CPU_BURST - MIN_CPU_BURST + 1)) + MIN_CPU_BURST;
    
    // 시그널 핸들러 설정
    signal(SIGUSR1, child_signal_handler);
    
    // sleep() 상태로 대기
    // 부모 프로세스가 해당 프로세스를 스케줄링하여 시그널을 보내면 실행 시작
    while (1) {
        pause();
    }
}

int main() {
    srand(time(NULL));
    parent_process();
    return 0;
}