//Yusairah Haque 
//10/17
//Homework 1 - Operating systems 
//producer.c 
// Build:  gcc producer.c -pthread -lrt -o producer
// Run:    ./producer

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <semaphore.h>
#include <errno.h>
#include <signal.h>
#define SHM_NAME "/pc_shm"
#define SEM_EMPTY "/sem_empty"
#define SEM_FULL "/sem_full"
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
static void on_sigint(int signo);   

static void on_sigint(int signo) {
    (void)signo; running = 0; //sets running to 0 so while(running) loop exists nice
}

static void cleanup(void) {
    if (shm) munmap(shm, sizeof(*shm)); //detachs the process's mapping of the shared memory 
    if (shm_fd != -1) close(shm_fd);    //closes file descriptor for the POSIX shm object
    if (sem_empty) sem_close(sem_empty);//sem_close detaches from the name semaphore object
    if (sem_full)  sem_close(sem_full);
    if (sem_mutex) sem_close(sem_mutex);
    // when totally done (both proccesses stopped), you may unlink in a separate cleanup tool.
}

static void *producer_thread(void *arg) {
    (void)arg;
    int item = 0;       // counter. loop produces 0, 1, 2, etc...
    while (running) {
        int value = item++; 

        sem_wait(sem_empty); //counts empty slots. if buffer is full it waits
        sem_wait(sem_mutex); //binary lock so only one side touches the shared buffer at a time

        //writes the item into the buffer
        shm->buffer[shm->in] = value; 
        int slot = shm->in;        //records were you wrote. 
        shm->in = (shm->in + 1)% N;//goes back and forth between 0 and 1
        printf("[PROD] put %d in slot %d | in=%d out=%d\n", value, slot, shm->in, shm->out);
        fflush(stdout); 

        sem_post(sem_mutex);
        sem_post(sem_full);     //increments and lets the consumer know it can proceed 

        usleep(300 * 1000);     //delay for readablity 
    }
    return NULL;    //exits when running = 0.
}

//error helper. prints msg and error reason and exits 
static void die(const char *msg) { 
    perror(msg); exit(1); 
}

int main(void) {
    //sets running to 0
    //signal(SIGINT, on_sigint);
    //makes sure cleanup runs 
    atexit(cleanup);

    //create or open shared memory
    shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
    if (shm_fd == -1) die("shm_open");
    if (ftruncate(shm_fd, sizeof(*shm)) == -1) die("ftruncate"); //ftruncate fits shm_t size

    //map the shared memory into process's address space 
    shm = mmap(NULL, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) die("mmap");

    //create/open semaphores. named, so both processes see them
    sem_empty = sem_open(SEM_EMPTY, O_CREAT, 0666, N); //starts at N: 2 empty slots
    sem_full  = sem_open(SEM_FULL,  O_CREAT, 0666, 0); //starts at 0 nothing is produced
    sem_mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1); //starts at1: unlocked
    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED || sem_mutex == SEM_FAILED) die("sem_open");

    // initialize indices and are within [0, N-1] (safe if producer runs first; harmless if already set)
    shm->in = shm->in % N;
    shm->out = shm->out % N;

    //spawns worker thread and waits for it to finish: when Ctrl + C is pressed   
    pthread_t t;
    if (pthread_create(&t, NULL, producer_thread, NULL) != 0) die("pthread_create");
    pthread_join(t, NULL);

    printf("[PROD] exiting\n");
    return 0;
}