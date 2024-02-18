#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include "lwp.h"
#include "fp.h"

#define DEBUG_PRINT(...)                     \
    do {                                     \
        printf(__VA_ARGS__);                 \
        fflush(stdout);                      \
    } while (0)

#define load_context(c) (swap_rfiles(NULL,c))
#define save_context(c) (swap_rfiles(c,NULL))

static thread thread_list = NULL;
static thread current_thread = NULL;

static thread rr_list_head = NULL;

// Round-Robin Scheduler Functions

// Admit a new thread into the round-robin list
static void rr_admit(thread new_thread) {

    DEBUG_PRINT("Admitting thread %ld\n", new_thread->tid);
    if (rr_list_head == NULL) {
        rr_list_head = new_thread;
        new_thread->lib_one = new_thread; // Points to itself, forming a circular list
    } else {
        // Insert the new thread before the head to avoid traversing the list
        new_thread->lib_one = rr_list_head->lib_one;
        rr_list_head->lib_one = new_thread;
        rr_list_head = new_thread; // Optional: Update the head to keep the newest thread at the front
    }
}

// Remove a thread from the round-robin list
static void rr_remove(thread victim) {
    if (rr_list_head == NULL || victim == NULL) {
        return; // No action if the list is empty or victim is NULL
    }

    thread prev = rr_list_head;
    do {
        if (prev->lib_one == victim) {
            prev->lib_one = victim->lib_one;
            if (victim == rr_list_head) { // If removing the head, update it to the next thread
                rr_list_head = (victim->lib_one == victim) ? NULL : victim->lib_one;
            }
            victim->lib_one = NULL; // Clear to prevent accidental reuse
            return;
        }
        prev = prev->lib_one;
    } while (prev != rr_list_head);

    // If victim is not found, the function simply returns without altering the list
}

// Select the next thread to run from the round-robin list
// Select the next thread to run from the round-robin list
static thread rr_next(void) {
    if (rr_list_head == NULL) {
        return NULL; // Return NULL if the list is empty
    }
    if (rr_list_head->lib_one == rr_list_head) {
        // If the list contains only one thread, return it without changing the head
        return rr_list_head;
    }

    // Rotate the head to simulate round-robin behavior for more than one thread
    rr_list_head = rr_list_head->lib_one;
    return rr_list_head;
}


// Return the number of threads in the round-robin list
static int rr_qlen(void) {
    int count = 0;
    if (rr_list_head != NULL) {
        thread current = rr_list_head;
        do {
            count++;
            current = current->lib_one;
        } while (current != rr_list_head);
    }
    return count;
}

// Define the Round-Robin Scheduler structure
static struct scheduler rr_scheduler = {
    NULL, 
    NULL,
    rr_admit,
    rr_remove,
    rr_next,
    rr_qlen
};

static scheduler current_scheduler = &rr_scheduler;
int thread_count = 0;
context initial_thread;

static void lwp_wrap(lwpfun fun, void *arg) {
    int rval = fun(arg); // Execute the provided function with arguments
    lwp_exit(rval); // Exit the LWP with the function's return value
}

/**
 * Creates a new lightweight process (LWP).
 * 
 * @param func The function to be executed by the LWP.
 * @param arg The argument to be passed to the function.
 * @return The thread ID of the new LWP, or -1 on failure.
 */
tid_t lwp_create(lwpfun func, void *arg) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        perror("sysconf failed to get page size");
        return (tid_t)-1;
    }

    if (!current_scheduler) {
        lwp_set_scheduler(&rr_scheduler);
        current_scheduler->init();
    }

    thread new_thread = malloc(sizeof(context));
    if (!new_thread) {
        perror("Failed to allocate memory for new thread");
        return (tid_t)-1;
    }

    struct rlimit rlim;
    size_t stack_size = 8 * 1024 * 1024; // Default stack size to 8 MB
    if (getrlimit(RLIMIT_STACK, &rlim) == 0 && rlim.rlim_cur != RLIM_INFINITY) {
        stack_size = rlim.rlim_cur;
    }

    // Ensure the stack size is a multiple of the page size
    stack_size = (stack_size + page_size - 1) / page_size * page_size;

    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("Failed to allocate stack for new thread");
        free(new_thread);
        return (tid_t)-1;
    }

    // Initialize the new thread's context and stack
    new_thread->stacksize = stack_size;
    new_thread->stack = (unsigned long *)((char *)stack + stack_size); // Point to the top of the stack
    new_thread->tid = ++thread_count;
    
    // Before placing lwp_wrap on the stack, align the stack pointer
    unsigned long *stack_top = (unsigned long *)((char *)stack + stack_size);
    stack_top = (unsigned long *)((uintptr_t)stack_top & ~0xF); // Align stack pointer to 16 bytes
    *(--stack_top) = (unsigned long)lwp_wrap; // lwp_wrap address
    *(--stack_top) = 0; // Placeholder for return address

    new_thread->state.rsp = (unsigned long)stack_top;
    new_thread->state.rbp = (unsigned long)stack_top;
    new_thread->state.rdi = (unsigned long)func;
    new_thread->state.rsi = (unsigned long)arg;
    new_thread->state.fxsave = FPU_INIT;
    new_thread->status = LWP_LIVE;

    // Admit the new thread to the scheduler
    current_scheduler->admit(new_thread);

    return new_thread->tid;
}

/**
 * Initializes and starts the LWP system, admitting an initial thread.
 */

void lwp_start(void) {
    // Initialize the initial thread context
    initial_thread.tid = 0;
    initial_thread.stack = NULL;
    initial_thread.status = LWP_LIVE;
    initial_thread.stacksize = 0;
    initial_thread.state.fxsave = FPU_INIT;
    current_thread = &initial_thread;
    current_scheduler->admit(&initial_thread);
    lwp_yield();
}


/**
 * Waits for a thread to terminate and cleans up resources.
 * 
 * @param status Pointer to store the exit status of the terminated thread. Can be NULL.
 * @return The thread ID of the terminated thread, or NO_THREAD if no terminable threads are found.
 */
tid_t lwp_wait(int *status) {
    // Loop until a terminated thread is found or no more runnable threads exist.
    while (true) {
        DEBUG_PRINT("Waiting for a terminated thread\n");

        thread prev = NULL;
        thread current = current_thread;
        
        while (current != NULL && current_scheduler->qlen() > 1) {
            DEBUG_PRINT("Checking thread %ld\n", current->tid);

            // Check if the thread is marked for termination and is not the initial thread.
            if (LWPTERMINATED(current->status) && current->tid != 0) {
                DEBUG_PRINT("Thread %ld is terminated\n", current->tid);

                // Remove the terminated thread from the list.
                if (prev) {
                    prev->lib_one = current->lib_one;
                } else {
                    current_thread = current->lib_one; // Adjust the head if the first thread is removed.
                }
                
                tid_t terminated_tid = current->tid; // Save the terminated thread's ID.
                // Save the terminated thread's exit status if requested.
                if (status) {
                    *status = LWPTERMSTAT(current->status);
                }
                
                // Deallocate the thread's resources.
                if (current->stack) {
                    munmap(current->stack, current->stacksize);
                }
                free(current);
                
                return terminated_tid; // Return the ID of the terminated thread.
            }
            
            prev = current;
            current = current->lib_one; // Move to the next thread in the list.
        }
        
        // If only the initial thread is left, exit the loop.
        if (current_scheduler->qlen() <= 1) {
            break;
        }
        
        lwp_yield(); // Yield to other threads, waiting for one to terminate.
    }
    
    return NO_THREAD; // Return NO_THREAD if no terminable threads are found.
}

/**
 * Yields execution from the current thread to the next thread in the scheduler.
 * If no next thread is found, the program will print an error message and exit.
 */
void lwp_yield(void) {

    thread next_thread = current_scheduler->next();
    if (current_scheduler->qlen() > 1) {
        while (next_thread->tid == 0) {
            DEBUG_PRINT("Skipping initial thread\n");
            next_thread = current_scheduler->next();
        } 
    }
    DEBUG_PRINT("Yielding from thread %ld to thread %ld\n", current_thread->tid, next_thread->tid);
    thread prev_thread = current_thread;
    current_thread = next_thread;
    swap_rfiles(&(prev_thread->state), &(next_thread->state));
}

/**
 * Terminates the current thread and removes it from the scheduler.
 * If no more threads are runnable, exits the program successfully.
 * 
 * @param status The exit status to associate with the terminating thread.
 */
void lwp_exit(int status) {
    DEBUG_PRINT("Thread %ld exiting with status %d\n", current_thread->tid, status);
    current_thread->status = MKTERMSTAT(LWP_TERM, status);

    current_scheduler->remove(current_thread);
    if (current_thread->tid == 0) {
        // Last thread has finished; switch back to the initial thread
        DEBUG_PRINT("Exiting initial thread\n");
    } else {
        // Yield to the next thread if there are more threads
        lwp_yield();
    }
    // If the initial_thread calls lwp_exit(), simply return to avoid recursion
}

/**
 * Sets the current scheduler to the provided scheduler function and initializes it.
 * 
 * @param sched The scheduler function to set as the current scheduler.
 */
void lwp_set_scheduler(scheduler sched) {
    current_scheduler = sched; // Set the current scheduler
    current_scheduler->init(); // Initialize the newly set scheduler
}

/**
 * Retrieves the current scheduler.
 * 
 * @return The current scheduler function.
 */
scheduler lwp_get_scheduler(void) {
    return current_scheduler; // Return the current scheduler
}

thread tid2thread(tid_t tid) {
    // Search for thread by tid
    thread t = thread_list;
    while (t) {
        if (t->tid == tid) {
            return t;
        }
        t = t->lib_one;
    }
    DEBUG_PRINT("LWP tid2thread: Thread %ld not found\n", tid);
    return NULL;
}
