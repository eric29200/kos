#ifndef _PTY_H_
#define _PTY_H_

#include <fs/fs.h>

extern struct inode_operations_t ptmx_iops;
extern struct inode_operations_t pty_iops;

#endif
