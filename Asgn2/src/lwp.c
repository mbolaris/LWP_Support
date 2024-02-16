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

void detailed_context_log(const char* prefix, const thread th) {
    if (!th) {
        printf("%s: Thread is NULL\n", prefix);
        return;
    }
    
    // Basic thread information
    printf("%s: Thread ID: %ld, Stack Base: %p, Stack Size: %zu\n",
           prefix, th->tid, th->stack, th->stacksize);

    // Register information
    printf("%s: Registers:\n", prefix);
    printf(" RAX: %lu, RBX: %lu, RCX: %lu, RDX: %lu\n", th->state.rax, th->state.rbx, th->state.rcx, th->state.rdx);
    printf(" RSI: %lu, RDI: %lu, RBP: %lu, RSP: %lu\n", th->state.rsi, th->state.rdi, th->state.rbp, th->state.rsp);
    printf(" R8: %lu, R9: %lu, R10: %lu, R11: %lu\n", th->state.r8, th->state.r9, th->state.r10, th->state.r11);
    printf(" R12: %lu, R13: %lu, R14: %lu, R15: %lu\n", th->state.r12, th->state.r13, th->state.r14, th->state.r15);

    // Floating-point state (if applicable)
    // printf("%s: Floating Point State: [Add specific logging here]\n", prefix);

    // Instruction pointer (if available)
    // printf("%s: RIP: %lu\n", prefix, th->state.rip);

    // Stack pointer check (ensure it's within the stack bounds)
    uintptr_t sp = (uintptr_t)th->state.rsp;
    if (sp < (uintptr_t)th->stack || sp >= ((uintptr_t)th->stack + th->stacksize)) {
        printf("%s: Warning: Stack pointer is outside of stack bounds!\n", prefix);
    }

    // Add any additional context-specific checks or logs here
}



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
    if (rr_list_head == NULL) {
        rr_list_head = new;
        new->lib_one = new; // Points to itself, forming a circular list
    } else {
        // Insert the new thread before the head to maintain the list as circular without traversing it
        thread tail = rr_list_head->lib_one;
        rr_list_head->lib_one = new;
        new->lib_one = tail;
    }
    DEBUG_PRINT("RR Scheduler: Admitted thread %ld\n", new->tid);
    log_linked_list();
}

static void rr_remove(thread victim) {
    if (rr_list_head == NULL || victim == NULL) {
        DEBUG_PRINT("RR Scheduler: Remove called with empty list or null victim.\n");
        return;
    }
    DEBUG_PRINT("RR Scheduler: Removing thread %ld\n", victim->tid);

    // Special case: single-element list
    if (victim == rr_list_head && victim->lib_one == victim) {
        rr_list_head = NULL;
    } else {
        thread prev = rr_list_head;
        while (prev->lib_one != victim && prev->lib_one != rr_list_head) {
            prev = prev->lib_one;
        }

        if (prev->lib_one == victim) {
            prev->lib_one = victim->lib_one;
            // Adjust rr_list_head if necessary
            if (rr_list_head == victim) {
                rr_list_head = victim->lib_one;
            }
        } else {
            DEBUG_PRINT("RR Scheduler: Victim not found in the list.\n");
            return;
        }
    }
    victim->lib_one = NULL; // Clear to help with debugging and prevent accidental reuse
    log_linked_list();
}


static thread rr_next(void) {
    DEBUG_PRINT("Entering rr_next. rr_list_head: %p, current_thread: %ld\n", 
                (void*)rr_list_head, (current_thread ? current_thread->tid : -1));

    // Check if the list is empty or contains only one thread, no switch needed
    if (rr_list_head == NULL || rr_list_head->lib_one == rr_list_head) {
        DEBUG_PRINT("RR Next: Insufficient threads for switching. Returning NULL.\n");
        return NULL;
    }

    // Ensure current_thread is valid and part of the round-robin list
    if (current_thread == NULL || current_thread == rr_list_head || current_thread->lib_one == NULL) {
        DEBUG_PRINT("RR Next: Current thread is invalid or not part of the list, defaulting to rr_list_head.\n");
        current_thread = rr_list_head;
    }

    thread next_thread = current_thread->lib_one;
    DEBUG_PRINT("RR Next: Switching to Thread %ld.\n", next_thread->tid);

    return next_thread;
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

static tid_t original_system_thread_id = 0;
static scheduler current_scheduler = &round_robin;

void lwp_start(void) {
    DEBUG_PRINT("LWP Start: Initializing LWP System. Setting up main thread.\n");
    if (!current_scheduler) {
        lwp_set_scheduler(&round_robin);
    }

    DEBUG_PRINT("LWP System: Started\n");
    // Initialize the main thread but do not admit to scheduler
    current_thread = malloc(sizeof(context));
    if (!current_thread) {
        perror("LWP Start: Failed to allocate main thread");
        return;
    }
    current_thread->tid = next_tid++;
    current_thread->stack = NULL; // Use the process's original stack
    current_thread->state.rsp = 0; // Not applicable for main thread
    original_system_thread_id = current_thread->tid;

    // Directly yield to the next thread without admitting the main thread
    DEBUG_PRINT("LWP Start: Main thread initialized with tid %ld.\n", current_thread->tid);
    lwp_yield();
}

static void lwp_wrap(lwpfun fun, void *arg) {
    int rval;
    rval = fun(arg);
    lwp_exit(rval);
}


void setup_thread_start_state(thread new_thread, lwpfun function_pointer, void *argument) {
    // Ensure the stack is aligned to a 16-byte boundary
    unsigned long *stack_top = (unsigned long *)((char *)new_thread->stack + new_thread->stacksize);
    // Align the stack pointer to 16 bytes. This is crucial for x86-64 ABI compliance.
    stack_top = (unsigned long *)((uintptr_t)stack_top & ~(uintptr_t)0xF);

    // The x86-64 ABI requires the stack pointer (RSP) to be 16-byte aligned before any CALL instruction.
    // Since we are simulating a call by manually modifying the stack, we need to ensure this alignment.
    // The CALL instruction would normally push the return address (next instruction's address) onto the stack,
    // thereby making RSP 8-byte aligned at the start of the called function. To maintain this convention,
    // we manually align the stack to 16 bytes here.

    // Adjust for "fake" return address space. This space simulates the address that would be on the stack
    // if a call instruction were used to start the thread function.
    stack_top--;

    // Push the argument for the thread function
    *(--stack_top) = (unsigned long)argument;

    // Push the address of the thread function as a "fake" return address.
    // This is where execution will begin when this thread is switched to.
    // For simplicity, we assume `lwp_wrap` is being used for all threads.
    *(--stack_top) = (unsigned long)lwp_wrap;

    // Push a fake return address to simulate a call to `lwp_wrap`.
    // This can be the address of a function that calls exit or an infinite loop as a safety measure.
    // For demonstration, we'll just repeat the address of `lwp_wrap`, but in a real system, you might
    // want a function that gracefully shuts down the thread if it ever "returns".
    *(--stack_top) = (unsigned long)lwp_exit; // Use lwp_exit as a placeholder.

    // Initialize the thread's registers, especially the stack pointer
    memset(&(new_thread->state), 0, sizeof(new_thread->state));
    new_thread->state.rsp = (unsigned long)stack_top;

    // Debug logging
    DEBUG_PRINT("Setup thread start state: Thread ID: %ld, Stack Base: %p, Stack Size: %zu, RSP: %lu\n",
                new_thread->tid, new_thread->stack, new_thread->stacksize, new_thread->state.rsp);
}




tid_t lwp_create(lwpfun function_pointer, void *argument) {
    long page_size = sysconf(_SC_PAGESIZE); // Get system page size for stack alignment
    struct rlimit rlim;
    size_t stack_size = 8 * 1024 * 1024; // Default stack size to 8 MB

    // Try to get current stack limit and adjust stack size accordingly
    if (getrlimit(RLIMIT_STACK, &rlim) == 0 && rlim.rlim_cur != RLIM_INFINITY) {
        stack_size = rlim.rlim_cur;
    }
    // Align stack size to page size boundary
    stack_size = (stack_size + page_size - 1) & ~(page_size - 1);

    // Allocate memory for new thread context
    thread new_thread = malloc(sizeof(struct threadinfo_st));
    if (!new_thread) {
        perror("Failed to allocate memory for new thread");
        return NO_THREAD;
    }

    // Allocate stack for new thread
    new_thread->stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (new_thread->stack == MAP_FAILED) {
        free(new_thread);
        perror("Failed to allocate stack");
        return NO_THREAD;
    }

    new_thread->stacksize = stack_size;
    new_thread->tid = next_tid++;

    // Prepare the stack and register state for the new thread
    setup_thread_start_state(new_thread, function_pointer, argument);

    // Admit new thread to the scheduler
    if (current_scheduler && current_scheduler->admit) {
        current_scheduler->admit(new_thread);
    } else {
        DEBUG_PRINT("No current scheduler set. Thread %ld not admitted.\n", new_thread->tid);
    }

    DEBUG_PRINT("Thread %ld created and admitted with stack base: %p, rsp: %lu\n", new_thread->tid, new_thread->stack, new_thread->state.rsp);

    return new_thread->tid;
}


void lwp_exit(int status) {
    DEBUG_PRINT("Entering lwp_exit. Exiting thread: %ld with status: %d\n", current_thread->tid, status);
    current_thread->status = (status & ~LWP_TERM_MASK) | LWP_TERM_MASK;
    current_scheduler->remove(current_thread);
    DEBUG_PRINT("LWP Exit: Thread %ld marked for removal\n", current_thread->tid);
    if (current_thread->stack) {
        munmap(current_thread->stack, current_thread->stacksize); // Free the thread's stack
    }
    free(current_thread); // Free the thread's context
    lwp_yield();
    DEBUG_PRINT("LWP Exit: No more threads, exiting program.\n");
    exit(EXIT_SUCCESS);
}


// Validation function to check thread context

bool validate_thread_context(thread th) {
    if (!th) {
        DEBUG_PRINT("Validation Error: Thread is NULL\n");
        return false;
    }

    if ((uintptr_t)th->state.rsp < (uintptr_t)th->stack || (uintptr_t)th->state.rsp >= ((uintptr_t)th->stack + th->stacksize)) {
        DEBUG_PRINT("Validation Error: Thread %ld's RSP (0x%lx) is out of stack bounds (Base: 0x%lx, Size: %zu)\n",
                    th->tid, (unsigned long)th->state.rsp, (unsigned long)th->stack, th->stacksize);
        return false;
    }

    return true;
}

void lwp_yield(void) {
    DEBUG_PRINT("Entering lwp_yield. Current thread: %ld\n", (current_thread ? current_thread->tid : -1));
    log_linked_list();

    if (rr_list_head == NULL) {
        DEBUG_PRINT("LWP Yield: No threads to yield to. Returning early.\n");
        return;
    }

    DEBUG_PRINT("lwp_yield: Before calling rr_next.\n");
    thread next_thread = current_scheduler->next();
    DEBUG_PRINT("lwp_yield: After rr_next. Next thread: %ld\n", (next_thread ? next_thread->tid : -1));

    if (next_thread) {
        // Debugging: Print information before swapping
        DEBUG_PRINT("Before swap: Current thread: %ld, Next thread: %ld\n", current_thread->tid, next_thread->tid);
        DEBUG_PRINT("Current thread rsp: %p, Next thread rsp: %p\n", (void*)current_thread->state.rsp, (void*)next_thread->state.rsp);

        // Validate stack pointers before swapping
        if (!validate_thread_context(current_thread) || !validate_thread_context(next_thread)) {
            DEBUG_PRINT("Error: Thread context validation failed. Aborting swap.\n");
            return; // Abort swap to prevent segfault
        }

        // Debugging: Print detailed context before swapping
        detailed_context_log("Pre-swap, current context", current_thread);
        detailed_context_log("Pre-swap, next context", next_thread);

        // Perform the context switch
        swap_rfiles(&(current_thread->state), &(next_thread->state));

        // Debugging: Print information after swapping
        DEBUG_PRINT("After swap: Current thread: %ld, New rsp: %p\n", current_thread->tid, (void*)current_thread->state.rsp);
    } else {
        DEBUG_PRINT("No next thread available. Returning.\n");
    }

    DEBUG_PRINT("Exiting lwp_yield\n");
}







tid_t lwp_wait(int *status) {
    DEBUG_PRINT("LWP Wait: Checking for terminated threads\n");

    thread prev = NULL;
    tid_t terminated_tid = NO_THREAD;

    // Block until a terminated thread is found or no more runnable threads
    while (true) {
        thread temp = thread_list;
        while (temp != NULL) {
            if (ISTERM(temp->status) && temp->tid != original_system_thread_id) {
                if (prev) {
                    prev->lib_one = temp->lib_one;
                } else {
                    thread_list = temp->lib_one; // Adjust head if first thread is removed
                }

                terminated_tid = temp->tid;
                if (status) {
                    *status = GETSTATUS(temp->status);
                }

                // Safe to deallocate resources
                if (temp->stack) {
                    munmap(temp->stack, temp->stacksize);
                }
                free(temp);
                return terminated_tid; // Return the tid of the terminated thread
            }
            prev = temp;
            temp = temp->lib_one;
        }

        if (current_scheduler->qlen() <= 1) {
            // No more threads to wait for
            return NO_THREAD;
        }

        // Yield to other threads, waiting for one to terminate
        lwp_yield();
    }
}

void lwp_set_scheduler(scheduler fun) {
    current_scheduler = fun;
    current_scheduler->init();    
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