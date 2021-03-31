#ifndef _MM_H_
#define _MM_H_

#include <lib/stddef.h>

void init_mem(uint32_t start, uint32_t end);
void *kmalloc(size_t size);
void kfree(void *p);

#endif
