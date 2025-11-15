#include "main.h"

#ifdef _WIN32
// Based on: https://stackoverflow.com/a/5404467
static LARGE_INTEGER win_freq;
static int win_freq_initialized = 0;
static LONGLONG start_time;


// Simple LCG implementation for rand_r
static int rand_r(unsigned int* seedp) {
    *seedp = *seedp * 1103515245 + 12345;
    return (int)((*seedp / 65536) % 32768);
}


int clock_gettime(int clockid, struct timespec* ts) {
    (void)clockid; // Unused parameter

    if (!win_freq_initialized) {
        if (!QueryPerformanceFrequency(&win_freq)) {
            return -1; // Error
        }
        LARGE_INTEGER start_ts;
        QueryPerformanceCounter(&start_ts);
        start_time = start_ts.QuadPart;
        win_freq_initialized = 1;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG elapsed = now.QuadPart - start_time;

    ts->tv_sec = (long)(elapsed / win_freq.QuadPart);
    ts->tv_nsec = (long)(((elapsed % win_freq.QuadPart) * 1e9) / win_freq.QuadPart);

    return 0;
}
#endif

/* ----------------------- Utility Functions ----------------------- */

// <-- FIX: Removed 'static' so main.c can link to this function
int parse_positive_int(const char* s, const char* name) {
    char* end = NULL;
    errno = 0;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || val <= 0) {
        fprintf(stderr, "Invalid %s: '%s'\n", name, s);
        exit(EXIT_FAILURE);
    }
    if (val > INT_MAX) {
        fprintf(stderr, "%s too large: '%s'\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)val;
}

// <-- FIX: Removed 'static' so main.c can link to this function
double timespec_diff_sec(const struct timespec* start, const struct timespec* end) {
    double sec = (double)(end->tv_sec - start->tv_sec);
    double nsec = (double)(end->tv_nsec - start->tv_nsec) / 1e9;
    return sec + nsec;
}

/* Put an item into the circular buffer (blocking if buffer is full). */
// <-- FIX: Removed 'static' so main.c (and producers) can link to this function
void buffer_put(Item item) {
    // Wait for a free slot
    if (sem_wait(&empty_slots) != 0) {
        perror("sem_wait(empty_slots)");
        exit(EXIT_FAILURE);
    }

    // Enter critical section
    if (pthread_mutex_lock(&buffer_mutex) != 0) {
        perror("pthread_mutex_lock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    buffer[in_index] = item;
    in_index = (in_index + 1) % buffer_size;

    if (pthread_mutex_unlock(&buffer_mutex) != 0) {
        perror("pthread_mutex_unlock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    // Signal that a new item is available
    if (sem_post(&full_slots) != 0) {
        perror("sem_post(full_slots)");
        exit(EXIT_FAILURE);
    }
}

/* Get an item from the circular buffer (blocking if buffer is empty). */
// <-- FIX: Removed 'static' so consumers can link to this function
Item buffer_get(void) {
    Item item;
    // Wait for an available item
    if (sem_wait(&full_slots) != 0) {
        perror("sem_wait(full_slots)");
        exit(EXIT_FAILURE);
    }

    // Enter critical section
    if (pthread_mutex_lock(&buffer_mutex) != 0) {
        perror("pthread_mutex_lock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    item = buffer[out_index];
    out_index = (out_index + 1) % buffer_size;

    if (pthread_mutex_unlock(&buffer_mutex) != 0) {
        perror("pthread_mutex_unlock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    // Signal that a slot became free
    if (sem_post(&empty_slots) != 0) {
        perror("sem_post(empty_slots)");
        exit(EXIT_FAILURE);
    }
    return item;
}

/* ----------------------- Thread Functions ----------------------- */

// <-- FIX: Removed 'static' (optional, but good practice for prototyped funcs)
void* producer_thread(void* arg) {
    ProducerArgs* pargs = (ProducerArgs*)arg;
    int id = pargs->id;
    unsigned int seed = pargs->seed;

    for (int i = 0; i < items_per_producer; ++i) {
        Item item;
        // Generate a non-negative random integer (avoid POISON_PILL which is -1)
        int value = (int)(rand_r(&seed) % 1000); // 0..999
        item.value = value;
        clock_gettime(CLOCK_REALTIME, &item.enqueue_ts);

        buffer_put(item);

        printf("[Producer-%d] Produced item: %d\n", id, value);
    }
    printf("[Producer-%d] Finished producing %d items.\n", id, items_per_producer);
    return NULL;
}

// <-- FIX: Removed 'static' (optional)
void* consumer_thread(void* arg) {
    ConsumerArgs* cargs = (ConsumerArgs*)arg;
    int id = cargs->id;
    while (1) {
        Item item = buffer_get();

        if (item.value == POISON_PILL) {
            printf("[Consumer-%d] Received POISON_PILL. Exiting.\n", id);
            break; // graceful termination
        }

        struct timespec dequeue_ts;
        clock_gettime(CLOCK_REALTIME, &dequeue_ts);

        double latency = timespec_diff_sec(&item.enqueue_ts, &dequeue_ts);

        if (pthread_mutex_lock(&stats_mutex) != 0) {
            perror("pthread_mutex_lock(stats_mutex)");
            exit(EXIT_FAILURE);
        }

        total_real_items_consumed++;
        total_latency_sec += latency;

        if (pthread_mutex_unlock(&stats_mutex) != 0) {
            perror("pthread_mutex_unlock(stats_mutex)");
            exit(EXIT_FAILURE);
        }

        printf("[Consumer-%d] Consumed item: %d (latency: %.6f s)\n", id, item.value, latency);
    }
    return NULL;
}