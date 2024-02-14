#include "lwp.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
    if (rr_list_head == NULL) {
        rr_list_head = new;
        current_thread = new; // Ensure current_thread points to the first thread
    } else {
        thread current = rr_list_head;
        while (current->lib_one != NULL) {
            current = current->lib_one;
        }
        current->lib_one = new;
    }
    DEBUG_PRINT("RR Scheduler: Admitted thread %ld\n", new->tid);
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
}

static thread rr_next(void) {
    // Assuming current_thread is always valid and part of rr_list_head
    if (current_thread && current_thread->lib_one) {
        // There's a next thread in the list
        return current_thread->lib_one;
    } else {
        // Reached the end of the list, cycle back to the start
        // This assumes you want to continuously cycle through threads
        return rr_list_head;
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
    // Implementation of starting the LWP system
    DEBUG_PRINT("LWP System: Started\n");
    
    // Transform the calling thread into a LWP
    current_thread = malloc(sizeof(context)); // Allocate memory for the thread context
    if (!current_thread) {
        perror("LWP Start: Failed to allocate thread");
        return;
    }
    current_thread->tid = next_tid++; // Assign a thread ID
    current_thread->stack = NULL; // Leave the stack NULL for the calling thread
    current_thread->state.rsp = 0; // Reset the stack pointer for the calling thread
    
    // Use the current scheduler's admit function
    if (current_scheduler && current_scheduler->admit) {
        current_scheduler->admit(current_thread);
    } else {
        // Handle the case where current_scheduler is not set up
        DEBUG_PRINT("LWP Start: No current scheduler set. Initial thread %ld not admitted.\n", current_thread->tid);
    }
    
    // Yield to the next thread
    lwp_yield();
}


tid_t lwp_create(lwpfun function_pointer, void *argument) {
    // Allocate memory for the thread context
    thread new_thread = malloc(sizeof(context));
    if (!new_thread) {
        perror("LWP Create: Failed to allocate thread");
        return NO_THREAD;
    }

    // Allocate memory for the stack
    new_thread->stack = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (new_thread->stack == MAP_FAILED) {
        free(new_thread);
        perror("LWP Create: Failed to allocate stack");
        return NO_THREAD;
    }

    // Initialize stack size and other thread properties
    new_thread->stacksize = 4096;
    new_thread->lib_one = NULL; // Correctly initialize the next pointer
    new_thread->tid = next_tid++;

    // Initialize the thread context and stack frame
    memset(&(new_thread->state), 0, sizeof(new_thread->state));
    unsigned long *stack_ptr = (unsigned long*)new_thread->stack + new_thread->stacksize / sizeof(unsigned long) - 1;
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
    DEBUG_PRINT("LWP Yield: Next thread %ld\n", next_thread->tid);
    if (next_thread == NULL) {
        DEBUG_PRINT("LWP Yield: No next thread, system might hang or exit\n");
        exit(EXIT_SUCCESS);
    }
    DEBUG_PRINT("LWP Yield: Switching to Thread %ld\n", next_thread->tid);

    thread prev_thread = current_thread;
    current_thread = next_thread; // Update the current_thread global variable

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