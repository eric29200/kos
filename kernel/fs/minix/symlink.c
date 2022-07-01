#include <fs/fs.h>
#include <fs/minix_fs.h>
#include <fcntl.h>
#include <stderr.h>

/*
 * Resolve a symbolic link.
 */
int minix_follow_link(struct inode_t *dir, struct inode_t *inode, struct inode_t **res_inode)
{
	struct buffer_head_t *bh;

	*res_inode = NULL;

	/* null inode */
	if (!inode) {
		return -ENOENT;
	}

	if (!S_ISLNK(inode->i_mode)) {
		*res_inode = inode;
		return 0;
	}

	/* read first link block */
	bh = minix_bread(inode, 0, 0);
	if (!bh) {
		iput(inode);
		return -EIO;
	}

	/* release link inode */
	iput(inode);

	/* resolve target inode */
	*res_inode = namei(AT_FDCWD, dir, bh->b_data, 0);
	if (!*res_inode) {
		brelse(bh);
		return -EACCES;
	}

	/* release link buffer */
	brelse(bh);
	return 0;
}

/*
 * Read value of a symbolic link.
 */
ssize_t minix_readlink(struct inode_t *inode, char *buf, size_t bufsize)
{
	struct buffer_head_t *bh;
	size_t len;

	/* inode must be link */
	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}

	/* limit buffer size to block size */
	if (bufsize > MINIX_BLOCK_SIZE - 1)
		bufsize = MINIX_BLOCK_SIZE - 1;

	/* check 1st block */
	if (!inode->u.minix_i.i_zone[0]) {
		iput(inode);
		return 0;
	}

	/* read 1st block */
	bh = minix_bread(inode, 0, 0);
	if (!bh) {
		iput(inode);
		return 0;
	}

	/* release inode */
	iput(inode);

	/* copy target name to user buffer */
	for (len = 0; len < bufsize; len++)
		buf[len] = bh->b_data[len];

	/* release buffer */
	brelse(bh);
	return len;
}
