#include <fs/fs.h>
#include <proc/sched.h>
#include <proc/timer.h>
#include <drivers/ata.h>
#include <mm/mm.h>
#include <string.h>
#include <stderr.h>
#include <stdio.h>
#include <time.h>
#include <dev.h>

/* global buffer table */
static int nr_buffer = 0;
static int buffer_htable_bits = 0;
static struct buffer_head_t *buffer_table = NULL;
static struct htable_link_t **buffer_htable = NULL;
static LIST_HEAD(lru_buffers);

/* block size of devices */
size_t *blocksize_size[MAX_BLKDEV] = { NULL, NULL };

/*
 * Set blocksize of a device.
 */
void set_blocksize(dev_t dev, size_t blocksize)
{
	/* check device */
	if (!blocksize_size[major(dev)])
		return;

	/* check block size */
	switch (blocksize) {
		case 512:
		case 1024:
		case 2048:
		case 4096:
			break;
		default:
			panic("set_blocksize : invalid block size");
	}

	/* nothing to do */
	if (blocksize_size[major(dev)][minor(dev)] == blocksize)
		return;

 	/* sync buffers */
	bsync();

	/* set block size */
	blocksize_size[major(dev)][minor(dev)] = blocksize;
}

/*
 * Write a block buffer.
 */
int bwrite(struct buffer_head_t *bh)
{
	int ret;

	if (!bh)
		return -EINVAL;

	/* write to block device */
	ret = ata_write(bh->b_dev, bh);
	if (ret)
		return ret;

	bh->b_dirt = 0;
	return ret;
}

/*
 * Get an empty buffer.
 */
static struct buffer_head_t *get_empty_buffer(dev_t dev, size_t blocksize)
{
	struct buffer_head_t *bh;
	struct list_head_t *pos;

	/* get first free entry from LRU list */
	list_for_each(pos, &lru_buffers) {
		bh = list_entry(pos, struct buffer_head_t, b_list);
		if (!bh->b_ref)
			goto found;
	}

	/* no free buffer : exit */
	return NULL;

found:
	/* write it on disk if needed */
	if (bh->b_dirt && bwrite(bh))
		printf("Can't write block %d on disk\n", bh->b_block);

	/* allocate data if needed */
	if (!bh->b_data) {
		bh->b_data = (char *) get_free_page();
		if (!bh->b_data)
			return NULL;
	}

	/* reset buffer */
	bh->b_ref = 1;
	bh->b_dev = dev;
	bh->b_size = blocksize;
	memset(bh->b_data, 0, bh->b_size);

	return bh;
}

/*
 * Get a buffer (from cache or create one).
 */
struct buffer_head_t *getblk(dev_t dev, uint32_t block, size_t blocksize)
{
	struct htable_link_t *node;
	struct buffer_head_t *bh;

	/* try to find buffer in cache */
	node = htable_lookup(buffer_htable, block, buffer_htable_bits);
	while (node) {
		bh = htable_entry(node, struct buffer_head_t, b_htable);
		if (bh->b_block == block && bh->b_dev == dev && bh->b_size == blocksize) {
			bh->b_ref++;
			goto out;
		}

		node = node->next;
	}

	/* get an empty buffer */
	bh = get_empty_buffer(dev, blocksize);
	if (!bh)
		return NULL;

	/* set buffer */
	bh->b_dev = dev;
	bh->b_block = block;
	bh->b_uptodate = 0;

	/* hash the new buffer */
	htable_delete(&bh->b_htable);
	htable_insert(buffer_htable, &bh->b_htable, block, buffer_htable_bits);
out:
	/* put it at the end of LRU list */
	list_del(&bh->b_list);
	list_add_tail(&bh->b_list, &lru_buffers);
	return bh;
}

/*
 * Read a block from a device.
 */
struct buffer_head_t *bread(dev_t dev, uint32_t block, size_t blocksize)
{
	struct buffer_head_t *bh;

	/* get buffer */
	bh = getblk(dev, block, blocksize);
	if (!bh)
		return NULL;

	/* read it from device */
	if (!bh->b_uptodate && ata_read(dev, bh) != 0) {
		brelse(bh);
		return NULL;
	}

	bh->b_uptodate = 1;
	return bh;
}

/*
 * Release a buffer.
 */
void brelse(struct buffer_head_t *bh)
{
	if (!bh)
		return;

	/* write diry buffer */
	if (bh->b_dirt)
		bwrite(bh);

	/* update inode reference count */
	bh->b_ref--;
}

/*
 * Reclaim buffers cache.
 */
void reclaim_buffers()
{
	int i;

	for (i = 0; i < nr_buffer; i++) {
		/* used buffer */
		if (buffer_table[i].b_ref || buffer_table[i].b_dirt)
			continue;

		/* free data */
		if (buffer_table[i].b_data)
			free_page(buffer_table[i].b_data);

		/* remove it from lists */
		htable_delete(&buffer_table[i].b_htable);
		list_del(&buffer_table[i].b_list);

		/* reset buffer */
		memset(&buffer_table[i], 0, sizeof(struct buffer_head_t));

		/* add it to LRU buffers list */
		list_add(&buffer_table[i].b_list, &lru_buffers);
	}
}

/*
 * Write all dirty buffers on disk.
 */
void bsync()
{
	int i;

	/* write all dirty buffers */
	for (i = 0; i < nr_buffer; i++) {
		if (buffer_table[i].b_dirt && bwrite(&buffer_table[i])) {
			printf("Can't write block %d on disk\n", buffer_table[i].b_block);
			panic("Disk error");
		}
	}
}

/*
 * Init buffers.
 */
int binit()
{
	void *addr;
	int nr, i;

	/* number of buffers = number of pages / 4 */
	nr_buffer = 1 << blksize_bits(nb_pages / 4);
	buffer_htable_bits = blksize_bits(nr_buffer);

	/* allocate buffers */
	nr = 1 + nr_buffer * sizeof(struct buffer_head_t) / PAGE_SIZE;
	for (i = 0; i < nr; i++) {
		/* get a free page */
		addr = get_free_page();
		if (!addr)
			return -ENOMEM;

		/* reset page */
		memset(addr, 0, PAGE_SIZE);

		/* set buffer table */
		if (i == 0)
			buffer_table = addr;
	}

	/* allocate buffers hash table */
	nr = 1 + nr_buffer * sizeof(struct htable_link_t *) / PAGE_SIZE;
	for (i = 0; i < nr; i++) {
		/* get a free page */
		addr = get_free_page();
		if (!addr)
			return -ENOMEM;

		/* reset page */
		memset(addr, 0, PAGE_SIZE);

		/* set buffer hash table */
		if (i == 0)
			buffer_htable = addr;
	}

	/* init Last Recently Used buffers list */
	INIT_LIST_HEAD(&lru_buffers);

	/* add all buffers to LRU list */
	for (i = 0; i < nr_buffer; i++)
		list_add(&buffer_table[i].b_list, &lru_buffers);

	/* init buffers hash table */
	htable_init(buffer_htable, buffer_htable_bits);

	return 0;
}
