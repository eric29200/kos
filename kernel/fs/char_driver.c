#include <fs/fs.h>
#include <drivers/null.h>
#include <drivers/tty.h>
#include <fcntl.h>

/*
 * Get character device driver.
 */
struct inode_operations_t *char_get_driver(struct inode_t *inode)
{
  dev_t dev;

  /* no a character device */
  if (!inode || !S_ISCHR(inode->i_mode))
    return NULL;

  /* get device number */
  dev = inode->i_zone[0];

  /* null driver */
  if (dev == DEV_NULL)
    return &null_iops;

  /* tty driver */
  if (major(dev) == major(DEV_TTY) || major(dev) == major(DEV_TTY0))
    return &tty_iops;

  return NULL;
}
