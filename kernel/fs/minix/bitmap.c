#include <fs/fs.h>
#include <fs/minix_fs.h>
#include <proc/sched.h>
#include <mm/mm.h>
#include <string.h>
#include <stdio.h>
#include <stderr.h>

#define MINIX_SET_BITMAP(bh, i)			((bh)->b_data[(i) / 8] |= (0x1 << ((i) % 8)))
#define MINIX_CLEAR_BITMAP(bh, i)		((bh)->b_data[(i) / 8] &= ~(0x1 << ((i) % 8)))

/*
 * Get first free bit in a bitmap block (inode or block).
 */
static inline int minix_get_free_bitmap(struct buffer_head_t *bh)
{
	uint32_t *bits = (uint32_t *) bh->b_data;
	register int i, j;

	for (i = 0; i < MINIX_BLOCK_SIZE / 4; i++)
		if (bits[i] != 0xFFFFFFFF)
			for (j = 0; j < 32; j++)
				if (!(bits[i] & (0x1 << j)))
					return 32 * i + j;

	return -1;
}

/*
 * Create a new block.
 */
uint32_t minix_new_block(struct super_block_t *sb)
{
	struct minix_sb_info_t *sbi = minix_sb(sb);
	struct buffer_head_t *bh;
	uint32_t block_nr, i;
	int j;

	/* find first free block in bitmap */
	for (i = 0; i < sbi->s_zmap_blocks; i++) {
		j = minix_get_free_bitmap(sbi->s_zmap[i]);
		if (j != -1)
			break;
	}

	/* no free block */
	if (j == -1)
		return 0;

	/* compute real block number */
	block_nr = j + i * MINIX_BLOCK_SIZE * 8 + sbi->s_firstdatazone - 1;
	if (block_nr >= sbi->s_nzones)
		return 0;

	/* get a buffer */
	bh = getblk(sb, block_nr);
	if (!bh)
		return 0;

	/* memzero buffer and release it */
	memset(bh->b_data, 0, MINIX_BLOCK_SIZE);
	bh->b_dirt = 1;
	bh->b_uptodate = 1;
	brelse(bh);

	/* set block in bitmap */
	MINIX_SET_BITMAP(sbi->s_zmap[i], j);
	sbi->s_zmap[i]->b_dirt = 1;

	return block_nr;
}

/*
 * Free a block.
 */
int minix_free_block(struct super_block_t *sb, uint32_t block)
{
	struct minix_sb_info_t *sbi = minix_sb(sb);
	struct buffer_head_t *bh;

	/* check block number */
	if (block < sbi->s_firstdatazone || block >= sbi->s_nzones)
		return -EINVAL;

	/* get buffer and clear it */
	bh = bread(sb, block);
	if (bh) {
		bh->b_dirt = 1;
		memset(bh->b_data, 0, MINIX_BLOCK_SIZE);
	}
	brelse(bh);

	/* update/clear block bitmap */
	block -= sbi->s_firstdatazone - 1;
	bh = sbi->s_zmap[block / (8 * MINIX_BLOCK_SIZE)];
	MINIX_CLEAR_BITMAP(bh, block & (MINIX_BLOCK_SIZE * 8 - 1));
	bh->b_dirt = 1;

	return 0;
}

/*
 * Free an inode.
 */
int minix_free_inode(struct inode_t *inode)
{
	struct buffer_head_t *bh;

	if (!inode)
		return 0;

	/* panic if inode is still used */
	if (inode->i_ref > 1) {
		printf("Trying to free inode %d with ref=%d\n", inode->i_ino, inode->i_ref);
		panic("");
	}

	/* update/clear inode bitmap */
	bh = minix_sb(inode->i_sb)->s_imap[inode->i_ino >> 13];
	MINIX_CLEAR_BITMAP(bh, inode->i_ino & (MINIX_BLOCK_SIZE * 8 - 1));
	bh->b_dirt = 1;

	/* free inode */
	memset(inode, 0, sizeof(struct inode_t));

	return 0;
}

/*
 * Create a new inode.
 */
struct inode_t *minix_new_inode(struct super_block_t *sb)
{
	struct minix_sb_info_t *sbi = minix_sb(sb);
	struct inode_t *inode;
	uint32_t i;
	int j;

	/* get an empty new inode */
	inode = get_empty_inode();
	if (!inode)
		return NULL;

	/* find first free inode in bitmap */
	for (i = 0; i < sbi->s_imap_blocks; i++) {
		j = minix_get_free_bitmap(sbi->s_imap[i]);
		if (j != -1)
			break;
	}

	/* no free inode */
	if (j == -1)
		iput(inode);

	/* set inode */
	memset(inode, 0, sizeof(struct inode_t));
	inode->i_time = CURRENT_TIME;
	inode->i_nlinks = 1;
	inode->i_ino = i * MINIX_BLOCK_SIZE * 8 + j;
	inode->i_ref = 1;
	inode->i_sb = sb;

	/* set inode in bitmap */
	MINIX_SET_BITMAP(sbi->s_imap[i], j);
	sbi->s_imap[i]->b_dirt = 1;

	return inode;
}
