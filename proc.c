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

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);


float CFS_weights[40] = {88716, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
                         9548, 7620, 6100, 4904, 3906, 3121, 2501, 1991, 1586, 1277, 
                         1024, 820, 655, 526, 423, 335, 272, 215, 172, 137,
                          110, 87, 70, 56, 45, 36, 29, 23, 18, 15};


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
  p->value = 20; //Default priority value of process is 20
  p->vruntime = 0;
  p->runtime = 0;
  // p->vruntime = (int)(1000 * (1024/CFS_weights[p->value])+0.5);
  // p->time_slice = (int)(10000 * (1024 / CFS_weights[p->value])+0.5);
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

// Own System Calls
int
getpname(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
        cprintf("%s\n", p->name);
        release(&ptable.lock);
        return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
getnice(int pid)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p=ptable.proc; p<&ptable.proc[NPROC];p++){
    if(p->pid == pid){
      cprintf("%d\n",p->value);
      release(&ptable.lock);
      return p->value;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
setnice(int pid, int value)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p=ptable.proc; p<&ptable.proc[NPROC];p++){
    if(p->pid == pid){
      p->value = value;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void
ps(int pid)
{
  struct proc *p;
  //Enables interrupts on this processor.
  sti();

  acquire(&ptable.lock);
  cprintf("name\t\tpid\tstate\t\tpriority\truntime/weight\truntime\t\tvruntime\ttick %d\n",ticks);
  if (pid == 0){
    for(p=ptable.proc; p<&ptable.proc[NPROC]; p++){
      int rw = (int)(p->runtime/CFS_weights[p->value]+0.5);
      if(p->state == SLEEPING)
        cprintf("%s\t\t%d\t%s\t%d\t\t%d\t\t%d\t\t%d\n",p->name,p->pid,"SLEEPING",p->value,rw,p->runtime,p->vruntime);
      else if(p->state == RUNNING)
        cprintf("%s\t\t%d\t%s\t\t%d\t\t%d\t\t%d\t\t%d\n",p->name,p->pid,"RUNNING",p->value,rw,p->runtime,p->vruntime);
      else if(p->state == RUNNABLE)
        cprintf("%s\t\t%d\t%s\t%d\t\t%d\t\t%d\t\t%d\n",p->name,p->pid,"RUNNABLE",p->value,rw,p->runtime,p->vruntime);
    }
  }
  else{
    for(p=ptable.proc; p<&ptable.proc[NPROC]; p++){
      int rw = (int)(p->runtime/CFS_weights[p->value]+0.5);
      if(p->pid == pid){
        if(p->state == SLEEPING)
          cprintf("%s\t\t%d\t%s\t%d\t\t%d\t\t%d\t\t%d\n",p->name,p->pid,"SLEEPING",p->value,rw,p->runtime,p->vruntime);
        else if(p->state == RUNNING)
          cprintf("%s\t\t%d\t%s\t\t%d\t\t%d\t\t%d\t\t%d\n",p->name,p->pid,"RUNNING",p->value,rw,p->runtime,p->vruntime);
        else if(p->state == RUNNABLE)
          cprintf("%s\t\t%d\t%s\t%d\t\t%d\t\t%d\t\t%d\n",p->name,p->pid,"RUNNABLE",p->value,rw,p->runtime,p->vruntime);
        break;
      }
    }
  }
  release(&ptable.lock);
  return;
}


//mmap additional functions
void copy_mmap(struct mmap_area *mmap1, struct mmap_area *mmap2) {
  mmap1->addr = mmap2->addr;
  mmap1->length = mmap2->length;
  mmap1->flags = mmap2->flags;
  mmap1->prot = mmap2->prot;
  mmap1->fd = mmap2->fd;
  mmap1->offset = mmap2->offset;
}

int init_mmap(struct proc *p, int length, int i, uint mmapaddr) {
  int j=p->mmap_index;
  while (j>i+1) {
    copy_mmap(&p->mmaps[j], &p->mmaps[j-1]);
    j--;
  }
  p->mmaps[i+1].addr = mmapaddr;
  p->mmaps[i+1].length = length;
  return i+1;
}

int make_new_mmap(struct proc *p, uint addr, int length) {
  uint mmap_addr = PGROUNDUP(addr);
  if (mmap_addr > PGROUNDUP(p->mmaps[p->mmap_index - 1].addr + p->mmaps[p->mmap_index - 1].length)) {
    return init_mmap(p, length, p->mmap_index-1, mmap_addr);
  }
  int i = 0;
  for (; i<p->mmap_index - 1; i++) {
    if (p->mmaps[i].addr >= mmap_addr) {
      return -1;
    }
    int start_addr = PGROUNDUP(p->mmaps[i].addr + p->mmaps[i].length);
    int end_addr = PGROUNDUP(p->mmaps[i+1].addr);
    if (mmap_addr > start_addr && end_addr > mmap_addr + length){
      return init_mmap(p,length,p->mmap_index-1,mmap_addr);
    }
  }
  return -1;
}

// Project 3 functions that perform real action
uint 
mmap(int addr, int length, int prot, int flags, int fd, int offset)
{
  //Invalid input
  if (length <= 0 || offset < 0) {
    return -1;
  }
  struct proc *p = myproc();
  // over the max number of mmap array
  if (p->mmap_index == 64) {
    return -1;
  }
  // 3 cases
  int i = -1;
  if (flags & MAP_POPULATE) {
    // uint rounded_addr = PGROUNDUP(PGROUNDUP(addr) + length);
    i = make_new_mmap(p, addr, length);
    // mmap is not possible
    if (i==-1) {
      return -1;
    }

    if (flags & MAP_ANONYMOUS) { //populate with anonymous file
      //refresh the memory to 0
    }
    else { // populate
      //read from the file
    }
  }
  else { // No populate
    // uint rounded_addr = PGROUNDUP(PGROUNDUP(addr) + length);
    i = make_new_mmap(p, addr, length);
    // mmap is not possible
    if (i==-1) {
      return -1;
    }
  }
  p->mmaps[i].flags = flags;
  p->mmaps[i].prot = prot;
  p->mmaps[i].offset = offset;
  p->mmaps[i].fd = (void *)fd;
  p->mmap_index += 1;
  
  return p->mmaps[i].addr;
}

int
munmap(int addr)
{
  // int mmap_index = p->mmap_index;
  // while (mmap_index > 0) {
    // if (p->mmaps[])
  // }

  int ret;
  ret = 1; // succeed
  ret = -1; // fail
  return ret;
}

int
freemem(void)
{
  int page_num; // current number of free memory pages
  page_num = 3; // for example 
  return page_num;
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

  np->vruntime = curproc->vruntime;
  np->time_slice = curproc->time_slice;

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
        p->mmap_index = 0;
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
  struct proc *shortestVruntimeP;
  struct cpu *c = mycpu();
  c->proc = 0;
  int total_weights;

  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      shortestVruntimeP = p;
      total_weights = 0;
      // select the minimum vruntime p
      for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
       if(p1->state == RUNNABLE && p1->vruntime < shortestVruntimeP->vruntime) {
        // cprintf("RUNNABLE p pid : %d\n",p->pid);        
	   	  shortestVruntimeP = p1;
        total_weights += CFS_weights[p1->value];
       }
       
	    }  
     
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      p = shortestVruntimeP;

      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      p->initial_runtime = p->actual_runtime;
      myproc()->time_slice = 10000 * (CFS_weights[myproc()->value]/total_weights);
 
      // go to sched() swtch and do swtch from first line to before swtch 
      swtch(&(c->scheduler), p->context);
      // come back to scheduler after sched
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
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
  //yield called
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  if (myproc()->value>0)
    myproc()->value -= 1;
  myproc()->delta_runtime = myproc()->actual_runtime - myproc()->initial_runtime;
  myproc()->vruntime += (int)(myproc()->delta_runtime * (1024 / CFS_weights[myproc()->value])+0.5);
  myproc()->initial_runtime = 0;
  myproc()->actual_runtime = 0;


  //go sched first line -> sched swtch-> scheduler swtch 
  //-> next for loof swtch -> sched swtch 
  //-> back to yield here and release -> sched 1 and recycle  
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
