// Yusairah Haque
// 10/17
// Homework 1 - Operating systems
// consumer.c
// Build:  gcc consumer.c -pthread -lrt -o consumer
// Run:    ./consumer
// Stop:   pkill consumer

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define SHM_NAME  "/pc_shm"
#define SEM_EMPTY "/sem_empty"
#define SEM_FULL  "/sem_full"
#define SEM_MUTEX "/sem_mutex"
#define N 2

//defines custom data type shm_t which represent the shared memory region: chunk of 
//  memory that both the producer and consumer proccesses can access at the same time.
typedef struct {
    int buffer[N]; //the table and it holds up to 2 as defined. 
    int in;     //next write indesx. increases every time new item is produced and wraps arounod using in = (in+1)%N; producer uses it 
    int out;    //next read index. out = (out+1)%N;. consumer uses it 
} shm_t; //both programs use thsi through shared memory 

static int shm_fd = -1;     //file descriptor. -1 means not opened yet
static shm_t *shm = NULL;   //shm is the pointer returned by mmap to the shared memory
static sem_t *sem_empty = NULL, *sem_full = NULL, *sem_mutex = NULL; //named semaphores 
static volatile sig_atomic_t running = 1;

//loop exits cleanly 
static void on_sigint(int _) { (void)_; running = 0; }

//detaches process from resources. does not destory them globally 
static void cleanup(void) {
    if (shm) munmap(shm, sizeof(*shm));
    if (shm_fd != -1) close(shm_fd);
    if (sem_empty) sem_close(sem_empty);
    if (sem_full)  sem_close(sem_full);
    if (sem_mutex) sem_close(sem_mutex);
}

static void *consumer_thread(void *arg) {
    (void)arg;
    while (running) {
        //if full==0 then waits here 
        sem_wait(sem_full);
        //locks critical section so only producer/consumer can use shared once at a time. 
        sem_wait(sem_mutex);

        int value = shm->buffer[shm->out];
        int slot = shm->out;
        shm->out = (shm->out + 1) % N;
        printf("[CONS] got %d from slot %d | in=%d out=%d\n", value, slot, shm->in, shm->out);
        fflush(stdout);
        //unlocks critical section 
        sem_post(sem_mutex);
        //signals there is 1 more empty slot. gives producer go ahead
        sem_post(sem_empty);
        // 500 ms. slows for readability
        usleep(500 * 1000); // 500 ms
    }
    //makes sure each line prints immediately 
    return NULL;
}

//print OS error quickly
static void die(const char *msg) { perror(msg); exit(1); }

int main(void) {
    //hooks ctrl+c so cleanup runs on normal exit 
    //signal(SIGINT, on_sigint);
    atexit(cleanup);

    //open (or create and sizes if launched first, before consumer) shared memory.
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
        if (shm_fd == -1) die("shm_open");
        if (ftruncate(shm_fd, sizeof(shm_t)) == -1) die("ftruncate");
    }

    //mmap makes this region visible as a shm_t*
    shm = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) die("mmap");

    //first tries to open what semaphores producer created; if missing, creates with 
    //proper intial counts. 
    //emtpty = N 
    sem_empty = sem_open(SEM_EMPTY, 0);
    if (sem_empty == SEM_FAILED) sem_empty = sem_open(SEM_EMPTY, O_CREAT, 0666, N);
    //full = 0 
    sem_full  = sem_open(SEM_FULL, 0);
    if (sem_full == SEM_FAILED)  sem_full  = sem_open(SEM_FULL,  O_CREAT, 0666, 0);
    //mutex = 1
    sem_mutex = sem_open(SEM_MUTEX, 0);
    if (sem_mutex == SEM_FAILED) sem_mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);

    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED || sem_mutex == SEM_FAILED) die("sem_open");
    //start the consumer loop in a thread; join waits until it exits aka when ctrl+c is pressed 
    pthread_t t;
    if (pthread_create(&t, NULL, consumer_thread, NULL) != 0) die("pthread_create");
    pthread_join(t, NULL);

    printf("[CONS] exiting\n");
    return 0;
}
