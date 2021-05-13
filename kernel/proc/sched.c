#include <x86/system.h>
#include <x86/interrupt.h>
#include <x86/tss.h>
#include <proc/sched.h>
#include <proc/task.h>
#include <proc/timer.h>
#include <sys/syscall.h>
#include <list.h>
#include <stderr.h>

LIST_HEAD(tasks_list);                    /* active processes list */
static struct task_t *kinit_task;         /* kernel init task (pid = 0) */
struct task_t *init_task;                 /* user init task (pid = 1) */
struct task_t *current_task = NULL;       /* current task */
static pid_t next_pid = 0;                /* pid counter */

/* switch tasks (defined in scheduler.s) */
extern void scheduler_do_switch(uint32_t *current_esp, uint32_t next_esp);

/*
 * Get next tid.
 */
pid_t get_next_pid()
{
  pid_t ret;
  ret = next_pid++;
  return ret;
}

/*
 * Init scheduler.
 */
int init_scheduler(void (*kinit_func)())
{
  /* create init task */
  kinit_task = create_kinit_task(kinit_func);
  if (!kinit_task)
    return -ENOMEM;

  /* set current task to kinit */
  list_add(&kinit_task->list, &tasks_list);

  return 0;
}

/*
 * Get next task to run.
 */
static struct task_t *get_next_task()
{
  struct list_head_t *pos;
  struct task_t *task;

  /* first scheduler call : return kinit */
  if (!current_task)
    return kinit_task;

  /* get next running task */
  list_for_each(pos, &current_task->list) {
    if (pos == &tasks_list)
      continue;

    task = list_entry(pos, struct task_t, list);
    if (task->state == TASK_RUNNING)
      return task;
  }

  /* no tasks found : return current if still running */
  if (current_task->state == TASK_RUNNING)
    return current_task;

  /* else execute kinit */
  return kinit_task;
}

/*
 * Spawn init process.
 */
int spawn_init()
{
  init_task = create_init_task();
  if (!init_task)
    return -ENOMEM;

  return 0;
}

/*
 * Schedule function (interruptions must be disabled and will be reenabled on function return).
 */
void schedule()
{
  struct task_t *prev_task, *task;
  struct list_head_t *pos;

  /* update timers */
  timer_update();

  /* update timeout on all tasks and wake them if needed */
  list_for_each(pos, &tasks_list) {
    task = list_entry(pos, struct task_t, list);
    if (task->timeout && task->timeout < jiffies) {
      task->timeout = 0;
      task->state = TASK_RUNNING;
    }
  }

  /* get next task to run */
  prev_task = current_task;
  current_task = get_next_task();

  /* switch tasks */
  if (prev_task != current_task) {
    tss_set_stack(0x10, current_task->kernel_stack);
    switch_page_directory(current_task->pgd);
    scheduler_do_switch(&prev_task->esp, current_task->esp);
  }
}

/*
 * Sleep on a channel.
 */
void task_sleep(void *chan)
{
  /* sleep on channel */
  current_task->waiting_chan = chan;
  current_task->state = TASK_SLEEPING;

  /* reschedule */
  schedule();

  /* reset waiting channel */
  current_task->waiting_chan = NULL;
}

/*
 * Wake up one task sleeping on channel.
 */
void task_wakeup(void *chan)
{
  struct list_head_t *pos;
  struct task_t *task;

  list_for_each(pos, &tasks_list) {
    task = list_entry(pos, struct task_t, list);
    if (task->waiting_chan == chan && task->state == TASK_SLEEPING) {
      task->state = TASK_RUNNING;
      break;
    }
  }
}

/*
 * Wake up all tasks sleeping on channel.
 */
void task_wakeup_all(void *chan)
{
  struct list_head_t *pos;
  struct task_t *task;

  list_for_each(pos, &tasks_list) {
    task = list_entry(pos, struct task_t, list);
    if (task->waiting_chan == chan && task->state == TASK_SLEEPING)
      task->state = TASK_RUNNING;
  }
}

/*
 * Get task with a pid.
 */
struct task_t *get_task(pid_t pid)
{
  struct list_head_t *pos;
  struct task_t *task;

  list_for_each(pos, &tasks_list) {
    task = list_entry(pos, struct task_t, list);
    if (task->pid == pid)
      return task;
  }

  return NULL;
}

/*
 * Send a signal to a task.
 */
static void __task_signal(struct task_t *task, int sig)
{
  /* just check permission */
  if (sig == 0)
    return;

  /* add to pending signals */
  sigaddset(&task->sigpend, sig);

  /* wakeup process if sleeping and sig is not masked */
  if (!sigismember(&task->sigmask, sig) && task->state == TASK_SLEEPING)
    task->state = TASK_RUNNING;
}

/*
 * Send a signal to a task.
 */
int task_signal(pid_t pid, int sig)
{
  struct task_t *task;

  /* get task */
  task = get_task(pid);
  if (!task)
    return -EINVAL;

  /* send signal */
  __task_signal(task, sig);

  return 0;
}

/*
 * Send a signal to all tasks in a group.
 */
int task_signal_group(pid_t pgid, int sig)
{
  struct list_head_t *pos;
  struct task_t *task;

  list_for_each(pos, &tasks_list) {
    task = list_entry(pos, struct task_t, list);
    if (task->pgid == pgid)
      __task_signal(task, sig);
  }

  return 0;
}

/*
 * Send a signal to all tasks (except init process).
 */
int task_signal_all(int sig)
{
  struct list_head_t *pos;
  struct task_t *task;

  list_for_each(pos, &tasks_list) {
    task = list_entry(pos, struct task_t, list);
    if (task->pid > 1)
      __task_signal(task, sig);
  }

  return 0;
}

/*
 * Handle signal of current task.
 */
int do_signal(struct registers_t *regs)
{
  struct sigaction_t *act;
  int sig;

  /* get first unblocked signal */
  for (sig = 0; sig < NSIGS; sig++)
    if (sigismember(&current_task->sigpend, sig) && !sigismember(&current_task->sigmask, sig))
      break;

  /* no signal */
  if (sig == NSIGS)
    return 0;

  /* remove signal from current task */
  sigdelset(&current_task->sigpend, sig);
  act = &current_task->signals[sig - 1];

  /* ignore signal handler */
  if (act->sa_handler == SIG_IGN || act->sa_handler == NULL)
    return 0;

  /* default signal handler */
  if (act->sa_handler == SIG_DFL) {
    switch (sig) {
      /* ignore those signals */
      case SIGCONT: case SIGCHLD: case SIGWINCH:
        return 0;
      default:
        sys_exit(sig);
        break;
    }
  }

  return 0;
}
