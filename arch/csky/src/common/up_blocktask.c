/************************************************************************************
 *arch/csky/src/common/up_blocktask.c
 *
 * Copyright (C) 2015 The YunOS Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <sched.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/clock.h>

#include "sched/sched.h"
#include "group/group.h"
#include "up_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_block_task
 *
 * Description:
 *   The currently executing task at the head of
 *   the ready to run list must be stopped.  Save its context
 *   and move it to the inactive list specified by task_state.
 *
 * Inputs:
 *   tcb: Refers to a task in the ready-to-run list (normally
 *     the task at the head of the list).  It most be
 *     stopped, its context saved and moved into one of the
 *     waiting task lists.  It it was the task at the head
 *     of the ready-to-run list, then a context to the new
 *     ready to run task must be performed.
 *   task_state: Specifies which waiting task list should be
 *     hold the blocked task TCB.
 *
 ****************************************************************************/

void up_block_task(struct tcb_s *tcb, tstate_t task_state)
{
    struct tcb_s *rtcb = (struct tcb_s *)g_readytorun.head;
    bool switch_needed;

    /* Verify that the context switch can be performed */

    ASSERT((tcb->task_state >= FIRST_READY_TO_RUN_STATE) &&
           (tcb->task_state <= LAST_READY_TO_RUN_STATE));

    /* Remove the tcb task from the ready-to-run list.  If we
     * are blocking the task at the head of the task list (the
     * most likely case), then a context switch to the next
     * ready-to-run task is needed. In this case, it should
     * also be true that rtcb == tcb.
     */

    switch_needed = sched_removereadytorun(tcb);

    /* Add the task to the specified blocked task list */

    sched_addblocked(tcb, (tstate_t)task_state);

    /* If there are any pending tasks, then add them to the g_readytorun
     * task list now
     */

    if (g_pendingtasks.head) {
        switch_needed |= sched_mergepending();
    }

    /* Now, perform the context switch if one is needed */

    if (switch_needed) {
        /* Update scheduler parameters */

        sched_suspend_scheduler(rtcb);

        /* Are we in an interrupt handler? */

        if (current_regs) {
            /* Yes, then we have to do things differently.
             * Just copy the current_regs into the OLD rtcb.
             */

            up_savestate(rtcb->xcp.regs);

            /* Restore the exception context of the rtcb at the (new) head
             * of the g_readytorun task list.
             */

            rtcb = (struct tcb_s *)g_readytorun.head;

            /* Reset scheduler parameters */
            sched_resume_scheduler(rtcb);

            /* Then switch contexts.  Any necessary address environment
             * changes will be made when the interrupt returns.
             */

            up_restorestate(rtcb->xcp.regs);
        }

        /* Copy the user C context into the TCB at the (old) head of the
         * g_readytorun Task list. if up_saveusercontext returns a non-zero
         * value, then this is really the previously running task restarting!
         */

        else if (!up_saveusercontext(rtcb->xcp.regs)) {
            /* Restore the exception context of the rtcb at the (new) head
             * of the g_readytorun task list.
             */

            rtcb = (struct tcb_s *)g_readytorun.head;

#ifdef CONFIG_ARCH_ADDRENV
            /* Make sure that the address environment for the previously
             * running task is closed down gracefully (data caches dump,
             * MMU flushed) and set up the address environment for the new
             * thread at the head of the ready-to-run list.
             */

            (void)group_addrenv(rtcb);
#endif

            /* Reset scheduler parameters */
            sched_resume_scheduler(rtcb);

            /* Then switch contexts */

            up_fullcontextrestore(rtcb->xcp.regs);
        }
    }
}
