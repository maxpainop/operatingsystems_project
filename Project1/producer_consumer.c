#include "main.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ----------------------- Utility Functions ----------------------- */

/* Simple thread-local pseudo-random generator (similar to rand_r) */
static int my_rand_r(unsigned int* seedp) {
    *seedp = *seedp * 1103515245u + 12345u;
    return (int)((*seedp / 65536u) % 32768u);
}

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

double timespec_diff_sec(const struct timespec* start, const struct timespec* end) {
    double sec  = (double)(end->tv_sec  - start->tv_sec);
    double nsec = (double)(end->tv_nsec - start->tv_nsec) / 1e9;
    return sec + nsec;
}

/* ----------------------- Time Wrapper ----------------------- */

void get_current_time(struct timespec* ts) {
#ifdef _WIN32
    /* High-resolution timer using QueryPerformanceCounter */
    static LARGE_INTEGER freq;
    static int initialized = 0;
    LARGE_INTEGER now;

    if (!initialized) {
        if (!QueryPerformanceFrequency(&freq)) {
            /* Fallback to time() if high-res timer is unavailable */
            time_t t = time(NULL);
            ts->tv_sec = (long)t;
            ts->tv_nsec = 0;
            return;
        }
        initialized = 1;
    }

    QueryPerformanceCounter(&now);
    long double seconds = (long double)now.QuadPart / (long double)freq.QuadPart;

    ts->tv_sec  = (time_t)seconds;
    ts->tv_nsec = (long)((seconds - (long double)ts->tv_sec) * 1e9L);
#else
    clock_gettime(CLOCK_REALTIME, ts);
#endif
}

/* ----------------------- Buffer Operations ----------------------- */

/* Insert item into bounded buffer (two priority queues share one capacity). */
void buffer_put(Item item) {
    if (item.value == POISON_PILL) {
        item.priority = 0;  // poison is always normal priority
    }

    /* Block if the bounded buffer is full */
    if (sem_wait(&empty_slots) != 0) {
        perror("sem_wait(empty_slots)");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_lock(&buffer_mutex) != 0) {
        perror("pthread_mutex_lock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    if (item.priority == 1) {
        urgent_buffer[urgent_in] = item;
        urgent_in = (urgent_in + 1) % buffer_size;
        urgent_count++;
    } else {
        normal_buffer[normal_in] = item;
        normal_in = (normal_in + 1) % buffer_size;
        normal_count++;
    }

    if (pthread_mutex_unlock(&buffer_mutex) != 0) {
        perror("pthread_mutex_unlock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    /* One more filled slot (urgent or normal) */
    if (sem_post(&full_slots) != 0) {
        perror("sem_post(full_slots)");
        exit(EXIT_FAILURE);
    }
}

/* Remove item from bounded buffer, always preferring urgent items when available. */
Item buffer_get(void) {
    Item item;

    /* Block until there is at least one item (urgent or normal) */
    if (sem_wait(&full_slots) != 0) {
        perror("sem_wait(full_slots)");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_lock(&buffer_mutex) != 0) {
        perror("pthread_mutex_lock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    if (urgent_count > 0) {
        item = urgent_buffer[urgent_out];
        urgent_out = (urgent_out + 1) % buffer_size;
        urgent_count--;
    } else {
        item = normal_buffer[normal_out];
        normal_out = (normal_out + 1) % buffer_size;
        normal_count--;
    }

    if (pthread_mutex_unlock(&buffer_mutex) != 0) {
        perror("pthread_mutex_unlock(buffer_mutex)");
        exit(EXIT_FAILURE);
    }

    if (sem_post(&empty_slots) != 0) {
        perror("sem_post(empty_slots)");
        exit(EXIT_FAILURE);
    }

    return item;
}

/* ----------------------- Thread Functions ----------------------- */

void* producer_thread(void* arg) {
    ProducerArgs* pargs = (ProducerArgs*)arg;
    int id = pargs->id;
    unsigned int seed = pargs->seed;

    for (int i = 0; i < items_per_producer; ++i) {
        Item item;
        int value = (int)(my_rand_r(&seed) % 1000);  // 0..999

        /* Roughly 25% of items are urgent */
        int priority = (my_rand_r(&seed) % 4 == 0) ? 1 : 0;

        item.value = value;
        item.priority = priority;
        get_current_time(&item.enqueue_ts);

        buffer_put(item);

        if (priority == 1) {
            printf("[Producer-%d] Produced URGENT item: %d\n", id, value);
        } else {
            printf("[Producer-%d] Produced item: %d\n", id, value);
        }
    }

    printf("[Producer-%d] Finished producing %d items.\n", id, items_per_producer);
    return NULL;
}

void* consumer_thread(void* arg) {
    ConsumerArgs* cargs = (ConsumerArgs*)arg;
    int id = cargs->id;

    while (1) {
        Item item = buffer_get();

        if (item.value == POISON_PILL) {
            printf("[Consumer-%d] Received POISON_PILL. Exiting.\n", id);
            break;
        }

        struct timespec dequeue_ts;
        get_current_time(&dequeue_ts);

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

        if (item.priority == 1) {
            printf("[Consumer-%d] Consumed URGENT item: %d (latency: %.6f s)\n",
                   id, item.value, latency);
        } else {
            printf("[Consumer-%d] Consumed item: %d (latency: %.6f s)\n",
                   id, item.value, latency);
        }
    }

    return NULL;
}
