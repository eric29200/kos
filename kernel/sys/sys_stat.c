#include <sys/syscall.h>
#include <fcntl.h>

/*
 * Stat system call.
 */
int sys_stat(const char *filename, struct stat_t *statbuf)
{
  return do_stat(AT_FDCWD, filename, statbuf);
}
