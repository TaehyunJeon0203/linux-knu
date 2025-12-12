#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define NPROC 10
#define INIT_TQ 3
#define IO_WAIT_MIN 1
#define IO_WAIT_MAX 5

typedef enum { READY, RUNNING, SLEEP, DONE } state_t;

typedef struct {
    pid_t pid;
    int cpu_burst;
    int time_quantum;
    state_t state;
    int io_wait;
} PCB;

PCB pcb[NPROC];
int ready_q[NPROC], rq_head=0, rq_tail=0;

int running = -1;
int sleep_cnt = 0;
int done_cnt = 0;

void push_ready(int i){ ready_q[rq_tail] = i; rq_tail=(rq_tail+1)%NPROC; }
int pop_ready(){
    if(rq_head==rq_tail) return -1;
    int x = ready_q[rq_head];
    rq_head=(rq_head+1)%NPROC;
    return x;
}

void child_handler(int sig){
    /* 자식 로그인 */
    /* SIGUSR1 = CPU 한 틱 실행 */
}

volatile sig_atomic_t child_signal = 0;

void child_sig(int sig){
    child_signal = 1;
}

void child_process(){
    signal(SIGUSR1, child_sig);

    srand(getpid());

    int cpu = rand() % 10 + 1;
    printf("[Child %d] CPU burst initialized: %d\n", getpid(), cpu);

    while(1){
        pause();  // 부모가 SIGUSR1 보낼 때까지 대기

        if(child_signal){
            child_signal = 0;
            cpu--;
            printf("[Child %d] CPU burst remaining: %d\n", getpid(), cpu);

            if(cpu <= 0){
                int do_io = rand() % 2;

                if(do_io == 0){
                    /* 종료 */
                    printf("[Child %d] Terminating\n", getpid());
                    exit(0);
                } else {
                    /* I/O 요청 -> 부모에게 SIGUSR2 */
                    printf("[Child %d] Requesting I/O\n", getpid());
                    kill(getppid(), SIGUSR2);
                    pause(); // I/O 완료 시그널 대기
                    
                    // I/O 완료 후 새로운 CPU 버스트
                    cpu = rand() % 10 + 1;
                    printf("[Child %d] I/O complete, new CPU burst: %d\n", getpid(), cpu);
                }
            }
        }
    }
}

volatile sig_atomic_t io_request = 0;

void parent_io_signal(int s){
    io_request = 1;
}

void init(){
    srand(time(NULL));
    for(int i=0;i<NPROC;i++){
        pcb[i].cpu_burst = rand()%10 + 1;
        pcb[i].time_quantum = INIT_TQ;
        pcb[i].state = READY;
        pcb[i].io_wait = 0;
    }
}

int all_tq_zero(){
    for(int i=0;i<NPROC;i++){
        if(pcb[i].state != DONE && pcb[i].time_quantum > 0)
            return 0;
    }
    return 1;
}

void timer_tick(int sig){
    static int tick=0;
    tick++;
    printf("\n===== TICK %d ===== (Running: %d, Ready: %d, Sleep: %d, Done: %d)\n", 
           tick, running>=0?1:0, (rq_tail-rq_head+NPROC)%NPROC, sleep_cnt, done_cnt);

    /* I/O wait 감소 */
    for(int i=0;i<NPROC;i++){
        if(pcb[i].state == SLEEP){
            pcb[i].io_wait--;
            if(pcb[i].io_wait <= 0){
                pcb[i].state = READY;
                push_ready(i);
                printf("PID %d I/O 완료 -> READY\n", pcb[i].pid);
                sleep_cnt--;
                kill(pcb[i].pid, SIGCONT); // I/O 끝났다고 unblock
            }
        }
    }

    /* 실행 중인 프로세스 없으면 스케줄 */
    if(running == -1){
        running = pop_ready();
        if(running != -1){
            pcb[running].state = RUNNING;
            printf("PID %d 스케줄됨 (PCB index: %d)\n", pcb[running].pid, running);
        } else {
            printf("Ready 큐가 비어있음 (head=%d, tail=%d)\n", rq_head, rq_tail);
        }
    }

    if(running != -1){
        /* 타임퀀텀 감소 */
        pcb[running].time_quantum--;
        printf("PID %d 실행중 (TQ: %d)\n", pcb[running].pid, pcb[running].time_quantum);

        /* 자식에 SIGUSR1 보내서 1틱 실행 */
        kill(pcb[running].pid, SIGUSR1);
        usleep(10000); // 자식이 처리할 시간 부여

        /* 자식이 I/O 요청 보냈는지 확인 */
        if(io_request){
            io_request = 0;
            pcb[running].state = SLEEP;
            pcb[running].io_wait = rand()%(IO_WAIT_MAX-IO_WAIT_MIN+1) + IO_WAIT_MIN;
            printf("PID %d -> I/O 요청 (wait=%d)\n", pcb[running].pid, pcb[running].io_wait);
            sleep_cnt++;
            running = -1;
        }

        else {
            /* 자식이 종료했는지 체크 */
            int status;
            pid_t res = waitpid(pcb[running].pid, &status, WNOHANG);

            if(res == pcb[running].pid){
                pcb[running].state = DONE;
                printf("PID %d 종료\n", pcb[running].pid);
                done_cnt++;
                running = -1;
            }
            else if(pcb[running].time_quantum <= 0){
                /* 타임퀀텀 소진 → preempt */
                printf("PID %d 타임퀀텀 소진 -> READY\n", pcb[running].pid);
                pcb[running].state = READY;
                pcb[running].time_quantum = INIT_TQ;
                push_ready(running);
                running = -1;
            }
        }
    }

    /* 전체 타임퀀텀 초기화 조건 */
    if(all_tq_zero()){
        for(int i=0;i<NPROC;i++){
            if(pcb[i].state != DONE)
                pcb[i].time_quantum = INIT_TQ;
        }
        printf("모든 프로세스 TQ = 0 → 전체 초기화\n");
    }

    alarm(1);
}

int main(){
    setbuf(stdout, NULL); // 출력 버퍼 비활성화
    
    init();

    signal(SIGUSR2, parent_io_signal);
    signal(SIGALRM, timer_tick);

    /* fork 10개 */
    for(int i=0;i<NPROC;i++){
        pid_t pid = fork();
        if(pid == 0){
            child_process();
            return 0;
        }
        pcb[i].pid = pid;
        push_ready(i);
        printf("Created child PID %d (index %d), TQ=%d\n", pid, i, pcb[i].time_quantum);
    }

    printf("\nAll children created. Starting scheduler...\n");
    printf("Ready queue: head=%d, tail=%d\n", rq_head, rq_tail);
    
    sleep(1); // 자식들이 초기화될 시간을 줌
    
    alarm(1);

    /* 부모 메인 루프 */
    while(done_cnt < NPROC){
        pause();
    }

    printf("모든 프로세스 종료됨\n");
    return 0;
}