

/*
        XXXXXXXXX        
     XXX         XXX     
   XX               XX   
  X                   X  
 X   XXXX       XXX    X 
X    XXXX       XXX     X
X    XXXX  X X  XXX     X   operating systems project
X         XX XX         X   Maaz Azhar | Mohammed Salim Sibai | Salem Jabour | Zein Abdul - Hussain
 X                     X 
  X                   X  
   XX               XX   
     XXX         XXX     
        XXXXXXXXX        
        XX XXX XX        
        XX XXX XX        
        XX XXX XX        

*/
#include "main.h"

/* ----------------------- Global Variable Definitions ----------------------- */

Item* urgent_buffer = NULL;
Item* normal_buffer = NULL;
int buffer_size = 0;

int urgent_in  = 0;
int urgent_out = 0;
int urgent_count = 0;

int normal_in  = 0;
int normal_out = 0;
int normal_count = 0;

sem_t empty_slots;
sem_t full_slots;
pthread_mutex_t buffer_mutex;
pthread_mutex_t stats_mutex;

long long total_real_items_consumed = 0;
double total_latency_sec = 0.0;

int num_producers = 0;
int num_consumers = 0;
int items_per_producer = DEFAULT_ITEMS_PER_PRODUCER;

struct timespec program_start_ts;
struct timespec program_end_ts;

/* ----------------------- Main & Setup ----------------------- */

static void print_usage(const char* progname) {
    fprintf(stderr,
        "Usage: %s <num_producers> <num_consumers> <buffer_size> [items_per_producer]\n"
        "  num_producers      > 0\n"
        "  num_consumers      > 0\n"
        "  buffer_size        > 0 (logical capacity of the bounded buffer)\n"
        "  items_per_producer > 0 (default %d)\n\n",
        progname, DEFAULT_ITEMS_PER_PRODUCER);
}

int main(int argc, char* argv[]) {
    if (argc < 4 || argc > 5) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    num_producers = parse_positive_int(argv[1], "num_producers");
    num_consumers = parse_positive_int(argv[2], "num_consumers");
    buffer_size   = parse_positive_int(argv[3], "buffer_size");
    if (argc == 5) {
        items_per_producer = parse_positive_int(argv[4], "items_per_producer");
    }

    int total_real_items = num_producers * items_per_producer;
    printf("Configuration: %d producers, %d consumers, buffer size %d, "
           "%d items/producer (total items = %d)\n",
           num_producers, num_consumers, buffer_size,
           items_per_producer, total_real_items);

    /* Allocate buffers for urgent and normal queues */
    urgent_buffer = (Item*)malloc(sizeof(Item) * buffer_size);
    normal_buffer = (Item*)malloc(sizeof(Item) * buffer_size);
    if (!urgent_buffer || !normal_buffer) {
        perror("malloc(buffer)");
        free(urgent_buffer);
        free(normal_buffer);
        return EXIT_FAILURE;
    }

    /* Initialize semaphores */
    if (sem_init(&empty_slots, 0, buffer_size) != 0) {
        perror("sem_init(empty_slots)");
        free(urgent_buffer);
        free(normal_buffer);
        return EXIT_FAILURE;
    }
    if (sem_init(&full_slots, 0, 0) != 0) {
        perror("sem_init(full_slots)");
        sem_destroy(&empty_slots);
        free(urgent_buffer);
        free(normal_buffer);
        return EXIT_FAILURE;
    }

    /* Initialize mutexes */
    if (pthread_mutex_init(&buffer_mutex, NULL) != 0) {
        perror("pthread_mutex_init(buffer_mutex)");
        sem_destroy(&empty_slots);
        sem_destroy(&full_slots);
        free(urgent_buffer);
        free(normal_buffer);
        return EXIT_FAILURE;
    }
    if (pthread_mutex_init(&stats_mutex, NULL) != 0) {
        perror("pthread_mutex_init(stats_mutex)");
        pthread_mutex_destroy(&buffer_mutex);
        sem_destroy(&empty_slots);
        sem_destroy(&full_slots);
        free(urgent_buffer);
        free(normal_buffer);
        return EXIT_FAILURE;
    }

    /* Thread arrays */
    pthread_t* producers = (pthread_t*)malloc(sizeof(pthread_t) * num_producers);
    pthread_t* consumers = (pthread_t*)malloc(sizeof(pthread_t) * num_consumers);
    ProducerArgs* pargs  = (ProducerArgs*)malloc(sizeof(ProducerArgs) * num_producers);
    ConsumerArgs* cargs  = (ConsumerArgs*)malloc(sizeof(ConsumerArgs) * num_consumers);

    if (!producers || !consumers || !pargs || !cargs) {
        fprintf(stderr, "Failed to allocate thread arrays.\n");
        free(producers);
        free(consumers);
        free(pargs);
        free(cargs);
        pthread_mutex_destroy(&stats_mutex);
        pthread_mutex_destroy(&buffer_mutex);
        sem_destroy(&empty_slots);
        sem_destroy(&full_slots);
        free(urgent_buffer);
        free(normal_buffer);
        return EXIT_FAILURE;
    }

    get_current_time(&program_start_ts);

    /* Create consumer threads first */
    for (int i = 0; i < num_consumers; ++i) {
        cargs[i].id = i + 1;
        int rc = pthread_create(&consumers[i], NULL, consumer_thread, &cargs[i]);
        if (rc != 0) {
            fprintf(stderr, "Error creating consumer thread %d: %s\n",
                    i + 1, strerror(rc));
            return EXIT_FAILURE;
        }
    }

    /* Seed RNG for producers */
    unsigned int global_seed = (unsigned int)time(NULL);

    /* Create producer threads */
    for (int i = 0; i < num_producers; ++i) {
        pargs[i].id = i + 1;
        pargs[i].seed = global_seed ^ (unsigned int)(i * 1234567U);
        int rc = pthread_create(&producers[i], NULL, producer_thread, &pargs[i]);
        if (rc != 0) {
            fprintf(stderr, "Error creating producer thread %d: %s\n",
                    i + 1, strerror(rc));
            return EXIT_FAILURE;
        }
    }

    /* Wait for all producers to finish */
    for (int i = 0; i < num_producers; ++i) {
        int rc = pthread_join(producers[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "Error joining producer thread %d: %s\n",
                    i + 1, strerror(rc));
        }
    }

    printf("All producers finished. Main thread will enqueue %d POISON_PILL items (one per consumer).\n",
           num_consumers);

    /* Enqueue one POISON_PILL per consumer (normal priority) */
    for (int i = 0; i < num_consumers; ++i) {
        Item poison;
        poison.value = POISON_PILL;
        poison.priority = 0;  // poison is treated as normal priority
        get_current_time(&poison.enqueue_ts);
        buffer_put(poison);
        printf("[Main] Enqueued POISON_PILL %d/%d\n", i + 1, num_consumers);
    }

    /* Wait for all consumers to finish */
    for (int i = 0; i < num_consumers; ++i) {
        int rc = pthread_join(consumers[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "Error joining consumer thread %d: %s\n",
                    i + 1, strerror(rc));
        } else {
            printf("[Main] Consumer-%d has terminated.\n", i + 1);
        }
    }

    get_current_time(&program_end_ts);

    double runtime_sec = timespec_diff_sec(&program_start_ts, &program_end_ts);
    double avg_latency = 0.0;
    if (total_real_items_consumed > 0) {
        avg_latency = total_latency_sec / (double)total_real_items_consumed;
    }

    printf("\n===== SUMMARY =====\n");
    printf("Total real items expected: %d\n", total_real_items);
    printf("Total real items consumed: %lld\n", total_real_items_consumed);
    printf("Average latency per item: %.6f seconds\n", avg_latency);
    printf("Total runtime: %.6f seconds\n", runtime_sec);
    if (runtime_sec > 0.0) {
        double throughput = (double)total_real_items_consumed / runtime_sec;
        printf("Throughput: %.2f items/second\n", throughput);
    }
    printf("====================\n");

    /* Cleanup */
    free(producers);
    free(consumers);
    free(pargs);
    free(cargs);

    pthread_mutex_destroy(&stats_mutex);
    pthread_mutex_destroy(&buffer_mutex);
    sem_destroy(&empty_slots);
    sem_destroy(&full_slots);

    free(urgent_buffer);
    free(normal_buffer);

    return EXIT_SUCCESS;
}
