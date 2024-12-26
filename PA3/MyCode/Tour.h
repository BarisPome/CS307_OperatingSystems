// Barış Pome - baris.pome - 31311
// 2024-2025 Fall Operating Systems - PA3
// Tour.h

#ifndef TOUR_H
#define TOUR_H

#include <pthread.h>
#include <semaphore.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

using namespace std;

class Tour
{
private:
    // Private members:

    // Maximum group size
    int max_group_size;

    // Current group size
    int current_gropu_size;

    // Indicates if the tour has a guide
    bool has_guide;

    // Indicates if the tour is currently active
    bool is_tour_active;

    // Barrier for synchronization at the end of the tour
    pthread_barrier_t _tour_barrier;

    // Mutex for state synchronization
    pthread_mutex_t _state_mutex;

    // Mutex for logging synchronization
    pthread_mutex_t _log_mutex;

    // Thread ID of the guide
    pthread_t guide_thread;

    // Semaphores for entry and guide synchronization
    sem_t _entry_sem;
    sem_t _guide_sem;

    // Validates constructor arguments: group size must be positive, has_guide must be 0 or 1
    void argument_check(int group_size, int has_guide)
    {
        if (group_size <= 0 || (has_guide != 0 && has_guide != 1))
        {
            throw invalid_argument("An error occurred.");
        }
    }

    // Prints thread-safe log messages with thread ID and status
    void print_info(const std::string &message, pthread_t tid)
    {
        pthread_mutex_lock(&_log_mutex);
        cout << "Thread ID: " << tid << " | Status: " << message << endl;
        pthread_mutex_unlock(&_log_mutex);
    }

    // Checks if the given thread ID belongs to the tour guide
    bool is_guide(pthread_t tid)
    {
        return has_guide && pthread_equal(tid, guide_thread);
    }

    // Handles the case when group is complete: marks tour as active and sets guide if needed
    void handle_group_complete(pthread_t tid)
    {
        is_tour_active = true;
        if (has_guide)
        {
            guide_thread = tid;
        }
        print_info("There are enough visitors, the tour is starting.", tid);
    }

    // Resets tour state after completion: marks tour as inactive and allows new visitors
    void reset_tour_state()
    {
        is_tour_active = false;
        sem_post(&_entry_sem);
    }

    // Processes a new visitor entry: increments group size and handles full/partial group cases
    void process_visitor_entry()
    {
        current_gropu_size++;
        // If the group is complete, handle the group completion
        if (current_gropu_size == max_group_size)
        {
            handle_group_complete(pthread_self());
        }
        else
        {
            handle_partial_group();
        }
    }

    // Handles case when group is not yet full: allows next visitor to enter
    void handle_partial_group()
    {
        print_info("Only " + to_string(current_gropu_size) + " visitors inside, starting solo shots.", pthread_self());
        sem_post(&_entry_sem);
    }

    // Handles visitor leaving early (before tour starts): decrements group size and manages entry semaphore
    void handle_early_leave()
    {
        print_info("My camera ran out of memory while waiting, I am leaving.", pthread_self());
        current_gropu_size--;
        if (current_gropu_size == 0)
        {
            sem_post(&_entry_sem);
        }
    }

    // Manages guide departure and visitor synchronization at tour end:
    // - Guide signals completion
    // - Visitors wait for guide's signal before departing
    void handle_guide_departure()
    {
        if (is_guide(pthread_self()))
        {
            print_info("Tour guide speaking, the tour is over.", pthread_self());
            sem_post(&_guide_sem);
        }
        else
        {
            pthread_mutex_unlock(&_state_mutex);
            sem_wait(&_guide_sem);
            sem_post(&_guide_sem);
            pthread_mutex_lock(&_state_mutex);
        }
    }

    // Handles visitor departure after tour:
    // - Decrements group size
    // - Resets tour state when last visitor leaves
    void handle_visitor_departure()
    {
        if (!is_guide(pthread_self()))
        {
            print_info("I am a visitor and I am leaving.", pthread_self());
        }
        current_gropu_size--;

        if (current_gropu_size == 0)
        {
            print_info("All visitors have left, the new visitors can come.", pthread_self());
            reset_tour_state();
        }
    }

public:
    // Starts the tour: only declared but not implemented
    void start();

    // Constructor: initializes tour state and semaphores
    Tour(int group_size, int has_guide_param)
    {
        // Validate arguments
        argument_check(group_size, has_guide_param);

        // Initialize tour state
        max_group_size = group_size;
        has_guide = has_guide_param;
        current_gropu_size = 0;
        is_tour_active = false;

        // If the tour has a guide, increment the maximum group size
        if (has_guide)
        {
            max_group_size++;
        }

        // Initialize barrier and semaphores
        pthread_barrier_init(&_tour_barrier, nullptr, max_group_size);
        sem_init(&_entry_sem, 0, 1);
        sem_init(&_guide_sem, 0, 0);

        // Initialize mutexes
        pthread_mutex_init(&_state_mutex, nullptr);
        pthread_mutex_init(&_log_mutex, nullptr);
    }

    // Handles visitor arrival: prints log message, waits for entry permission, and processes entry
    void arrive()
    {
        // Get the thread ID of the current thread
        pthread_t tid = pthread_self();

        // Print log message
        print_info("Arrived at the location.", tid);

        // Wait for entry permission
        sem_wait(&_entry_sem);

        // Lock the state mutex and process the visitor entry
        pthread_mutex_lock(&_state_mutex);
        process_visitor_entry();
        pthread_mutex_unlock(&_state_mutex);
    }

    // Handles visitor departure: checks if in tour, handles early leave, waits for guide, and handles departure
    void leave()
    {
        // Get the thread ID of the current thread
        pthread_t tid = pthread_self();

        // Lock the state mutex and check if the visitor is in the tour
        pthread_mutex_lock(&_state_mutex);
        bool in_tour = is_tour_active;

        // If the visitor is not in the tour, handle early leave
        if (!in_tour)
        {
            handle_early_leave();
            pthread_mutex_unlock(&_state_mutex);
            return;
        }

        // Unlock the state mutex and wait for the tour barrier
        pthread_mutex_unlock(&_state_mutex);

        // Wait for the tour barrier
        pthread_barrier_wait(&_tour_barrier);

        // Lock the state mutex and handle guide departure if needed
        pthread_mutex_lock(&_state_mutex);
        if (has_guide)
        {
            handle_guide_departure();
        }

        // Handle visitor departure
        handle_visitor_departure();

        // Unlock the state mutex
        pthread_mutex_unlock(&_state_mutex);
    }

    // Destructor: destroys barrier, mutexes, and semaphores
    ~Tour()
    {
        pthread_barrier_destroy(&_tour_barrier);

        pthread_mutex_destroy(&_state_mutex);
        pthread_mutex_destroy(&_log_mutex);

        sem_destroy(&_entry_sem);
        sem_destroy(&_guide_sem);
    }
};

#endif // TOUR_H