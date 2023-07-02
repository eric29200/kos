#include <fs/proc_fs.h>
#include <net/inet/net.h>
#include <fcntl.h>
#include <stdio.h>
#include <stderr.h>

#define NR_NET_DIRENTRY		(sizeof(net_dir) / sizeof(net_dir[0]))

/*
 * Net directory.
 */
static struct proc_dir_entry_t net_dir[] = {
	{ PROC_NET_INO,		1, 	"." },
	{ PROC_ROOT_INO,	2,	".." },
	{ PROC_NET_DEV_INO,	3,	"dev" },
};

/*
 * Read net dir.
 */
static int proc_net_getdents64(struct file_t *filp, void *dirp, size_t count)
{
	struct dirent64_t *dirent = (struct dirent64_t *) dirp;
	int ret, n;
	size_t i;

	/* read net dir entries */
	for (i = filp->f_pos, n = 0; i < NR_NET_DIRENTRY; i++, filp->f_pos++) {
		/* fill in directory entry */ 
		ret = filldir(dirent, net_dir[i].name, net_dir[i].name_len, net_dir[i].ino, count);
		if (ret)
			return n;

		/* go to next dir entry */
		count -= dirent->d_reclen;
		n += dirent->d_reclen;
		dirent = (struct dirent64_t *) ((void *) dirent + dirent->d_reclen);
	}

	return n;
}

/*
 * Lookup net dir.
 */
static int proc_net_lookup(struct inode_t *dir, const char *name, size_t name_len, struct inode_t **res_inode)
{
	ino_t ino;
	size_t i;

	/* dir must be a directory */
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}

	/* find matching entry */
	for (i = 0; i < NR_NET_DIRENTRY; i++) {
		if (proc_match(name, name_len, &net_dir[i])) {
			ino = net_dir[i].ino;
			break;
		}
	}

	/* get inode */
	*res_inode = iget(dir->i_sb, ino);
	if (!*res_inode) {
		iput(dir);
		return -EACCES;
	}

	iput(dir);
	return 0;
}

/*
 * Net file operations.
 */
struct file_operations_t proc_net_fops = {
	.getdents64		= proc_net_getdents64,
};

/*
 * Net inode operations.
 */
struct inode_operations_t proc_net_iops = {
	.fops			= &proc_net_fops,
	.lookup			= proc_net_lookup,
};

/*
 * Read net dev.
 */
static int proc_net_dev_read(struct file_t *filp, char *buf, int count)
{
	char tmp_buf[256];
	size_t len;
	int i;

	/* print header */
	len = sprintf(tmp_buf, "Inter-|   Receive                  |  Transmit\n"
		" face |bytes\tpackets errs drop fifo frame compressed multicast|bytes\tpackets errs drop fifo colls carrier compressed\n");

	/* print interfaces */
	for (i = 0; i < nr_net_devices; i++)
		len += sprintf(tmp_buf + len, "%s: No statistics available.\n", net_devices[i].name);

	/* file position after end */
	if (filp->f_pos >= len)
		return 0;

	/* update count */
	if (filp->f_pos + count > len)
		count = len - filp->f_pos;

	/* copy content to user buffer and update file position */
	memcpy(buf, tmp_buf + filp->f_pos, count);
	filp->f_pos += count;

	return count;
}

/*
 * Net dev file operations.
 */
struct file_operations_t proc_net_dev_fops = {
	.read		= proc_net_dev_read,
};

/*
 * Net dev inode operations.
 */
struct inode_operations_t proc_net_dev_iops = {
	.fops		= &proc_net_dev_fops,
};
