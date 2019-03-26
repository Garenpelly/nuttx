/****************************************************************************
 * arch/csky/src/ck802/up_vfork.c
 *
 *   Copyright (C) 2013 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
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

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/sched.h>
#include <nuttx/arch.h>
#include <arch/irq.h>

#include "up_vfork.h"
#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CSKYV2 requires at least a 4-byte stack alignment.
 */

#ifndef CONFIG_STACK_ALIGNMENT
#define CONFIG_STACK_ALIGNMENT 4
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_vfork
 *
 * Description:
 *   The vfork() function has the same effect as fork(), except that the
 *   behavior is undefined if the process created by vfork() either modifies
 *   any data other than a variable of type pid_t used to store the return
 *   value from vfork(), or returns from the function in which vfork() was
 *   called, or calls any other function before successfully calling _exit()
 *   or one of the exec family of functions.
 *
 *   The overall sequence is:
 *
 *   1) User code calls vfork().  vfork() collects context information and
 *      transfers control up up_vfork().
 *   2) up_vfork()and calls nxtask_vforksetup().
 *   3) nxtask_vforksetup() allocates and configures the child task's TCB.  This
 *      consists of:
 *      - Allocation of the child task's TCB.
 *      - Initialization of file descriptors and streams
 *      - Configuration of environment variables
 *      - Setup the input parameters for the task.
 *      - Initialization of the TCB (including call to up_initial_state()
 *   4) up_vfork() provides any additional operating context. up_vfork must:
 *      - Allocate and initialize the stack
 *      - Initialize special values in any CPU registers that were not
 *        already configured by up_initial_state()
 *   5) up_vfork() then calls nxtask_vforkstart()
 *   6) nxtask_vforkstart() then executes the child thread.
 *
 * nxtask_vforkabort() may be called if an error occurs between steps 3 and 6.
 *
 * Input Parameters:
 *   context - Caller context information saved by vfork()
 *
 * Return:
 *   Upon successful completion, vfork() returns 0 to the child process and
 *   returns the process ID of the child process to the parent process.
 *   Otherwise, -1 is returned to the parent, no child process is created,
 *   and errno is set to indicate the error.
 *
 ****************************************************************************/

pid_t up_vfork(const struct vfork_s *context)
{
  struct tcb_s *parent = (FAR struct tcb_s *)g_readytorun.head;
  struct task_tcb_s *child;
  size_t stacksize;
  uint32_t newsp;
  uint32_t stackutil;
  size_t argsize;
  void *argv;
  int ret;

  sinfo("vfork context [%p]:\n", context);
  sinfo("  r4:%08x r5:%08x r6:%08x r7:%08x\n",
        context->r4, context->r5, context->r6, context->r7);
  sinfo("  r8:%08x r9:%08x r10:%08x\n",
        context->r8, context->r9, context->r10);
  sinfo("  r11:%08x sp:%08x lr:%08x\n",
        context->r11, context->sp, context->lr);

  /* Allocate and initialize a TCB for the child task. */

  child = nxtask_vforksetup((start_t)(context->lr & ~1), &argsize);
  if (!child)
    {
      sinfo("ERROR: nxtask_vforksetup failed\n");
      return (pid_t)ERROR;
    }

  sinfo("TCBs: Parent=%p Child=%p\n", parent, child);

  /* Get the size of the parent task's stack.  Due to alignment operations,
   * the adjusted stack size may be smaller than the stack size originally
   * requested.
   */

  stacksize = parent->adj_stack_size + CONFIG_STACK_ALIGNMENT - 1;

  /* Allocate the stack for the TCB */

  ret = up_create_stack((FAR struct tcb_s *)child, stacksize + argsize,
                        parent->flags & TCB_FLAG_TTYPE_MASK);
  if (ret != OK)
    {
      sinfo("ERROR: up_create_stack failed: %d\n", ret);
      nxtask_vforkabort(child, -ret);
      return (pid_t)ERROR;
    }

  /* Allocate the memory and copy argument from parent task */

  argv = up_stack_frame((FAR struct tcb_s *)child, argsize);
  memcpy(argv, parent->adj_stack_ptr, argsize);

  /* How much of the parent's stack was utilized?  The CSKY uses
   * a push-down stack so that the current stack pointer should
   * be lower than the initial, adjusted stack pointer.  The
   * stack usage should be the difference between those two.
   */

  DEBUGASSERT((uint32_t)parent->adj_stack_ptr > context->sp);
  stackutil = (uint32_t)parent->adj_stack_ptr - context->sp;

  sinfo("Parent: stacksize:%d stackutil:%d\n", stacksize, stackutil);

  /* Make some feeble effort to preserve the stack contents.  This is
   * feeble because the stack surely contains invalid pointers and other
   * content that will not work in the child context.  However, if the
   * user follows all of the caveats of vfork() usage, even this feeble
   * effort is overkill.
   */

  newsp = (uint32_t)child->cmn.adj_stack_ptr - stackutil;
  memcpy((void *)newsp, (const void *)context->sp, stackutil);

  /* Update the stack pointer, frame pointer, and volatile registers.  When
   * the child TCB was initialized, all of the values were set to zero.
   * up_initial_state() altered a few values, but the return value in R0
   * should be cleared to zero, providing the indication to the newly started
   * child thread.
   */

  child->cmn.xcp.regs[REG_R4]  = context->r4;  /* Volatile register r4 */
  child->cmn.xcp.regs[REG_R5]  = context->r5;  /* Volatile register r5 */
  child->cmn.xcp.regs[REG_R6]  = context->r6;  /* Volatile register r6 */
  child->cmn.xcp.regs[REG_R7]  = context->r7;  /* Volatile register r7 */
  child->cmn.xcp.regs[REG_R8]  = context->r8;  /* Volatile register r8 */
  child->cmn.xcp.regs[REG_R9]  = context->r9;  /* Volatile register r9 */
  child->cmn.xcp.regs[REG_R10] = context->r10; /* Volatile register r10 */
  child->cmn.xcp.regs[REG_R11] = context->r11; /* Volatile register r11 */
  child->cmn.xcp.regs[REG_SP]  = newsp;        /* Stack pointer */

  /* And, finally, start the child task.  On a failure, nxtask_vforkstart()
   * will discard the TCB by calling nxtask_vforkabort().
   */

  return nxtask_vforkstart(child);
}
