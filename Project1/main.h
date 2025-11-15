// <-- FIX: Added include guards to prevent multiple inclusion
#ifndef MAIN_H_
#define MAIN_H_
#define _POSIX_C_SOURCE 199309L
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h> // For struct timespec, clock_gettime

#define DEFAULT_ITEMS_PER_PRODUCER 20
#define POISON_PILL -1

#ifdef _WIN32
#include <Windows.h> // For high-resolution timer
#define CLOCK_REALTIME 0
int clock_gettime(int clockid, struct timespec* ts);
#endif

/* ----------------------- Data Structures ----------------------- */

typedef struct {
    int value; // item value (POISON_PILL for sentinel)
    struct timespec enqueue_ts; // time when producer enqueued the item
} Item;

typedef struct {
    int id; // human-readable id starting from 1
    unsigned int seed; // thread-local RNG seed
} ProducerArgs;

typedef struct {
    int id;
} ConsumerArgs;

/* ----------------------- Global Variables (Declarations) ----------------------- */
// <-- FIX: Changed all 'static' to 'extern'.
// 'extern' declares the variable, but the definition (memory)
// will be in one of the .c files (we'll put it in main.c).
// <-- FIX: Removed initializers (like = NULL or = 0).
// Initializers only go with the *definition*.

extern Item* buffer; // circular buffer
extern int buffer_size; // number of slots
extern int in_index; // next position to insert
extern int out_index; // next position to remove

// Synchronization primitives
extern sem_t empty_slots; // counts free slots
extern sem_t full_slots; // counts filled slots
extern pthread_mutex_t buffer_mutex; // protects buffer indices & content
extern pthread_mutex_t stats_mutex;

// Statistics
extern long long total_real_items_consumed;
extern double total_latency_sec;

// Global configuration
extern int num_producers;
extern int num_consumers;
extern int items_per_producer;

// For throughput
extern struct timespec program_start_ts;
extern struct timespec program_end_ts;

/* ----------------------- Function Prototypes ----------------------- */
// <-- FIX: Added prototypes for all functions defined in producer_consumer.c
// so that main.c knows about them.

// Thread functions
void* producer_thread(void* arg);
void* consumer_thread(void* arg);

// Buffer functions
void buffer_put(Item item);
Item buffer_get(void);

// Utility functions
int parse_positive_int(const char* s, const char* name);
double timespec_diff_sec(const struct timespec* start, const struct timespec* end);

#endif 