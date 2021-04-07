#include <kernel/task.h>
#include <kernel/mm.h>
#include <kernel/interrupt.h>
#include <string.h>
#include <stderr.h>

/* threads list (idle thread is always the last thread in the list) */
LIST_HEAD(threads_list);
static struct thread_t *current_thread = NULL;
static struct thread_t *idle_thread = NULL;

/* tids counter */
static uint32_t next_tid = 0;

/* switch threads (defined in scheduler.s) */
extern void do_switch(uint32_t *current_esp, uint32_t next_esp);

/*
 * Destroy a thread.
 */
static void thread_destroy(struct thread_t *thread)
{
  if (!thread)
    return;

  kfree((void *) (thread->kernel_stack - STACK_SIZE));
  kfree(thread);
}


/*
 * Kernel thread trampoline (used to end threads properly).
 */
static void thread_entry(struct thread_t *thread, void (*func)())
{
  /* execute thread */
  func();

  /* remove thread from the list and destroy it */
  irq_disable();
  list_del(&thread->list);
  thread_destroy(thread);

  /* reschedule */
  schedule();
}

/*
 * Create a thread.
 */
static struct thread_t *create_thread(void (*func)(void))
{
  struct task_registers_t *regs;
  struct thread_t *thread;
  void *stack;

  /* create thread */
  thread = (struct thread_t *) kmalloc(sizeof(struct thread_t));
  if (!thread)
    return NULL;

  /* set tid */
  thread->tid = next_tid++;
  INIT_LIST_HEAD(&thread->list);

  /* allocate stack */
  stack = (void *) kmalloc(STACK_SIZE);
  if (!stack) {
    kfree(thread);
    return NULL;
  }

  /* set stack */
  memset(stack, 0, STACK_SIZE);
  thread->kernel_stack = (uint32_t) stack + STACK_SIZE;
  thread->esp = thread->kernel_stack - sizeof(struct task_registers_t);

  /* set registers */
  regs = (struct task_registers_t *) thread->esp;
  memset(regs, 0, sizeof(struct task_registers_t));

  /* set eip to function */
  regs->parameter1 = (uint32_t) thread;
  regs->parameter2 = (uint32_t) func;
  regs->return_address = 0xFFFFFFFF;
  regs->eip = (uint32_t) thread_entry;

  return thread;
}

/*
 * Start a thread.
 */
int start_thread(void (*func)(void))
{
  struct thread_t *thread;
  uint32_t flags;

  /* create a thread */
  thread = create_thread(func);
  if (!thread)
    return ENOMEM;

  /* add to the threads list */
  irq_save(flags);
  list_add(&thread->list, &threads_list);
  irq_restore(flags);

  return 0;
}

/*
 * Idle task.
 */
static void idle_task()
{
  for (;;)
    halt();
}

/*
 * Init task.
 */
int init_task()
{
  uint32_t flags;

  /* create idle thread */
  idle_thread = create_thread(idle_task);
  if (!idle_thread)
    return ENOMEM;

  /* add it to the threads list */
  irq_save(flags);
  list_add(&idle_thread->list, &threads_list);
  current_thread = idle_thread;
  irq_restore(flags);

  return 0;
}

/*
 * Schedule function.
 */
void schedule()
{
  struct thread_t *prev_thread = current_thread;

  /* take first thread */
  current_thread = list_first_entry(&threads_list, struct thread_t, list);

  /* put it at the end of the list (just before idle thread) */
  if (current_thread != idle_thread) {
    list_del(&current_thread->list);
    list_add_tail(&current_thread->list, &idle_thread->list);
  }

  /* switch threads */
  if (current_thread != prev_thread)
    do_switch(&prev_thread->esp, current_thread->esp);
}
