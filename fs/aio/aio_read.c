/****************************************************************************
 * fs/aio/aio_read.c
 *
 *   Copyright (C) 2014 Gregory Nutt. All rights reserved.
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

#include <unistd.h>
#include <sched.h>
#include <aio.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/net/net.h>

#include "aio/aio.h"

#ifdef CONFIG_FS_AIO

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: aio_read_worker
 *
 * Description:
 *   This function executes on the worker thread and performs the
 *   asynchronous I/O operation.
 *
 * Input Parameters:
 *   arg - Worker argument.  In this case, a pointer to an instance of
 *     struct aiocb cast to void *.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void aio_read_worker(FAR void *arg)
{
  FAR struct aio_container_s *aioc = (FAR struct aio_container_s *)arg;
  FAR struct aiocb *aiocbp;
  pid_t pid;
#ifdef CONFIG_PRIORITY_INHERITANCE
  uint8_t prio;
#endif
  ssize_t nread = 0;

  /* Get the information from the container, decant the AIO control block,
   * and free the container before starting any I/O.  That will minimize
   * the delays by any other threads waiting for a pre-allocated container.
   */

  DEBUGASSERT(aioc && aioc->aioc_aiocbp);
  pid    = aioc->aioc_pid;
#ifdef CONFIG_PRIORITY_INHERITANCE
  prio   = aioc->aioc_prio;
#endif
  aiocbp = aioc_decant(aioc);

#if defined(AIO_HAVE_FILEP) && defined(AIO_HAVE_PSOCK)
  if (aiocbp->aio_fildes < CONFIG_NFILE_DESCRIPTORS)
#endif
#ifdef AIO_HAVE_FILEP
    {
      /* Perform the file read using:
       *
       *   u.aioc_filep - File structure pointer
       *   aio_buf      - Location of buffer
       *   aio_nbytes   - Length of transfer
       *   aio_offset   - File offset
       */

     nread = file_pread(aioc->u.aioc_filep, (FAR void *)aiocbp->aio_buf,
                        aiocbp->aio_nbytes, aiocbp->aio_offset);
    }
#endif
#if defined(AIO_HAVE_FILEP) && defined(AIO_HAVE_PSOCK)
  else
#endif
#ifdef AIO_HAVE_PSOCK
    {
      /* Perform the socket receive using:
       *
       *   u.aioc_psock - Socket structure pointer
       *   aio_buf      - Location of buffer
       *   aio_nbytes   - Length of transfer
       */

      nread = psock_recv(aioc->u.aioc_psock, (FAR void *)aiocbp->aio_buf,
                         aiocbp->aio_nbytes, 0);
    }
#endif

  /* Set the result of the read operation. */

#ifdef CONFIG_DEBUG_FS_ERROR
  if (nread < 0)
    {
      ferr("ERROR: read failed: %d\n", (int)nread);
    }
#endif

  aiocbp->aio_result = nread;

  /* Signal the client */

  (void)aio_signal(pid, aiocbp);

#ifdef CONFIG_PRIORITY_INHERITANCE
  /* Restore the low priority worker thread default priority */

  lpwork_restorepriority(prio);
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: aio_read
 *
 * Description:
 *   The aio_read() function reads aiocbp->aio_nbytes from the file
 *   associated with aiocbp->aio_fildes into the buffer pointed to by
 *   aiocbp->aio_buf. The function call will return when the read request
 *   has been initiated or queued to the file or device (even when the data
 *   cannot be delivered immediately).
 *
 *   If prioritized I/O is supported for this file, then the asynchronous
 *   operation will be submitted at a priority equal to a base scheduling
 *   priority minus aiocbp->aio_reqprio. If Thread Execution Scheduling is
 *   not supported, then the base scheduling priority is that of the calling
 *   thread (the latter is implemented at present).
 *
 *   The aiocbp value may be used as an argument to aio_error() and
 *   aio_return() in order to determine the error status and return status,
 *   respectively, of the asynchronous operation while it is proceeding. If
 *   an error condition is encountered during queuing, the function call
 *   will return without having initiated or queued the request. The
 *   requested operation takes place at the absolute position in the file as
 *   given by aio_offset, as if lseek() were called immediately prior to the
 *   operation with an offset equal to aio_offset and a whence equal to
 *   SEEK_SET. After a successful call to enqueue an asynchronous I/O
 *   operation, the value of the file offset for the file is unspecified.
 *
 *   The aiocbp->aio_lio_opcode field will be ignored by aio_read().
 *
 *   The aiocbp argument points to an aiocb structure. If the buffer pointed
 *   to by aiocbp->aio_buf or the control block pointed to by aiocbp becomes
 *   an illegal address prior to asynchronous I/O completion, then the
 *   behavior is undefined.
 *
 *   Simultaneous asynchronous operations using the same aiocbp produce
 *   undefined results.
 *
 *   For any system action that changes the process memory space while an
 *   synchronous I/O is outstanding to the address range being changed, the
 *   result of that action is undefined.
 *
 * Input Parameters:
 *   aiocbp - A pointer to an instance of struct aiocb
 *
 * Returned Value:
 *
 *   The aio_read() function will return the value zero if the I/O operation
 *   is successfully queued; otherwise, the function will return the value
 *   -1 and set errno to indicate the error.  The aio_read() function will
 *   ail if:
 *
 *   EAGAIN - The requested asynchronous I/O operation was not queued due to
 *     system resource limitations.
 *
 *   Each of the following conditions may be detected synchronously at the
 *   time of the call to aio_read(), or asynchronously. If any of the
 *   conditions below are detected synchronously, the aio_read() function
 *   will return -1 and set errno to the corresponding value. If any of the
 *   conditions below are detected asynchronously, the return status of the
 *   asynchronous operation is set to -1, and the error status of the
 *   asynchronous operation is set to the corresponding value.
 *
 *   EBADF - The aiocbp->aio_fildes argument is not a valid file descriptor
 *     open for reading.
 *   EINVAL - The file offset value implied by aiocbp->aio_offset would be
 *     invalid, aiocbp->aio_reqprio is not a valid value, or
 *     aiocbp->aio_nbytes is an invalid value.
 *
 *   In the case that the aio_read() successfully queues the I/O operation
 *   but the operation is subsequently canceled or encounters an error, the
 *   return status of the asynchronous operation is one of the values
 *   normally returned by the read() function call. In addition, the error
 *   status of the asynchronous operation is set to one of the error
 *   statuses normally set by the read() function call, or one of the
 *   following values:
 *
 *   EBADF - The aiocbp->aio_fildes argument is not a valid file descriptor
 *     open for reading.
 *   ECANCELED - The requested I/O was canceled before the I/O completed
 *     due to an explicit aio_cancel() request.
 *   EINVAL - The file offset value implied by aiocbp->aio_offset would be
 *     invalid.
 *
 * The following condition may be detected synchronously or asynchronously:
 *
 *   EOVERFLOW - The file is a regular file, aiobcp->aio_nbytes is greater
 *     than 0, and the starting offset in aiobcp->aio_offset is before the
 *     end-of-file and is at or beyond the offset maximum in the open file
 *     description associated with aiocbp->aio_fildes.
 *
 * POSIX Compliance:
 * - Most errors required in the standard are not detected at this point.
 *   There are no pre-queuing checks for the validity of the operation.
 *
 ****************************************************************************/

int aio_read(FAR struct aiocb *aiocbp)
{
  FAR struct aio_container_s *aioc;
  int ret;

  DEBUGASSERT(aiocbp);

  /* The result -EINPROGRESS means that the transfer has not yet completed */

  aiocbp->aio_result = -EINPROGRESS;
  aiocbp->aio_priv   = NULL;

  /* Create a container for the AIO control block.  This may cause us to
   * block if there are insufficient resources to satisfy the request.
   */

  aioc = aio_contain(aiocbp);
  if (!aioc)
    {
      /* The errno has already been set (probably EBADF) */

      aiocbp->aio_result = -get_errno();
      return ERROR;
    }

  /* Defer the work to the worker thread */

  ret = aio_queue(aioc, aio_read_worker);
  if (ret < 0)
    {
      /* The result and the errno have already been set */

      aioc_decant(aioc);
      return ERROR;
    }

  return OK;
}

#endif /* CONFIG_FS_AIO */
