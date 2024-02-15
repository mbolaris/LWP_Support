#include "lwp.h"
#include <sys/resource.h> // For getrlimit, struct rlimit, RLIMIT_STACK, and RLIM_INFINITY
#include <sys/mman.h>     // For mmap and munmap
#include <unistd.h>       // For sysconf
#include <stdlib.h>       // For malloc, free
#include <stdio.h>        // For perror
#include <string.h>       // For memset
#include <stdbool.h> // Add this for true




#define LWP_TERM_MASK 0x01 // Assuming LSB indicates termination
#define LWP_STATUS_MASK 0xFFFFFFFE // Mask to extract the exit status from the rest of the bits

// Example definitions, adjust according to your actual status representation
#define ISTERM(status) ((status) & LWP_TERM_MASK) // Adjust LWP_TERM_MASK to your needs
#define GETSTATUS(status) ((status) & LWP_STATUS_MASK) // Adjust LWP_STATUS_MASK to your needs

static thread current_thread = NULL;
static thread thread_list = NULL; // Linked list of threads
static tid_t next_tid = 1;

// Debug Logging Utility
#define DEBUG_PRINT(...) \
    do { printf(__VA_ARGS__); fflush(stdout); } while (0)

// Default Round-Robin Scheduler Functions
static thread rr_list_head = NULL;


void log_linked_list(void) {
    if (rr_list_head == NULL) {
        DEBUG_PRINT("Linked List: Empty\n");
        return;
    }

    thread temp = rr_list_head;
    DEBUG_PRINT("Linked List Start: ");
    do {
        DEBUG_PRINT("Thread %ld -> ", temp->tid);
        temp = temp->lib_one;
    } while (temp != rr_list_head);
    DEBUG_PRINT("Back to Head Thread %ld\n", rr_list_head->tid);
}


static void rr_init(void) {
    DEBUG_PRINT("RR Scheduler: Initialized\n");
    rr_list_head = NULL; // Initialize the list head
}

static void rr_shutdown(void) {
    DEBUG_PRINT("RR Scheduler: Shutdown\n");
    // Clean up the thread list if necessary
    thread current = rr_list_head;
    while (current != NULL) {
        thread next = current->lib_one;
        free(current);
        current = next;
    }
    rr_list_head = NULL;
}

static void rr_admit(thread new) {
    new->lib_one = NULL;  // Initially, no next thread.
    if (rr_list_head == NULL) {
        rr_list_head = new;
        new->lib_one = new;  // Points to itself, forming a circular list.
        current_thread = new;  // Set the current thread to the new thread if it's the first.
    } else {
        thread last = rr_list_head;
        while (last->lib_one != rr_list_head) {  // Traverse to find the last thread.
            last = last->lib_one;
        }
        last->lib_one = new;  // Link the new thread to the last.
        new->lib_one = rr_list_head;  // Complete the circle by linking back to the head.
    }
    DEBUG_PRINT("RR Scheduler: Correctly admitted thread %ld, ensuring a continuous circular list.\n", new->tid);
    log_linked_list();  // To check the list after admission.
}



static void rr_remove(thread victim) {
    DEBUG_PRINT("RR Scheduler: Removed thread %ld\n", victim->tid);
    // Remove the victim from the list
    if (rr_list_head == victim) {
        rr_list_head = victim->lib_one;
    } else {
        thread current = rr_list_head;
        while (current != NULL && current->lib_one != victim) {
            current = current->lib_one;
        }
        if (current != NULL) {
            current->lib_one = victim->lib_one;
        }
    }
    log_linked_list();
}

static thread rr_next(void) {
    if (current_thread == NULL || current_thread->lib_one == current_thread) {
        // Case where only one thread exists or no current thread is set
        DEBUG_PRINT("RR Next: Insufficient threads for switching.\n");
        return NULL;
    } else {
        thread next_thread = current_thread->lib_one;
        if (next_thread == rr_list_head) {
            DEBUG_PRINT("RR Next: Cycling back to the head, Thread %ld.\n", next_thread->tid);
        } else {
            DEBUG_PRINT("RR Next: Switching to Thread %ld.\n", next_thread->tid);
        }
        return next_thread;
    }
}




static int rr_qlen(void) {
    int count = 0;
    thread current = rr_list_head;
    while (current != NULL) {
        count++;
        current = current->lib_one;
    }
    return count;
}

// Round-Robin Scheduler structure
static struct scheduler round_robin = {
    rr_init, rr_shutdown, rr_admit, rr_remove, rr_next, rr_qlen
};

static scheduler current_scheduler = &round_robin;

void lwp_start(void) {
    DEBUG_PRINT("LWP System: Starting\n");
    if (!current_scheduler) {
        lwp_set_scheduler(&round_robin);
    }
    current_scheduler->init();
    DEBUG_PRINT("LWP System: Started\n");
    
    // Allocate memory for the initial thread context but do not admit it to the scheduler.
    thread initial_thread = malloc(sizeof(context));
    if (!initial_thread) {
        perror("LWP Start: Failed to allocate initial thread");
        return;
    }
    initial_thread->tid = 0;  // Optionally assign a special ID or handle for the initial thread.
    initial_thread->stack = NULL;  // Initial thread uses the main stack.
    initial_thread->state.rsp = 0;  // Reset the stack pointer for the initial thread.
    
    // Do not admit the initial thread to the round-robin scheduler.
    // This decision is based on the assumption that the initial thread's role does not require round-robin scheduling.

    // Directly yield to user-created threads.
    lwp_yield();
}



tid_t lwp_create(lwpfun function_pointer, void *argument) {
    // Determine page size
    long page_size = sysconf(_SC_PAGE_SIZE);
    
    // Determine stack size
    struct rlimit rlim;
    size_t stack_size;
    if (getrlimit(RLIMIT_STACK, &rlim) == 0 && rlim.rlim_cur != RLIM_INFINITY) {
        stack_size = rlim.rlim_cur;
    } else {
        stack_size = 8 * 1024 * 1024; // Default to 8 MB
    }
    // Ensure stack size is a multiple of page size
    stack_size = (stack_size + page_size - 1) / page_size * page_size;

    // Log the stack size being used
    DEBUG_PRINT("LWP Create: Using stack size of %zu bytes\n", stack_size);

    // Allocate memory for the thread context
    thread new_thread = malloc(sizeof(context));
    if (!new_thread) {
        perror("LWP Create: Failed to allocate thread");
        return NO_THREAD;
    }

    // Allocate memory for the stack using mmap with MAP_STACK flag
    new_thread->stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (new_thread->stack == MAP_FAILED) {
        free(new_thread);
        perror("LWP Create: Failed to allocate stack");
        return NO_THREAD;
    }

    // Initialize stack size and other thread properties
    new_thread->stacksize = stack_size;
    new_thread->lib_one = NULL; 
    new_thread->tid = next_tid++;

    // Initialize the thread context and stack frame
    memset(&(new_thread->state), 0, sizeof(new_thread->state));
    unsigned long *stack_ptr = (unsigned long *)new_thread->stack + stack_size / sizeof(unsigned long) - 1;
    *--stack_ptr = (unsigned long)lwp_exit; // Setup for lwp_exit
    *--stack_ptr = (unsigned long)argument; // Argument for the thread function
    *--stack_ptr = (unsigned long)function_pointer; // Thread function pointer
    new_thread->state.rsp = (unsigned long)stack_ptr;

    // Admit the new thread to the scheduler
    if (current_scheduler && current_scheduler->admit) {
        current_scheduler->admit(new_thread);
    } else {
        DEBUG_PRINT("LWP Create: No current scheduler set. Thread %ld not admitted.\n", new_thread->tid);
    }

    DEBUG_PRINT("LWP Create: Thread %ld created and admitted\n", new_thread->tid);
    return new_thread->tid;
}


void lwp_exit(int status) {
    DEBUG_PRINT("LWP Exit: Thread %ld exiting with status %d\n", current_thread->tid, status);
    
    // Mark the thread as terminated and store the exit status
    current_thread->status = MKTERMSTAT(LWP_TERM, status & 0xFF);

    // Remove the thread from the scheduler's list
    current_scheduler->remove(current_thread);

    // Check if there are any threads left to run
    if (current_scheduler->qlen() == 0) {
        // No more threads to run, exit the LWP system
        DEBUG_PRINT("No more threads to run, exiting LWP system.\n");
        exit(EXIT_SUCCESS);
    } else {
        // Yield control to the next thread
        lwp_yield();
    }
}

void lwp_yield(void) {
    DEBUG_PRINT("LWP Yield: Current Thread %ld yielding\n", current_thread ? current_thread->tid : -1);
    thread next_thread = current_scheduler->next();
    if (!next_thread || next_thread == current_thread) {
        DEBUG_PRINT("LWP Yield: No next thread or only one thread in the system, might hang or exit\n");
        return; // Consider handling this case more gracefully
    }
    DEBUG_PRINT("LWP Yield: Switching from Thread %ld to Thread %ld\n", current_thread->tid, next_thread->tid);

    thread prev_thread = current_thread;
    current_thread = next_thread; // Update the current_thread global variable

    // Assuming swap_rfiles is your context switching function
    // Perform the context switch
    swap_rfiles(&(prev_thread->state), &(current_thread->state));
}


tid_t lwp_wait(int *status) {
    DEBUG_PRINT("LWP Wait: Checking for terminated threads\n");

    while (true) {
        bool found_terminated_thread = false;
        thread prev = NULL;
        thread temp = thread_list;
        tid_t terminated_tid = NO_THREAD;

        // Iterate through the thread list to find a terminated thread
        while (temp != NULL) {
            if (ISTERM(temp->status)) { // Assuming ISTERM checks if the thread is terminated
                found_terminated_thread = true;
                if (prev != NULL) {
                    prev->lib_one = temp->lib_one; // Remove the terminated thread from the list
                } else {
                    thread_list = temp->lib_one; // If the first thread in the list is terminated
                }

                terminated_tid = temp->tid;
                if (status != NULL) {
                    *status = GETSTATUS(temp->status); // Assuming GETSTATUS extracts the exit status
                }

                // Deallocate thread resources, except for the original system thread
                if (temp->stack != NULL) {
                    munmap(temp->stack, temp->stacksize); // Assuming stack was allocated with mmap
                }
                free(temp);
                break; // Exit the loop after dealing with the first found terminated thread
            }
            prev = temp;
            temp = temp->lib_one;
        }

        if (found_terminated_thread) {
            return terminated_tid;
        }

        // If no terminated threads found and no other threads are runnable, return NO_THREAD
        if (current_scheduler->qlen() <= 1) {
            return NO_THREAD;
        }

        // If no terminated threads found, yield to potentially allow other threads to finish
        lwp_yield();
    }
}

void lwp_set_scheduler(scheduler fun) {
    current_scheduler = fun;
    DEBUG_PRINT("LWP Scheduler: Set to %p\n", (void*)fun);
}

scheduler lwp_get_scheduler(void) {
    return current_scheduler;
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