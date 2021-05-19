#ifndef _MMAP_H_
#define _MMAP_H_

#include <stddef.h>
#include <list.h>

#define VM_READ         0x01
#define VM_WRITE        0x02
#define VM_EXEC         0x04
#define VM_SHARED       0x08

/*
 * Virtual memory area structure.
 */
struct vm_area_t {
  uint32_t vm_start;
  uint32_t vm_end;
  uint16_t vm_flags;
  struct list_head_t list;
};

void *do_mmap(void *addr, size_t length, int prot, int flags);
int do_munmap(void *addr, size_t length);

#endif
