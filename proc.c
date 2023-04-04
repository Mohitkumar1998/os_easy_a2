#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int utf_edf = 0;
int utf_rm = 0;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority = 1;
  p->deadline = 0;
  p->sched_policy = -1;
  p->execution_time = 1;
  p->elapsed_time = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p, *p1;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    struct proc *highP;
    int policy_to_use = -1;
    // uint ticks_consumed;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      highP = p;
      policy_to_use = p->sched_policy;
      if (policy_to_use == 0) {
        for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
          if(p1->state != RUNNABLE)
            continue;
          if(p1->deadline <= highP->deadline){
            if(p1->deadline == highP->deadline) {
              if (p1->pid < highP ->pid) {
                highP = p1;
              }
            } else {
              highP = p1;
            }
          }
        }
        // cprintf("Process chosen is %d\n", highP->pid);
        highP->elapsed_time += 1;
        // cprintf("Process Execution Time: %d\n", highP->execution_time);
      } else if (policy_to_use == 1) {
        for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
          if(p1->state != RUNNABLE)
            continue;
          if(p1->priority <= highP->priority){
            if(p1->priority == highP->priority) {
              if (p1->pid < highP ->pid) {
                highP = p1;
              }
            } else {
              highP = p1;
            }
          }
        }
        // cprintf("************Process Pid - %d, Process rate - %d, Process Priority - %d\n", highP->pid, highP->rate, highP->priority);
        // cprintf("Process chosen is %d\n", highP->pid);
        highP->elapsed_time += 1;
        // cprintf("Process Execution Time: %d\n", highP->execution_time);
      }
      p = highP;
      // ticks_consumed = ticks;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      // ticks_consumed = ticks - ticks_consumed;
      // p->execution_time = p->execution_time - 1;
      // if(p->execution_time <= 0) {
      //   p->state = SLEEPING;
      // }
      // cprintf("PID - %d, Exec Time - %d, State - %s", p->pid, p->execution_time, p->state);
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// 
int
printinfo(void)
{
  struct proc *p;
  sti();

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING)
      cprintf("Process State - SLEEPING, Process Name - %s, Process Id - %d, Policy - %d, Exec time - %d, Deadline - %d\n",p->name,p->pid,p->sched_policy, p->execution_time, p->deadline);
    else if(p->state == RUNNING)
      cprintf("Process State - RUNNING, Process Name - %s, Process Id - %d, Policy - %d, Exec time - %d, Deadline - %d\n",p->name,p->pid,p->sched_policy, p->execution_time, p->deadline);
    else if(p->state == RUNNABLE)
      cprintf("Process State - RUNNABLE, Process Name - %s, Process Id - %d, Policy - %d, Exec time - %d, Deadline - %d\n",p->name,p->pid,p->sched_policy, p->execution_time, p->deadline);
  }
  release(&ptable.lock);
  // utf_edf++;
  return 22;
}

int 
sched_policy(int pid, int policy)
{
  // add code for schedulability checks and change policy
  // if not pass return -22
  struct proc *p;
  int check = 0;
  int lproc = pid-2;
  sti();

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      if (policy == 0) {
        utf_edf += (p->execution_time * 100)/p->deadline;
        if (utf_edf >= 100) {
          utf_edf -= (p->execution_time*100)/p->deadline;
          p->killed = 1;
          p->state = ZOMBIE;
          check = 1;

        } else {
          p->sched_policy = policy;
          check = 0;
        }
      }
      if (policy == 1) {
        int curr_utf_rm = p->execution_time * p->rate * 10;
        int tempChk = utf_rm + curr_utf_rm;
        int chk_lproc = 0;
        // cprintf("lproc - %d, pid - %d\n", lproc, p->pid);
        if (lproc == 1) {
          chk_lproc = 1000;
        } else if (lproc == 2) {
          chk_lproc = 828;
        } else if (lproc == 3) {
          chk_lproc = 779;
        } else if (lproc == 4) {
          chk_lproc = 756;
        } else if (lproc == 5) {
          chk_lproc = 743;
        } else if (lproc == 6) {
          chk_lproc = 734;
        } else if (lproc == 7) {
          chk_lproc = 728;
        } else if (lproc == 8) {
          chk_lproc = 724;
        } else if (lproc == 9) {
          chk_lproc = 720;
        } else if (lproc == 10) {
          chk_lproc = 717;
        } else if (lproc == 11) {
          chk_lproc = 715;
        } else if (lproc == 12) {
          chk_lproc = 713;
        } else if (lproc == 13) {
          chk_lproc = 711;
        } else if (lproc == 14) {
          chk_lproc = 710;
        } else if (lproc == 15) {
          chk_lproc = 709;
        } else if (lproc == 16) {
          chk_lproc = 708;
        } else if (lproc == 17) {
          chk_lproc = 707;
        } else if (lproc == 18) {
          chk_lproc = 706;
        } else if (lproc == 19) {
          chk_lproc = 705;
        } else if (lproc == 20) {
          chk_lproc = 705;
        } else if (lproc == 21) {
          chk_lproc = 704;
        } else if (lproc == 22) {
          chk_lproc = 704;
        } else if (lproc == 23) {
          chk_lproc = 703;
        } else if (lproc == 24) {
          chk_lproc = 703;
        } else if (lproc == 25) {
          chk_lproc = 702;
        } else if (lproc == 26) {
          chk_lproc = 702;
        } else if (lproc == 27) {
          chk_lproc = 702;
        } else if (lproc == 28) {
          chk_lproc = 701;
        } else if (lproc == 29) {
          chk_lproc = 701;
        } else if (lproc == 30) {
          chk_lproc = 701;
        } else if (lproc == 31) {
          chk_lproc = 700;
        } else if (lproc == 32) {
          chk_lproc = 700;
        } else if (lproc == 33) {
          chk_lproc = 700;
        } else if (lproc == 34) {
          chk_lproc = 700;
        } else if (lproc == 35) {
          chk_lproc = 700;
        } else if (lproc == 36) {
          chk_lproc = 699;
        } else if (lproc == 37) {
          chk_lproc = 699;
        } else if (lproc == 38) {
          chk_lproc = 699;
        } else if (lproc == 39) {
          chk_lproc = 699;
        } else if (lproc == 40) {
          chk_lproc = 699;
        } else if (lproc == 41) {
          chk_lproc = 699;
        } else if (lproc == 42) {
          chk_lproc = 698;
        } else if (lproc == 43) {
          chk_lproc = 698;
        } else if (lproc == 44) {
          chk_lproc = 698;
        } else if (lproc == 45) {
          chk_lproc = 698;
        } else if (lproc == 46) {
          chk_lproc = 698;
        } else if (lproc == 47) {
          chk_lproc = 698;
        } else if (lproc == 48) {
          chk_lproc = 698;
        } else if (lproc == 49) {
          chk_lproc = 698;
        } else if (lproc == 50) {
          chk_lproc = 697;
        } else if (lproc == 51) {
          chk_lproc = 697;
        } else if (lproc == 52) {
          chk_lproc = 697;
        } else if (lproc == 53) {
          chk_lproc = 697;
        } else if (lproc == 54) {
          chk_lproc = 697;
        } else if (lproc == 55) {
          chk_lproc = 697;
        } else if (lproc == 56) {
          chk_lproc = 697;
        } else if (lproc == 57) {
          chk_lproc = 697;
        } else if (lproc == 58) {
          chk_lproc = 697;
        } else if (lproc == 59) {
          chk_lproc = 697;
        } else if (lproc == 60) {
          chk_lproc = 697;
        } else if (lproc == 61) {
          chk_lproc = 697;
        } else if (lproc == 62) {
          chk_lproc = 697;
        } else if (lproc == 63) {
          chk_lproc = 696;
        } else {
          chk_lproc = 696;
        }
        if (tempChk <= chk_lproc) {
          utf_rm += curr_utf_rm;
          p->arrival_time = ticks;
          p->sched_policy = policy;
          check = 0;
        } else {
          // cprintf("Pid - %d, tempchk - %d, check_lproc - %d", p->pid, tempChk, chk_lproc);
          p->killed = 1;
          p->state = ZOMBIE;
          check = 1;
        }
      }
      break;
    }
  }
  release(&ptable.lock);
  if (check > 0)
    return -22;
  
  return 0;
}

int 
exec_time(int pid, int time)
{
  struct proc *p;
  sti();
  int found = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      found = 1;
      p->execution_time = time;
      break;
    }
  }
  release(&ptable.lock);
  if (found == 0)
    return -22;

  return 0;
}

int 
deadline(int pid, int deadline)
{
  struct proc *p;
  sti();
  int found = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      found = 1;
      p->deadline = deadline;
      break;
    }
  }
  release(&ptable.lock);
  if (found == 0)
    return -22;

  return 0;
}

int 
rate(int pid, int rate)
{
  struct proc *p;
  sti();
  int found = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      found = 1;
      p->rate = rate;
      int priority_temp = (90 - 3 * rate + 28)/29;
      if (priority_temp < 1) {
        p->priority = 1;
      } else {
        p->priority = priority_temp;
      }
      break;
    }
  }
  release(&ptable.lock);
  if (found == 0)
    return -22;
  
  return 0;
}