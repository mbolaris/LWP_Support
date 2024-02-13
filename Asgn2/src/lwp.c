#include "lwp.h"
/* double linked list of all the threads*/
thread LWP_list = NULL;
thread current_thread = NULL; // current thread pointer

int thread_count = 1;

static struct scheduler rr_publish = {NULL, NULL, rr_admit, rr_remove, rr_next};

/**
 * @param func thread to run
 * @param arg the arguments of the function
 * @param stack_size size of the stack to be created
 * 
*/
tid_t lwp_create(lwpfun func, void *arg, size_t stack_size){
    thread tmp;
    unsigned long *stack;

    tmp = malloc(sizeof(context));
    if(!tmp){
        perror("lwp_create");
        return (tid_t)-1;
    }
    //get the size of the new thread in bytes
    tmp->stacksize = stack_size * sizeof(unsigned long);
    //set id
    tmp->tid = thread_count++;

    
}

void lwp_yield(void){
    thread tmp;

    tmp = current_thread;
}

