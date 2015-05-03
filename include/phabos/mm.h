/*
 * Copyright (C) 2015 Fabien Parent. All rights reserved.
 * Author: Fabien Parent <parent.f@gmail.com>
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#ifndef __MM_H__
#define __MM_H__

#include <stdlib.h>
#include <string.h>

#define MM_DMA          (1 << 0)

int mm_add_region(unsigned long addr, unsigned order, unsigned int flags);

void *kmalloc(size_t size, unsigned int flags);
void kfree(void *ptr);
void *page_alloc(unsigned int flags, int order);
void page_free(void *ptr, int order);

int size_to_order(size_t size);

static inline void *kzalloc(size_t size, unsigned int flags)
{
    void *ptr = kmalloc(size, 0);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

static inline void *zalloc(size_t size)
{
    return kzalloc(size, 0);
}

#endif /* __MM_H__ */
