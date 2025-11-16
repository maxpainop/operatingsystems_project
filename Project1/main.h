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
#include <time.h>

#define DEFAULT_ITEMS_PER_PRODUCER 20
#define POISON_PILL -1

/* ----------------------- Data Structures ----------------------- */

typedef struct {
    int value;                   // item value (POISON_PILL for sentinel)
    int priority;                // 0 = normal, 1 = urgent
    struct timespec enqueue_ts;  // enqueue timestamp
} Item;

typedef struct {
    int id;
    unsigned int seed;
} ProducerArgs;

typedef struct {
    int id;
} ConsumerArgs;

/* ----------------------- Global Variables ----------------------- */
/* Two priority queues sharing one bounded capacity */

extern Item* urgent_buffer;
extern Item* normal_buffer;
extern int buffer_size;

extern int urgent_in;
extern int urgent_out;
extern int urgent_count;

extern int normal_in;
extern int normal_out;
extern int normal_count;

/* Synchronization primitives */
extern sem_t empty_slots;   // available slots in the bounded buffer
extern sem_t full_slots;    // total filled items (urgent + normal)
extern pthread_mutex_t buffer_mutex;
extern pthread_mutex_t stats_mutex;

/* Statistics */
extern long long total_real_items_consumed;
extern double total_latency_sec;

/* Global configuration */
extern int num_producers;
extern int num_consumers;
extern int items_per_producer;

/* Timing */
extern struct timespec program_start_ts;
extern struct timesspec program_end_ts;

/* Thread and buffer functions */
void* producer_thread(void* arg);
void* consumer_thread(void* arg);
void buffer_put(Item item);
Item buffer_get(void);

/* Utility */
int parse_positive_int(const char* s, const char* name);
double timespec_diff_sec(const struct timespec* start, const struct timespec* end);

/* Time wrapper */
void get_current_time(struct timespec* ts);

#endif
