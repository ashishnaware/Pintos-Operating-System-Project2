#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/kernel/list.h"
#include "threads/thread.h"

void syscall_init (void);

struct thread_children{
  tid_t child_id;
  struct list_elem child_elem;
  struct thread *child_thread;
};

#endif /* userprog/syscall.h */
