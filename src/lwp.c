#include "lwp.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
/* double linked list of all the threads*/
thread LWP_list = NULL;
thread current_thread = NULL; // current thread pointer

int thread_count = 1;

/* static struct scheduler rr_publish = {NULL, NULL};

/**
 * @param func thread to run
 * @param arg the arguments of the function
 * @param stack_size size of the stack to be created
 * 
*/
tid_t lwp_create(lwpfun func, void *arg){
    unsigned long *stack;
    thread new_thread 
    
    new_thread = malloc(sizeof(context));

    if(!new_thread){
        perror("lwp_create");
        return (tid_t)-1;
    }
    // Allocate stack using mmap
    size_t stack_size = allocate_stack(stack_size);
    //allocate size for the stack base 1mb
    tmp->stacksize = stack_size

    //set id
    tmp->tid = thread_count++;


    return tmp->tid;
    
}

void* allocate_stack(size_t stack_size) {
    // Ensure the stack size is aligned to the page size
    long page_size = sysconf(_SC_PAGESIZE);
    stack_size = (stack_size + page_size - 1) / page_size * page_size; // Round up to nearest page size
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap");
        return NULL; // mmap failed
    }

    return stack;
}


void lwp_yield(void){
    thread tmp;

    tmp = current_thread;


}

