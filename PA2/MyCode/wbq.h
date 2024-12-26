// Barış Pome - CS307 - Operating Systems Course - Fall 2024-2025
// This is header file wbq.h

#ifndef WBQ_H
#define WBQ_H

#include <stdatomic.h>
#include "constants.h"
#include <stdbool.h>
#include <pthread.h>

// Queue structure optimized for:
// 1. Cache efficiency: Through local queue priority
// 2. Synchronization: Using atomic operations and mutex locks
// 3. Load balancing: Atomic counter for O(1) size checks

// This struct is used to create the queue.
typedef struct WorkBalancerQueue WorkBalancerQueue;

// This struct is used to pass the queue and the id of the thread to the processJobs function.
typedef struct ThreadArguments
{
    WorkBalancerQueue *q;
    int id;
} ThreadArguments;

// This struct is used to pass the task to the executeJob function.
typedef struct Task
{
    char *task_id;
    int task_duration;
    double cache_warmed_up;
    WorkBalancerQueue *owner;
} Task;

// This struct is used to create the nodes of the queue.
typedef struct QueueNode
{
    Task *task;
    _Atomic(struct QueueNode *) next;
} QueueNode;

// This struct is used to create the queue.
typedef struct WorkBalancerQueue
{
    _Atomic(QueueNode *) head; // Atomic pointer for thread-safe access
    _Atomic(QueueNode *) tail; // Atomic pointer for thread-safe access
    _Atomic int count;         // O(1) size tracking for load balancing decisions
    pthread_mutex_t lock;      // Coarse-grained lock for complex operations
} WorkBalancerQueue;

// Function declarations with performance characteristics:

// Submit Task Function
// This function is used to submit a task to the queue.
// submitTask: O(1) operation
// - Cache friendly: Tasks initially stay in their original queue
// - Synchronization: Uses mutex + atomic counter
void submitTask(WorkBalancerQueue *q, Task *_task);

// Fetch Task Function
// This function is used to fetch a task from the queue.
// fetchTask: O(1) operation
// - Cache optimized: Prioritizes local queue access
// - Maintains data locality
Task *fetchTask(WorkBalancerQueue *q);

// Fetch Task From Others Function
// This function is used to fetch a task from other queues.
// fetchTaskFromOthers: Work stealing implementation
// - Load balancing: Steals tasks when local queue is empty
// - Cache trade-off: May cause cache misses but improves load distribution
Task *fetchTaskFromOthers(WorkBalancerQueue *q);

// Initialize Queue Function
// This function is used to initialize the queue.
void WorkBalancerQueue_Init(WorkBalancerQueue *q);

// Get Queue Size Function
// This function is used to get the size of the queue.
int getQueueSize(WorkBalancerQueue *q);

// Execute Job Function
// This function is used to execute a job.
void executeJob(Task *task, WorkBalancerQueue *my_queue, int my_id);

// Process Jobs Function
// This function is used to process the jobs.
void *processJobs(void *arg);

// Initialize Shared Variables Function
// This function is used to initialize the shared variables.
void initSharedVariables();

#endif
