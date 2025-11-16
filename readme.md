# Producer-Consumer Problem in C (using Pthreads)

This project is a C implementation of the classic   Producer-Consumer problem   using multithreading. It demonstrates fundamental concurrency concepts using   pthreads  ,   semaphores  , and   mutexes   to manage a shared bounded buffer.

The implementation closely follows standard textbook pseudocode while incorporating adjustments to handle concurrency requirements and resource management.

## Key Features & Design

    Bounded Circular Buffer:   Implemented using a dynamically allocated array, allowing the buffer size to be set via a command-line argument.
    Correct Buffer Access:   Uses modulo arithmetic for buffer indexing to ensure correct wrap-around logic and prevent overwriting:
      `in = (in + 1) % buffer_size`
      `out = (out + 1) % buffer_size`
    Semaphore Synchronization:   Utilizes two POSIX semaphores (`sem_t`) to manage the state of the buffer:
      `empty_slots`: Counts the number of available empty slots (initialized to buffer size).
      `full_slots`: Counts the number of filled slots (initialized to 0).
    Mutual Exclusion:   Employs a `pthread_mutex_t` to ensure that only one thread can access the shared buffer and its associated indices (`in`, `out`) at a time, preventing race conditions.
    Thread Logic:  
        Producers:   Each producer thread is designed to generate 20 items.
        Consumers:   Consumer threads run in a loop, consuming one item per iteration until all items are consumed.

## Challenges Addressed

This project successfully navigated several common concurrency challenges:

1.    Graceful Termination:  
        Challenge:   Ensuring all producer threads, consumer threads, mutexes, and semaphores were properly terminated and released without errors.
        Solution:   Manually tracked all resources (`sem_destroy`, `pthread_mutex_destroy`) through each code block to guarantee complete and clean resource deallocation upon program completion.

2.    Synchronization & Deadlock Avoidance:  
        Challenge:   Preventing potential deadlocks or race conditions.
        Solution:   Carefully implemented a locking and signaling order using `pthread_mutex_lock` for mutual exclusion and `sem_wait` / `sem_post` to block threads when the buffer was full (for producers) or empty (for consumers).

3.    Consumer Termination Logic:  
        Challenge:   A potential scenario where `sem_post(&full_slots)` could wake a consumer thread  after  the buffer was empty, leading to unnecessary work or errors.
        Solution:   This was determined to be a harmless condition. The woken consumer will acquire the mutex, check the global `consumed_count`, see that no items remain, and exit gracefully. This ensures all consumers terminate correctly once all items are processed.