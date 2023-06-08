#include <fs/fs.h>
#include <mm/mm.h>
#include <drivers/char/tty.h>
#include <proc/sched.h>
#include <stdio.h>
#include <stderr.h>
#include <fcntl.h>
#include <string.h>

/* global file table */
struct file_t filp_table[NR_FILE];

/*
 * Get an empty file.
 */
struct file_t *get_empty_filp()
{
	int i;

	for (i = 0; i < NR_FILE; i++)
		if (!filp_table[i].f_ref)
			break;

	if (i >= NR_FILE)
		return NULL;

	filp_table[i].f_ref = 1;
	return &filp_table[i];
}

/*
 * Open system call.
 */
int do_open(int dirfd, const char *pathname, int flags, mode_t mode)
{
	struct inode_t *inode;
	struct file_t *filp;
	int fd, ret;

	/* find a free slot in current process */
	for (fd = 0; fd < NR_OPEN; fd++)
		if (!current_task->files->filp[fd])
			break;

	/* no slots : exit */
	if (fd >= NR_OPEN)
		return -EINVAL;

	/* get an empty file */
	filp = get_empty_filp();
	if (!filp)
		return -EINVAL;

	/* open file */
	ret = open_namei(dirfd, NULL, pathname, flags, mode, &inode);
	if (ret != 0)
		goto err;

	/* check inode operations */
	if (!inode->i_op) {
		ret = -EINVAL;
		goto err;
	}

	/* set file */
	current_task->files->filp[fd] = filp;
	FD_CLR(fd, &current_task->files->close_on_exec);
	filp->f_mode = inode->i_mode;
	filp->f_inode = inode;
	filp->f_flags = flags;
	filp->f_pos = 0;
	filp->f_op = inode->i_op->fops;

	/* set path */
	filp->f_path = strdup(pathname);
	if (!filp->f_path) {
		ret = -ENOMEM;
		goto err;
	}

	/* specific open function */
	if (filp->f_op && filp->f_op->open) {
		ret = filp->f_op->open(filp);
		if (ret)
			goto err;
	}

	return fd;
err:
	if (filp->f_path)
		kfree(filp->f_path);
	memset(filp, 0, sizeof(struct file_t));
	return ret;
}

/*
 * Open system call.
 */
int sys_open(const char *pathname, int flags, mode_t mode)
{
	return do_open(AT_FDCWD, pathname, flags, mode);
}

/*
 * Creat system call.
 */
int sys_creat(const char *pathname, mode_t mode)
{
	return do_open(AT_FDCWD, pathname, O_CREAT | O_TRUNC, mode);
}

/*
 * Openat system call.
 */
int sys_openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
	return do_open(dirfd, pathname, flags, mode);
}

/*
 * Close system call.
 */
int do_close(struct file_t *filp)
{
	filp->f_ref--;

	/* release file if not used anymore */
	if (filp->f_ref <= 0) {
		/* specific close operation */
		if (filp->f_op && filp->f_op->close)
			filp->f_op->close(filp);

		/* release inode */
		iput(filp->f_inode);

		/* free path */
		if (filp->f_path)
			kfree(filp->f_path);

		/* clear inode */
		memset(filp, 0, sizeof(struct file_t));
	}

	return 0;
}

/*
 * Close system call.
 */
int sys_close(int fd)
{
	int ret;

	/* check file descriptor */
	if (fd < 0 || fd >= NR_OPEN || !current_task->files->filp[fd])
		return -EINVAL;

	/* close file */
	ret = do_close(current_task->files->filp[fd]);
	if (ret)
		return ret;

	FD_CLR(fd, &current_task->files->close_on_exec);
	current_task->files->filp[fd] = NULL;
	return 0;
}

/*
 * Chmod system call.
 */
static int do_chmod(int dirfd, const char *pathname, mode_t mode)
{
	struct inode_t *inode;

	/* get inode */
	inode = namei(dirfd, NULL, pathname, 1);
	if (!inode)
		return -ENOSPC;

	/* adjust mode */
	if (mode == (mode_t) - 1)
		mode = inode->i_mode;

	/* change mode */
	inode->i_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	inode->i_dirt = 1;
	iput(inode);

	return 0;
}

/*
 * Chmod system call.
 */
int sys_chmod(const char *pathname, mode_t mode)
{
	return do_chmod(AT_FDCWD, pathname, mode);
}

/*
 * Fchmod system call.
 */
static int do_fchmod(int fd, mode_t mode)
{
	struct inode_t *inode;

	/* check file descriptor */
	if (fd < 0 || fd >= NR_OPEN || !current_task->files->filp[fd])
		return -EINVAL;

	/* get inode */
	inode = current_task->files->filp[fd]->f_inode;

	/* adjust mode */
	if (mode == (mode_t) - 1)
		mode = inode->i_mode;

	/* change mode */
	inode->i_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	inode->i_dirt = 1;

	return 0;
}

/*
 * Fchmod system call.
 */
int sys_fchmod(int fd, mode_t mode)
{
	return do_fchmod(fd, mode);
}

/*
 * Fchmodat system call.
 */
int sys_fchmodat(int dirfd, const char *pathname, mode_t mode, unsigned int flags)
{
	UNUSED(flags);
	return do_chmod(dirfd, pathname, mode);
}

/*
 * Chown system call.
 */
static int do_chown(int dirfd, const char *pathname, uid_t owner, gid_t group, unsigned int flags)
{
	struct inode_t *inode;

	/* get inode */
	inode = namei(dirfd, NULL, pathname, flags & AT_SYMLINK_NO_FOLLOW ? 0 : 1);
	if (!inode)
		return -ENOSPC;

	/* update inode */
	inode->i_uid = owner;
	inode->i_gid = group;
	inode->i_dirt = 1;
	iput(inode);

	return 0;
}

/*
 * Chown system call.
 */
int sys_chown(const char *pathname, uid_t owner, gid_t group)
{
	return do_chown(AT_FDCWD, pathname, owner, group, 0);
}

/*
 * Fchown system call.
 */
static int do_fchown(int fd, uid_t owner, gid_t group)
{
	struct inode_t *inode;

	/* check file descriptor */
	if (fd < 0 || fd >= NR_OPEN || !current_task->files->filp[fd])
		return -EINVAL;

	/* update inode */
	inode = current_task->files->filp[fd]->f_inode;
	inode->i_uid = owner;
	inode->i_gid = group;
	inode->i_dirt = 1;

	return 0;
}

/*
 * Fchown system call.
 */
int sys_fchown(int fd, uid_t owner, gid_t group)
{
	return do_fchown(fd, owner, group);
}

/*
 * Fchownat system call.
 */
int sys_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, unsigned int flags)
{
	return do_chown(dirfd, pathname, owner, group, flags);
}

/*
 * Utimensat system call.
 */
static int do_utimensat(int dirfd, const char *pathname, struct kernel_timeval_t *times, int flags)
{
	struct inode_t *inode;

	/* get inode */
	inode = namei(dirfd, NULL, pathname, flags & AT_SYMLINK_NO_FOLLOW ? 0 : 1);
	if (!inode)
		return -ENOENT;

	/* set time */
	if (times)
		inode->i_atime = inode->i_mtime = times[0].tv_sec;
	else
		inode->i_atime = inode->i_mtime = CURRENT_TIME;

	/* mark inode dirty */
	inode->i_dirt = 1;

	/* release inode */
	iput(inode);

	return 0;
}

/*
 * Utimensat system call.
 */
int sys_utimensat(int dirfd, const char *pathname, struct timespec_t *times, int flags)
{
	struct kernel_timeval_t ktimes[2];

	/* convert times to kernel timevals */
	if (times) {
		timespec_to_kernel_timeval(&times[0], &ktimes[0]);
		timespec_to_kernel_timeval(&times[1], &ktimes[1]);
	}

	return do_utimensat(dirfd, pathname, times ? ktimes : NULL, flags);
}

/*
 * Chroot system call.
 */
int sys_chroot(const char *path)
{
	struct inode_t *inode;

	/* get inode */
	inode = namei(AT_FDCWD, NULL, path, 1);
	if (!inode)
		return -ENOENT;

	/* check if it's a directory */
	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	/* release current root directory and change it */
	iput(current_task->fs->root);
	current_task->fs->root = inode;

	return 0;
}