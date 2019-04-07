#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <synch.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include <array.h>
#include "opt-A2.h"
#include <test.h>
#include <vfs.h>
#include <limits.h>
#include <opt-A3.h>


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  // (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  #if OPT_A2
  p->dead = true;
  if (exitcode == __WSIGNALED) {
    p->exitCode = _MKWAIT_SIG( 0 );
  }
  else p->exitCode = _MKWAIT_EXIT(exitcode);

  // signal a possibly waiting parent
  lock_acquire(p->cv_lock);
  cv_broadcast(p->p_cv, p->cv_lock);
  lock_release(p->cv_lock);

  // No parent
  if(p->parent == NULL) {
    // Delete all zombie children, then destroy itself
    
    proc_destroy(p);
  } else { // if it has a parent, just set it to dead
    p->dead = true;
  }

  #else
  proc_destroy(p);
  #endif
  
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

#if OPT_A2

int sys_fork(struct trapframe *tf, pid_t *retval) {
  // Step 1: create process structure for child
  struct proc *parentProc = curproc;
  struct proc *childProc = proc_create_runprogram(parentProc->p_name);
  if (childProc == NULL) {
    DEBUG(DB_SYSCALL, "Could not create proc structure for child");
    return ENOMEM; 
  }
  // step 2, create and copy addr space
  spinlock_acquire(&childProc->p_lock);
  as_copy(curproc_getas(), &(childProc->p_addrspace));
  spinlock_release(&childProc->p_lock);
  if (childProc->p_addrspace == NULL) {
    proc_destroy(childProc);
    DEBUG(DB_SYSCALL, "Could not create new addr space for child!");
    return ENOMEM;
  }
  // step 3, assign PID to child process
  // done automatically in proc.c when proc_create is called
  // create parent-child relationship
  lock_acquire(parentProc->cv_lock);
  childProc->parent = parentProc;
  array_add(parentProc->children, childProc, NULL);
  lock_release(parentProc->cv_lock);

  // step 4: create thread for child process
  // first create a copy of the parent tf on the heap
  struct trapframe *tfCopy = kmalloc(sizeof(struct trapframe));
  if (tfCopy == NULL) {
    DEBUG(DB_SYSCALL, "could not create new trapframe for child process");
    proc_destroy(childProc);
    return ENOMEM;
  }
  memcpy(tfCopy, tf, sizeof(struct trapframe));
  // now call thread_fork
  // int pass = thread_fork(curthread->t_name, childProc, enter_forked_process, tfCopy, 0);
  thread_fork(curthread->t_name, childProc, enter_forked_process, tfCopy, 0);
  // if (pass) {
  //   proc_destroy(childProc);
  //   kfree(tfCopy);
  //   tfCopy = NULL;
  //   return ENOMEM;
  // }

  *retval = childProc->pid;
  return 0;
}

#endif

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A2
  KASSERT(curproc != NULL);
  *retval = curproc->pid;
  #else
  *retval = 1;
  #endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  #ifdef OPT_A2
  struct proc *child = NULL;
  for(unsigned int i = 0; i < array_num(curproc->children); i++) {
    struct proc *thisChild = array_get(curproc->children, i);
    if(thisChild->pid == pid) {
      child = thisChild;
    }
  }

  if(child == NULL) {
    *retval = -1;
    return ECHILD;
  }
  // else the child is valid and found, now check if dead, if not, cv_wait
  lock_acquire(child->cv_lock);
  while(child->dead == false) {
    cv_wait(child->p_cv, child->cv_lock);
  }
  lock_release(child->cv_lock);

  exitstatus = child->exitCode;

  #else 
  exitstatus = 0;
  #endif
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#ifdef OPT_A2
int sys_execv(userptr_t program, userptr_t params) {
  // Extract the progname
  size_t prognameLen = strlen((char *)program) + 1;
  size_t bytes = 0;
  const int maxArgs = 64;

  // Count number of args
  int numArgs = 0;

   for(;((userptr_t *)params)[numArgs] != NULL; numArgs++) {
    bytes += strlen(((char **)params)[numArgs]) + 1; // + 1 for '\0'
  }
  bytes += prognameLen;

  // total bytes cannot exceed ARG_MAX from limits.h
  if (numArgs > maxArgs || bytes > ARG_MAX) return E2BIG;

  // kprintf("%d\n", numArgs);

  size_t realLen;
  int which = 0;
  char tempArgs[bytes];

  // Copy args into kernel
  char *args[numArgs];
  for (int i = 0; i < numArgs; i++) {
    size_t len = strlen(((char **)params)[i]) + 1;

    int test = copyinstr(((userptr_t *)params)[i], (char *)(tempArgs + which), len, &realLen);
    if (test) return test;

    args[i] = tempArgs + which;
    which += realLen;  
  }

  char progName[255];
  int test = copyinstr(program, progName, 255, NULL);
  if (test) return test;

  test = runprogram(progName, args, numArgs);

  return test;
}
#endif
