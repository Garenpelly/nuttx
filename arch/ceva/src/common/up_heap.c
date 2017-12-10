/****************************************************************************
 * arch/ceva/src/common/up_heap.c
 *
 *   Copyright (C) 2018 Pinecone Inc. All rights reserved.
 *   Author: Xiang Xiao <xiaoxiang@pinecone.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#include "mpu.h"
#include "up_arch.h"
#include "up_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_MM_KERNEL_HEAP
#  define MM_DEF_HEAP   &g_kmmheap
#else
#  define MM_DEF_HEAP   &g_mmheap
#endif

#if CONFIG_ARCH_DEFAULT_HEAP == 0
#  define MM_HEAP1      MM_DEF_HEAP
#else
static struct mm_heap_s g_mmheap1;
#  define MM_HEAP1      &g_mmheap1
#endif

#if CONFIG_ARCH_NR_MEMORY >= 2
#  if CONFIG_ARCH_DEFAULT_HEAP == 1
#    define MM_HEAP2    MM_DEF_HEAP
#  else
static struct mm_heap_s g_mmheap2;
#    define MM_HEAP2    &g_mmheap2
#  endif
#  define _END_BSS2     ((void *)&_ebss2)
#  define _END_HEAP2    ((void *)&_eheap2)
#else
#  define MM_HEAP2      NULL
#  define _END_BSS2     NULL
#  define _END_HEAP2    NULL
#endif

#if CONFIG_ARCH_NR_MEMORY >= 3
#  if CONFIG_ARCH_DEFAULT_HEAP == 2
#    define MM_HEAP3    MM_DEF_HEAP
#  else
static struct mm_heap_s g_mmheap3;
#    define MM_HEAP3    &g_mmheap3
#  endif
#  define _END_BSS3     ((void *)&_ebss3)
#  define _END_HEAP3    ((void *)&_eheap3)
#else
#  define MM_HEAP3      NULL
#  define _END_BSS3     NULL
#  define _END_HEAP3    NULL
#endif

#if CONFIG_ARCH_NR_MEMORY >= 4
#  if CONFIG_ARCH_DEFAULT_HEAP == 3
#    define MM_HEAP4    MM_DEF_HEAP
#  else
static struct mm_heap_s g_mmheap4;
#    define MM_HEAP4    &g_mmheap4
#  endif
#  define _END_BSS4     ((void *)&_ebss4)
#  define _END_HEAP4    ((void *)&_eheap4)
#else
#  define MM_HEAP4      NULL
#  define _END_BSS4     NULL
#  define _END_HEAP4    NULL
#endif

#if CONFIG_ARCH_NR_MEMORY >= 5
#  error CONFIG_ARCH_NR_MEMORY must between 1 to 4
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static void *const g_bssend[] =
{
  _START_HEAP, _END_BSS2, _END_BSS3, _END_BSS4, NULL,
};

static void *const g_heapend[] =
{
  _END_HEAP, _END_HEAP2, _END_HEAP3, _END_HEAP4, NULL,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct mm_heap_s *const g_mm_heap[] =
{
  MM_HEAP1, MM_HEAP2, MM_HEAP3, MM_HEAP4, NULL,
};

/* g_idle_topstack: _sbss is the start of the BSS region as defined by the
 * linker script. _ebss lies at the end of the BSS region. The idle task
 * stack starts at the end of BSS and is of size CONFIG_IDLETHREAD_STACKSIZE.
 * The IDLE thread is the thread that the system boots on and, eventually,
 * becomes the IDLE, do nothing task that runs only when there is nothing
 * else to run.  The heap continues from there until the end of memory.
 * g_idle_topstack is a read-only variable the provides this computed
 * address.
 */

void *g_idle_basestack = _END_BSS;
void *g_idle_topstack  = _START_HEAP;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   This function will be called to dynamically set aside the heap region.
 *
 *   - For the normal "flat" build, this function returns the size of the
 *     single heap.
 *   - For the protected build (CONFIG_BUILD_PROTECTED=y) with both kernel-
 *     and user-space heaps (CONFIG_MM_KERNEL_HEAP=y), this function
 *     provides the size of the unprotected, user-space heap.
 *
 *   The following memory map is assumed for the flat build:
 *
 *     .data region.  Size determined at link time.
 *     .bss  region  Size determined at link time.
 *     IDLE thread stack.  Size determined by CONFIG_IDLETHREAD_STACKSIZE.
 *     Heap.  Extends to the end of SRAM.
 *
 *   The following memory map is assumed for the kernel build:
 *
 *     Kernel .data region.  Size determined at link time.
 *     Kernel .bss  region  Size determined at link time.
 *     Kernel IDLE thread stack.  Size determined by CONFIG_IDLETHREAD_STACKSIZE.
 *     Kernel heap.  Size determined by CONFIG_MM_KERNEL_HEAPSIZE.
 *     Padding for alignment
 *     User .data region.  Size determined at link time.
 *     User .bss region  Size determined at link time.
 *     User heap.  Extends to the end of SRAM.
 *
 ****************************************************************************/

void up_allocate_heap(FAR void **heap_start, size_t *heap_size)
{
  int i;

#ifdef CONFIG_BUILD_PROTECTED
  struct mm_heap_s *const *heap =
    (struct mm_heap_s *const *)USERSPACE->us_heap;
  void *const *bssend = (void *const *)USERSPACE->us_bssend;
  void *const *heapend = (void *const *)USERSPACE2->us_heapend;

  for (i = 0; i < CONFIG_ARCH_NR_USER_MEMORY; i++)
    {
      size_t size = heapend[i] - bssend[i];
      if (size != 0)
        {
          mpu_user_data(bssend[i], size);
          up_heap_color(bssend[i], size);
          if (i == CONFIG_ARCH_USER_DEFAULT_HEAP)
            {
              /* Return the default user-space heap settings */

              *heap_start = bssend[i];
              *heap_size  = heapend[i] - bssend[i];

            }
          else
            {
              /* Initialize the additional user-space heap */

              mm_initialize(heap[i], bssend[i], size);
            }
        }
    }
#else
  for (i = 0; i < CONFIG_ARCH_NR_MEMORY; i++)
    {
      size_t size = g_heapend[i] - g_bssend[i];
      if (size != 0)
        {
          mpu_priv_data(g_bssend[i], size);
          up_heap_color(g_bssend[i], size);
          if (i == CONFIG_ARCH_DEFAULT_HEAP)
            {
              /* Return the default heap settings */

              *heap_start = g_bssend[i];
              *heap_size  = g_heapend[i] - g_bssend[i];
            }
          else
            {
              /* Initialize the additional heap */

              mm_initialize(g_mm_heap[i], g_bssend[i], size);
            }
        }
    }
#endif
}

/****************************************************************************
 * Name: up_allocate_kheap
 *
 * Description:
 *   For the kernel build (CONFIG_BUILD_PROTECTED/KERNEL=y) with both kernel-
 *   and user-space heaps (CONFIG_MM_KERNEL_HEAP=y), this function allocates
 *   the kernel-space heap.
 *
 ****************************************************************************/

#ifdef CONFIG_MM_KERNEL_HEAP
void up_allocate_kheap(FAR void **heap_start, size_t *heap_size)
{
  int i;

  for (i = 0; i < CONFIG_ARCH_NR_MEMORY; i++)
    {
      size_t size = g_heapend[i] - g_bssend[i];
      if (size != 0)
        {
          mpu_priv_data(g_bssend[i], size);
          up_heap_color(g_bssend[i], size);
          if (i == CONFIG_ARCH_DEFAULT_HEAP)
            {
              /* Return the default kernel-space heap settings */

              *heap_start = g_bssend[i];
              *heap_size  = g_heapend[i] - g_bssend[i];

              DEBUGASSERT(*heap_size >= CONFIG_MM_KERNEL_HEAPSIZE);
            }
          else
            {
              /* Initialize the additional kernel-space heap */

              mm_initialize(g_mm_heap[i], g_bssend[i], size);
            }
        }
    }
}
#endif
