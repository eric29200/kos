#include <drivers/char/random.h>
#include <drivers/char/keyboard.h>
#include <proc/sched.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <x86/io.h>
#include <stdio.h>
#include <stderr.h>
#include <resource.h>
#include <reboot.h>

/*
 * Get time system call.
 */
int sys_clock_gettime64(clockid_t clockid, struct timespec_t *tp)
{
	switch (clockid) {
		case CLOCK_REALTIME:
			tp->tv_sec = startup_time + xtimes.tv_sec;
			tp->tv_nsec = xtimes.tv_nsec;
			break;
		case CLOCK_MONOTONIC:
			tp->tv_sec = xtimes.tv_sec;
			tp->tv_nsec = xtimes.tv_nsec;
			break;
		default:
			printf("clock_gettime64 not implement on clockid=%d\n", clockid);
			return -ENOSYS;
	}

	return 0;
}

/*
 * Get time system call.
 */
int sys_clock_gettime32(clockid_t clockid, struct old_timespec_t *tp)
{
	switch (clockid) {
		case CLOCK_REALTIME:
			tp->tv_sec = startup_time + xtimes.tv_sec;
			tp->tv_nsec = xtimes.tv_nsec;
			break;
		case CLOCK_MONOTONIC:
			tp->tv_sec = xtimes.tv_sec;
			tp->tv_nsec = xtimes.tv_nsec;
			break;
		default:
			printf("clock_gettime32 not implement on clockid=%d\n", clockid);
			return -ENOSYS;
	}

	return 0;
}

/*
 * Get random system call.
 */
int sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
	/* unused flags */
	UNUSED(flags);

	/* use /dev/random */
	return random_iops.fops->read(NULL, buf, buflen);
}

/*
 * Get rusage system call.
 */
int sys_getrusage(int who, struct rusage_t *ru)
{
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;

	/* reset rusage */
	memset(ru, 0, sizeof(struct rusage_t));

	return 0;
}

/*
 * Nano sleep system call.
 */
int sys_nanosleep(const struct old_timespec_t *req, struct old_timespec_t *rem)
{
	time_t timeout;

	/* check request */
	if (req->tv_nsec < 0 || req->tv_sec < 0)
		return -EINVAL;

	/* compute delay in jiffies */
	timeout = old_timespec_to_jiffies(req) + (req->tv_sec || req->tv_nsec) + jiffies;

	/* set current state sleeping and set timeout */
	current_task->state = TASK_SLEEPING;
	current_task->timeout = timeout;

	/* reschedule */
	schedule();

	/* task interrupted before timer end */
	if (timeout > jiffies) {
		if (rem)
			jiffies_to_old_timespec(timeout - jiffies - (timeout > jiffies + 1), rem);

		return -EINTR;
	}

	return 0;
}

/*
 * Pause system call.
 */
int sys_pause()
{
	/* set current state sleeping and reschedule */
	current_task->state = TASK_SLEEPING;
	schedule();

	return -ERESTARTNOHAND;
}

/*
 * Prlimit system call.
 */
int sys_prlimit64(pid_t pid, int resource, struct rlimit64_t *new_limit, struct rlimit64_t *old_limit)
{
	struct task_t *task;

	/* check resource */
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	/* get task */
	if (pid)
		task = find_task(pid);
	else
		task = current_task;

	/* no matching task */
	if (!task)
		return -ESRCH;

	/* write prlimit not implemented */
	if (new_limit)
		printf("write prlimit not implemented\n");

	/* get limit */
	if (old_limit) {
		memset(old_limit, 0, sizeof(struct rlimit64_t));
		old_limit->rlim_cur = task->rlim[resource].rlim_cur;
		old_limit->rlim_max = task->rlim[resource].rlim_max;
	}

	return 0;
}

/*
 * Restart CPU.
 */
static int do_restart()
{
	uint8_t out;

	/* disable interrupts */
	irq_disable();

	/* clear keyboard buffer */
	out = 0x02;
	while (out & 0x02)
		out = inb(KEYBOARD_STATUS);

	/* pluse CPU reset line */
	outb(KEYBOARD_STATUS, KEYBOARD_RESET);
	halt();

	return 0;
}

/*
 * Reboot system call.
 */
int sys_reboot(int magic1, int magic2, int cmd, void *arg)
{
	/* unused argument */
	UNUSED(arg);

	/* check magic */
	if ((uint32_t) magic1 != LINUX_REBOOT_MAGIC1 || (
		magic2 != LINUX_REBOOT_MAGIC2
		&& magic2 != LINUX_REBOOT_MAGIC2A
		&& magic2 != LINUX_REBOOT_MAGIC2B
		&& magic2 != LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	switch (cmd) {
		case LINUX_REBOOT_CMD_RESTART:
		case LINUX_REBOOT_CMD_RESTART2:
		case LINUX_REBOOT_CMD_POWER_OFF:
		case LINUX_REBOOT_CMD_HALT:
			return do_restart();
		case LINUX_REBOOT_CMD_CAD_ON:
			break;
		case LINUX_REBOOT_CMD_CAD_OFF:
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

/*
 * Timer handler = send a SIGALRM signal to caller.
 */
static void itimer_handler(void *arg)
{
	pid_t *pid = (pid_t *) arg;
	task_signal(*pid, SIGALRM);
}

/*
 * Set an interval timer (send SIGALRM signal at expiration).
 */
int sys_setitimer(int which, const struct itimerval_t *new_value, struct itimerval_t *old_value)
{
	uint32_t expires_ms;

	/* unused old value */
	UNUSED(old_value);

	/* implement only real timer */
	if (which != ITIMER_REAL) {
		printf("setitimer (%d) not implemented\n", which);
		return -ENOSYS;
	}

	/* compute expiration in ms */
	expires_ms = new_value->it_value_sec * 1000;
	expires_ms += (new_value->it_value_usec / 1000);

	/* delete timer */
	if (current_task->sig_tm.list.next)
		timer_event_del(&current_task->sig_tm);

	/* set timer */
	if (new_value->it_value_sec || new_value->it_value_usec) {
		timer_event_init(&current_task->sig_tm, itimer_handler, &current_task->pid, jiffies + ms_to_jiffies(expires_ms));
		timer_event_add(&current_task->sig_tm);
	}

	return 0;
}

/*
 * System info system call.
 */
int sys_sysinfo(struct sysinfo_t *info)
{
	memset(info, 0, sizeof(struct sysinfo_t));
	info->uptime = jiffies / HZ;
	info->totalram = 0;
	return 0;
}

/*
 * Umask system call.
 */
mode_t sys_umask(mode_t mask)
{
	mode_t ret = current_task->fs->umask;
	current_task->fs->umask = mask & 0777;
	return ret;
}

/*
 * Uname system call.
 */
int sys_uname(struct utsname_t *buf)
{
	if (!buf)
		return -EINVAL;

	strncpy(buf->sysname, "nulix", UTSNAME_LEN);
	strncpy(buf->nodename, "nulix", UTSNAME_LEN);
	strncpy(buf->release, "0.0.1", UTSNAME_LEN);
	strncpy(buf->version, "nulix 0.0.1", UTSNAME_LEN);
	strncpy(buf->machine, "x86", UTSNAME_LEN);

	return 0;
}
