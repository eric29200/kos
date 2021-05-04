#include <fs/fs.h>
#include <proc/sched.h>
#include <mm/mm.h>
#include <stdio.h>
#include <stderr.h>
#include <stat.h>
#include <string.h>

/*
 * Get an empty inode.
 */
struct inode_t *get_empty_inode()
{
  struct inode_t *inode;

  /* allocate a new inode and zero it */
  inode = (struct inode_t *) kmalloc(sizeof(struct inode_t));
  if (inode) {
    memset(inode, 0, sizeof(struct inode_t));
    inode->i_ref = 1;
  }

  return inode;
}

/*
 * Read an inode.
 */
static struct inode_t *read_inode(struct minix_super_block_t *sb, ino_t ino)
{
  struct minix_inode_t *minix_inode;
  struct buffer_head_t *bh;
  struct inode_t *inode;
  uint32_t block, i, j;

  /* get an empty inode */
  inode = get_empty_inode();
  if (!inode)
    return NULL;

  /* read minix inode block */
  block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks + (ino - 1) / MINIX_INODES_PER_BLOCK;
  bh = bread(sb->s_dev, block);
  if (!bh) {
    iput(inode);
    return NULL;
  }

  /* read minix inode */
  minix_inode = (struct minix_inode_t *) bh->b_data;
  i = (ino - 1) % MINIX_INODES_PER_BLOCK;

  /* fill in memory inode */
  inode->i_mode = minix_inode[i].i_mode;
  inode->i_uid = minix_inode[i].i_uid;
  inode->i_size = minix_inode[i].i_size;
  inode->i_time = minix_inode[i].i_time;
  inode->i_gid = minix_inode[i].i_gid;
  inode->i_nlinks = minix_inode[i].i_nlinks;
  for (j = 0; j < 9; j++)
    inode->i_zone[j] = minix_inode[i].i_zone[j];
  inode->i_ino = ino;
  inode->i_ref = 0;
  inode->i_sb = sb;
  inode->i_dev = sb->s_dev;

  /* free minix inode */
  brelse(bh);

  return inode;
}

/*
 * Write an inode on disk.
 */
static int write_inode(struct inode_t *inode)
{
  struct minix_inode_t *minix_inode;
  uint32_t block, i, j, ret;
  struct buffer_head_t *bh;

  if (!inode)
    return -EINVAL;

  /* read minix inode block */
  block = 2 + inode->i_sb->s_imap_blocks + inode->i_sb->s_zmap_blocks + (inode->i_ino - 1) / MINIX_INODES_PER_BLOCK;
  bh = bread(inode->i_sb->s_dev, block);
  if (!bh)
    return -EIO;

  /* read minix inode */
  minix_inode = (struct minix_inode_t *) bh->b_data;
  i = (inode->i_ino - 1) % MINIX_INODES_PER_BLOCK;

  /* fill in on disk inode */
  minix_inode[i].i_mode = inode->i_mode;
  minix_inode[i].i_uid = inode->i_uid;
  minix_inode[i].i_size = inode->i_size;
  minix_inode[i].i_time = inode->i_time;
  minix_inode[i].i_gid = inode->i_gid;
  minix_inode[i].i_nlinks = inode->i_nlinks;
  for (j = 0; j < 9; j++)
    minix_inode[i].i_zone[j] = inode->i_zone[j];

  /* write inode block */
  ret = bwrite(bh);
  brelse(bh);

  return ret;
}

/*
 * Get block number.
 */
int bmap(struct inode_t *inode, int block, int create)
{
  struct buffer_head_t *bh;
  int i;

  if (block < 0 || block >= 7 + 512 + 512 * 512)
    return 0;

  /* direct blocks */
  if (block < 7) {
    if (create && !inode->i_zone[block]) {
      inode->i_zone[block] = new_block();
      inode->i_dirt = 1;
    }

    return inode->i_zone[block];
  }

  /* first indirect block (contains address to 512 blocks) */
  block -= 7;
  if (block < 512) {
    /* create block if needed */
    if (create && !inode->i_zone[7]) {
      inode->i_zone[7] = new_block();
      inode->i_dirt = 1;
    }

    if (!inode->i_zone[7])
      return 0;

    /* read indirect block */
    bh = bread(inode->i_dev, inode->i_zone[7]);
    if (!bh)
      return 0;

    /* get matching block */
    i = ((uint16_t *) bh->b_data)[block];
    if (create && !i) {
      i = new_block();
      if (i) {
        ((uint16_t *) (bh->b_data))[block] = i;
        bh->b_dirt = 1;
      }
    }
    brelse(bh);
    return i;
  }

  /* second indirect block */
  block -= 512;
  if (create && !inode->i_zone[8]) {
    inode->i_zone[8] = new_block();
    inode->i_dirt = 1;
  }

  if (!inode->i_zone[8])
    return 0;

  /* read second indirect block */
  bh = bread(inode->i_dev, inode->i_zone[8]);
  if (!bh)
    return 0;
  i = ((uint16_t *) bh->b_data)[block >> 9];

  /* create it if needed */
  if (create && !i) {
    i = new_block();
    if (i) {
      ((uint16_t *) (bh->b_data))[block >> 9] = i;
      bh->b_dirt = 1;
    }
  }
  brelse(bh);

  if (!i)
    return 0;

  /* get second second indirect block */
  bh = bread(inode->i_dev, i);
  if (!bh)
    return 0;
  i = ((uint16_t *) bh->b_data)[block & 511];
  if (create && !i) {
    i = new_block();
    if (i) {
      ((uint16_t *) (bh->b_data))[block & 511] = i;
      bh->b_dirt = 1;
    }
  }
  brelse(bh);

  return i;
}

/*
 * Get an inode.
 */
struct inode_t *iget(struct minix_super_block_t *sb, ino_t ino)
{
  struct inode_t *inode;

  /* read inode */
  inode = read_inode(sb, ino);

  /* update reference count */
  if (inode)
    inode->i_ref++;

  return inode;
}

/*
 * Release an inode.
 */
void iput(struct inode_t *inode)
{
  if (!inode)
    return;

  /* write inode if needed */
  if (inode->i_dirt) {
    write_inode(inode);
    inode->i_dirt = 0;
  }

  /* free inode if not used anymore */
  inode->i_ref--;
  if (inode->i_ref <= 0)
    kfree(inode);
}
