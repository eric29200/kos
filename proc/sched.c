#include <x86/system.h>
#include <x86/interrupt.h>
#include <proc/sched.h>
#include <proc/task.h>
#include <proc/wait.h>
#include <list.h>
#include <lock.h>
#include <stderr.h>

/* tasks list */
LIST_HEAD(tasks_ready_list);
LIST_HEAD(tasks_waiting_list);
static struct task_t *current_task = NULL;
static struct task_t *idle_task = NULL;

/* scheduler lock */
spinlock_t sched_lock;

/* switch tasks (defined in scheduler.s) */
extern void scheduler_do_switch(uint32_t *current_esp, uint32_t next_esp);

/*
 * Idle task (used if no tasks are ready).
 */
void idle_task_func(void *arg)
{
  UNUSED(arg);

  for (;;)
    halt();
}

/*
 * Init scheduler.
 */
int init_scheduler(void (*init_func)(void *), void *init_arg)
{
  struct task_t *init_task;

  /* init scheduler lock */
  spin_lock_init(&sched_lock);

  /* create idle task */
  idle_task = create_task(idle_task_func, NULL);
  if (!idle_task)
    return ENOMEM;

  /* create init task */
  init_task = create_task(init_func, init_arg);
  if (!init_task) {
    destroy_task(idle_task);
    return ENOMEM;
  }

  /* run init task */
  return run_task(init_task);
}

/*
 * Pop next task to run.
 */
static struct task_t *pop_next_task()
{
  struct task_t *next_task;

  next_task = list_first_entry(&tasks_ready_list, struct task_t, list);
  list_del(&next_task->list);

  return next_task;
}

/*
 * Update task state.
 */
static void __update_task_state(struct task_t *task, uint8_t state)
{
  uint32_t flags;

  spin_lock_irqsave(&sched_lock, flags);

  /* update state */
  task->state = state;

  /* updates tasks list */
  list_del(&task->list);
  if (task->state == TASK_READY)
    list_add(&task->list, &tasks_ready_list);
  else if (task->state == TASK_WAITING)
    list_add(&task->list, &tasks_waiting_list);

  spin_unlock_irqrestore(&sched_lock, flags);
}

/*
 * Schedule function (interruptions must be disabled and will be reenabled on function return).
 */
void schedule()
{
  struct task_t *prev_task, *task;
  struct list_head_t *pos, *n;

  /* disable interrupts */
  irq_disable();

  /* remember current task */
  prev_task = current_task;

  /* update waiting tasks */
  list_for_each_safe(pos, n, &tasks_waiting_list) {
    task = list_entry(pos, struct task_t, list);

    /* timer expires : remove it from waiting list and run it */
    if (--task->expires == 0) {
      list_del(&task->list);
      run_task(task);
    }
  }

  /* scheduling algorithm */
  do {
    /* no tasks : break */
    if (list_empty(&tasks_ready_list)) {
      current_task = NULL;
      break;
    }

    /* pop next task */
    current_task = pop_next_task();

    /* if task is terminated : destroy it */
    if (current_task->state == TASK_TERMINATED) {
      current_task = NULL;
      continue;
    }

    /* put current task at the end of the list */
    list_add_tail(&current_task->list, &tasks_ready_list);
  } while (!current_task);

  /* no running task : use idle task */
  if (!current_task)
    current_task = idle_task;

  /* switch tasks */
  if (current_task != prev_task)
    scheduler_do_switch(&prev_task->esp, current_task->esp);

  /* restore irq */
  irq_enable();
}

/*
 * Put current task in sleep mode for timeout jiffies.
 */
void schedule_timeout(uint32_t timeout)
{
  uint32_t flags;

  spin_lock_irqsave(&sched_lock, flags);
  if (current_task) {
    /* set timeout */
    current_task->expires = timeout;
    current_task->state = TASK_WAITING;

    /* put current task in waiting list */
    list_del(&current_task->list);
    list_add(&current_task->list, &tasks_waiting_list);
  }
  spin_unlock_irqrestore(&sched_lock, flags);

  /* reschedule */
  schedule();
}

/*
 * Run a task.
 */
int run_task(struct task_t *task)
{
  if (!task)
    return EINVAL;

  /* add to the task list */
  __update_task_state(task, TASK_READY);

  return 0;
}

/*
 * Kill a task.
 */
void kill_task(struct task_t *task)
{
  /* NULL task */
  if (!task)
    return;

  /* mark task terminated and reschedule */
  __update_task_state(task, TASK_TERMINATED);

  /* call scheduler */
  schedule();
}

/*
 * Wait on a wait queue.
 */
void wait(struct wait_queue_head_t *q)
{
  struct wait_queue_t wait;
  uint32_t flags;

  /* create a wait queue entry for current task */
  spin_lock_irqsave(&sched_lock, flags);
  init_waitqueue_entry(&wait, current_task);
  spin_unlock_irqrestore(&sched_lock, flags);

  /* create a wait queue entry for current task */
  add_wait_queue(q, &wait);

  /* set current state to waiting */
  __update_task_state(wait.task, TASK_WAITING);

  /* call scheduler */
  schedule();

  /* remove from wait queue */
  remove_wait_queue(q, &wait);
}

/*
 * Wake up a task (internal function : wait queue head lock must be held).
 */
static void __wake_up(struct wait_queue_t *wait)
{
  /* remove it from the wait queue */
  list_del(&wait->list);

  /* update task status */
  __update_task_state(wait->task, TASK_READY);
}

/*
 * Wake up one task from the wait queue.
 */
void wake_up(struct wait_queue_head_t *q)
{
  struct wait_queue_t *wait;
  uint32_t flags;

  /* wake up first task */
  spin_lock_irqsave(&q->lock, flags);
  if (!list_empty(&q->task_list)) {
    wait = list_first_entry(&q->task_list, struct wait_queue_t, list);
    __wake_up(wait);
  }
  spin_unlock_irqrestore(&q->lock, flags);
}

/*
 * Wake up all tasks from the wait queue.
 */
void wake_up_all(struct wait_queue_head_t *q)
{
  struct list_head_t *pos, *n;
  struct wait_queue_t *wait;
  uint32_t flags;

  /* wake up each task */
  spin_lock_irqsave(&q->lock, flags);
  list_for_each_safe(pos, n, &q->task_list) {
    wait = list_entry(pos, struct wait_queue_t, list);
    __wake_up(wait);
  }
  spin_unlock_irqrestore(&q->lock, flags);
}
