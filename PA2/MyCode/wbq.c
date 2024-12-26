// Barış Pome - CS307 - Operating Systems Course - Fall 2024-2025
// This is wbq.c file

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include "wbq.h"

int flag_array[NUM_CORES];

// Initialize queue with thread-safe properties
// - Sets up atomic pointers and counter
// - Initializes synchronization primitives
void WorkBalancerQueue_Init(WorkBalancerQueue *q)
{
    atomic_store(&q->head, NULL);
    atomic_store(&q->tail, NULL);
    atomic_store(&q->count, 0);
    pthread_mutex_init(&q->lock, NULL);
}

// Task submission with cache and thread safety considerations
// Performance characteristics:
// - Cache affinity: Tasks start in their original queue
// - Synchronization: Mutex + atomic operations
// - Load tracking: Atomic counter increment
void submitTask(WorkBalancerQueue *q, Task *_task)
{
    QueueNode *node = malloc(sizeof(QueueNode));
    node->task = _task;
    atomic_store(&node->next, NULL);

    pthread_mutex_lock(&q->lock);
    if (atomic_load(&q->tail) == NULL) // If the queue is empty
    {
        atomic_store(&q->head, node); // Set the head to the new node
        atomic_store(&q->tail, node); // Set the tail to the new node
    }
    else
    {
        atomic_store(&atomic_load(&q->tail)->next, node); // Set the next of the tail to the new node
        atomic_store(&q->tail, node);                     // Set the tail to the new node
    }
    atomic_fetch_add(&q->count, 1); // Increment the count of the queue
    pthread_mutex_unlock(&q->lock); // Unlock the mutex
}

// Local task fetching optimized for cache efficiency
// - Prioritizes local queue access
// - Maintains data locality
// - Thread-safe task removal
Task *fetchTask(WorkBalancerQueue *q)
{
    pthread_mutex_lock(&q->lock);
    QueueNode *node = atomic_load(&q->head); // Load the head of the queue
    Task *task = NULL;

    if (node != NULL)
    {
        task = node->task;
        atomic_store(&q->head, atomic_load(&node->next)); // Set the head to the next node
        if (atomic_load(&q->head) == NULL)
        {
            atomic_store(&q->tail, NULL);
        }
        free(node);
        atomic_fetch_sub(&q->count, 1); // Decrement the count of the queue
    }

    pthread_mutex_unlock(&q->lock);
    return task;
}

// Work stealing implementation for load balancing
// Design considerations:
// - Load Distribution: Steals from middle of queue
// - Cache Impact: Accepts cache misses for better load balance
// - Queue Preservation: Leaves minimum tasks in source queue
Task *fetchTaskFromOthers(WorkBalancerQueue *q)
{
    pthread_mutex_lock(&q->lock);

    if (atomic_load(&q->count) <= 1) // If the queue has only one task or is empty
    {                                // Leave at least one task
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    QueueNode *first = atomic_load(&q->head);
    QueueNode *second = atomic_load(&first->next);

    if (second == NULL) // If the second node is NULL
    {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    Task *task = second->task;
    atomic_store(&first->next, atomic_load(&second->next)); // Set the next of the first node to the next of the second node

    if (second == atomic_load(&q->tail)) // If the second node is the tail of the queue
    {
        atomic_store(&q->tail, first);
    }

    atomic_fetch_sub(&q->count, 1); // Decrement the count of the queue
    free(second);                   // Free the second node

    pthread_mutex_unlock(&q->lock);
    return task;
}

// O(1) queue size check for load balancing decisions
// - Lock-free implementation
// - Used for work stealing decisions
// - Enables quick load balancing checks
int getQueueSize(WorkBalancerQueue *q)
{
    return atomic_load(&q->count); // Return the count of the queue
}