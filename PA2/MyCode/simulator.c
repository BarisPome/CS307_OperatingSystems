// Barış Pome - CS307 - Operating Systems Course - Fall 2024-2025
// This is simulator.c file

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include "constants.h"
#include "wbq.h"

extern int stop_threads;
extern int finished_jobs[NUM_CORES];
extern WorkBalancerQueue **processor_queues;

// Dynamic Watermark Calculation
// Performance considerations:
// - Adaptive load balancing: Adjusts thresholds based on system state
// - System-wide optimization: Considers all queue states
// - Trade-off: Additional overhead for better load distribution

// Calculate watermarks based on the current state of all queues
// The reason why i implemented this in the simulator is because i want to have the ability to change the watermarks
// based on the current state of the queues. This causes use maximum number of cores and arrangement of the queues.
// This causes use maximum number of cores and arrangement of the queues.
void calculateWatermarks(int my_id, int *high_watermark, int *low_watermark)
{
    int total_tasks = 0;
    int max_size = 0;

    // System-wide load analysis
    // - O(n) operation where n is number of cores
    // - Provides global load perspective
    for (int i = 0; i < NUM_CORES; i++)
    {
        int current_size = getQueueSize(processor_queues[i]);
        total_tasks += current_size;
        if (current_size > max_size)
        {
            max_size = current_size;
        }
    }

    // Dynamic threshold calculation
    // - Adapts to current system load
    // - Prevents unnecessary work stealing
    // - Optimizes resource utilization
    // Calculate average load
    float avg_load = (float)total_tasks / NUM_CORES;
    // Set watermarks based on system state
    *high_watermark = (int)(avg_load * 1.5); // 50% more than average
    *low_watermark = (int)(avg_load * 0.5);  // 50% less than average

    // Minimum threshold enforcement
    // - Ensures basic load balancing even with light loads
    // - Prevents excessive stealing for small workloads
    if (*high_watermark < 4)
        *high_watermark = 4;
    if (*low_watermark < 2)
        *low_watermark = 2;
}

// Main Thread Processing Loop
// Key features:
// - Cache affinity: Prioritizes local queue processing
// - Load balancing: Work stealing when local queue is under-utilized
// - Adaptive behavior: Uses dynamic watermarks
// - Thread-safe operations: Handles shared data with synchronization

// This function is the main function that each thread will execute.
// It will fetch tasks from its own queue or from other queues if the current queue is below the low watermark.
// It will then execute the task and reinsert it back into the queue if it is not complete.
// It will also sleep for a short time before reinserting the task to allow other tasks to progress.
void *processJobs(void *arg)
{
    ThreadArguments *my_arg = (ThreadArguments *)arg;
    WorkBalancerQueue *my_queue = my_arg->q;
    int my_id = my_arg->id;

    while (!stop_threads)
    {
        Task *task = NULL;
        int queue_size = getQueueSize(my_queue);

        // Dynamic load balancing thresholds
        // - Adapts to current system state
        // - Balances work distribution vs cache efficiency

        int high_watermark, low_watermark;
        calculateWatermarks(my_id, &high_watermark, &low_watermark);

        // Work stealing logic
        // Cache trade-off: Accepts potential cache misses for better load distribution
        // - Accepts cache misses for better load balance
        // - Balances between cache efficiency and load distribution
        if (queue_size < low_watermark)
        {
            // Steal attempt loop
            // - Checks other queues for excess work
            // - Prioritizes heavily loaded queues
            for (int i = 0; i < NUM_CORES; i++)
            {
                if (i != my_id)
                {
                    int other_size = getQueueSize(processor_queues[i]);
                    if (other_size > high_watermark)
                    {
                        task = fetchTaskFromOthers(processor_queues[i]);
                        if (task != NULL)
                            break;
                    }
                }
            }
        }

        // Local queue processing
        // - Maintains cache affinity
        // - Reduces inter-core communication

        // If no task was stolen (or queue was not low), try own queue
        if (task == NULL)
        {
            task = fetchTask(my_queue);
        }

        // Task execution and management
        // - Includes cache warm-up considerations
        // - Handles task lifecycle

        if (task != NULL)
        {
            // Execute the task
            executeJob(task, my_queue, my_id);

            // Task resubmission logic
            // - Implements task continuity
            // - Maintains workload distribution
            if (task->task_duration > 0)
            {
                // Small delay to prevent monopolization
                // - Allows other tasks to progress
                // - Improves overall fairness
                usleep(100);
                submitTask(my_queue, task);
            }
            else
            {
                // Clean up completed tasks
                free(task->task_id);
                free(task);
            }
        }
        else
        {
            // Backoff strategy when no work is available
            // - Reduces contention
            // - Saves CPU cycles
            usleep(1000);
        }
    }

    free(my_arg);
    pthread_exit(NULL);
}

// System Initialization
// Setup considerations:
// - Resource allocation: Per-core queue initialization
// - Performance tracking: Sets up monitoring structures
// - System-wide state management

// This function initializes the shared variables for the simulator.
// It will initialize the processor queues and the finished jobs counter for each core.
void initSharedVariables()
{
    // Initialize per-core queues
    // - Distributed queue structure for better cache locality
    // - Independent synchronization domains
    for (int i = 0; i < NUM_CORES; i++)
    {
        processor_queues[i] = malloc(sizeof(WorkBalancerQueue));
        WorkBalancerQueue_Init(processor_queues[i]); // Initialize WBQ for each core
        finished_jobs[i] = 0;                        // Reset finished jobs counter
    }
}
