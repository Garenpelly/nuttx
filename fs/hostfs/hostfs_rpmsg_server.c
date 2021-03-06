/****************************************************************************
 * fs/hostfs/hostfs_rpmsg_server.c
 * Hostfs rpmsg driver on server
 *
 *   Copyright (C) 2017 Pinecone Inc. All rights reserved.
 *   Author: Guiding Li<liguiding@pinecone.net>
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

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/hostfs_rpmsg.h>
#include <nuttx/rptun/openamp.h>

#include "hostfs_rpmsg.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct hostfs_rpmsg_server_s
{
  struct rpmsg_endpoint ept;
  struct file           files[CONFIG_NFILE_DESCRIPTORS];
  void                  *dirs[CONFIG_NFILE_DESCRIPTORS];
  sem_t                 sem;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hostfs_rpmsg_open_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_);
static int hostfs_rpmsg_close_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_);
static int hostfs_rpmsg_read_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_);
static int hostfs_rpmsg_write_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_);
static int hostfs_rpmsg_lseek_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_);
static int hostfs_rpmsg_ioctl_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_);
static int hostfs_rpmsg_sync_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_);
static int hostfs_rpmsg_dup_handler(struct rpmsg_endpoint *ept,
                                    void *data, size_t len,
                                    uint32_t src, void *priv_);
static int hostfs_rpmsg_fstat_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_);
static int hostfs_rpmsg_ftruncate_handler(struct rpmsg_endpoint *ept,
                                          void *data, size_t len,
                                          uint32_t src, void *priv_);
static int hostfs_rpmsg_opendir_handler(struct rpmsg_endpoint *ept,
                                        void *data, size_t len,
                                        uint32_t src, void *priv_);
static int hostfs_rpmsg_readdir_handler(struct rpmsg_endpoint *ept,
                                        void *data, size_t len,
                                        uint32_t src, void *priv_);
static int hostfs_rpmsg_rewinddir_handler(struct rpmsg_endpoint *ept,
                                          void *data, size_t len,
                                          uint32_t src, void *priv_);
static int hostfs_rpmsg_closedir_handler(struct rpmsg_endpoint *ept,
                                         void *data, size_t len,
                                         uint32_t src, void *priv_);
static int hostfs_rpmsg_statfs_handler(struct rpmsg_endpoint *ept,
                                       void *data, size_t len,
                                       uint32_t src, void *priv);
static int hostfs_rpmsg_unlink_handler(struct rpmsg_endpoint *ept,
                                       void *data, size_t len,
                                       uint32_t src, void *priv);
static int hostfs_rpmsg_mkdir_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv);
static int hostfs_rpmsg_rmdir_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv);
static int hostfs_rpmsg_rename_handler(struct rpmsg_endpoint *ept,
                                       void *data, size_t len,
                                       uint32_t src, void *priv);
static int hostfs_rpmsg_stat_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv);

static void hostfs_rpmsg_ns_bind(struct rpmsg_device *rdev, void *priv_,
                                 const char *name, uint32_t dest);
static void hostfs_rpmsg_ns_unbind(struct rpmsg_endpoint *ept);
static int  hostfs_rpmsg_ept_cb(struct rpmsg_endpoint *ept, void *data,
                                size_t len, uint32_t src, void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const rpmsg_ept_cb g_hostfs_rpmsg_handler[] =
{
  [HOSTFS_RPMSG_OPEN]      = hostfs_rpmsg_open_handler,
  [HOSTFS_RPMSG_CLOSE]     = hostfs_rpmsg_close_handler,
  [HOSTFS_RPMSG_READ]      = hostfs_rpmsg_read_handler,
  [HOSTFS_RPMSG_WRITE]     = hostfs_rpmsg_write_handler,
  [HOSTFS_RPMSG_LSEEK]     = hostfs_rpmsg_lseek_handler,
  [HOSTFS_RPMSG_IOCTL]     = hostfs_rpmsg_ioctl_handler,
  [HOSTFS_RPMSG_SYNC]      = hostfs_rpmsg_sync_handler,
  [HOSTFS_RPMSG_DUP]       = hostfs_rpmsg_dup_handler,
  [HOSTFS_RPMSG_FSTAT]     = hostfs_rpmsg_fstat_handler,
  [HOSTFS_RPMSG_FTRUNCATE] = hostfs_rpmsg_ftruncate_handler,
  [HOSTFS_RPMSG_OPENDIR]   = hostfs_rpmsg_opendir_handler,
  [HOSTFS_RPMSG_READDIR]   = hostfs_rpmsg_readdir_handler,
  [HOSTFS_RPMSG_REWINDDIR] = hostfs_rpmsg_rewinddir_handler,
  [HOSTFS_RPMSG_CLOSEDIR]  = hostfs_rpmsg_closedir_handler,
  [HOSTFS_RPMSG_STATFS]    = hostfs_rpmsg_statfs_handler,
  [HOSTFS_RPMSG_UNLINK]    = hostfs_rpmsg_unlink_handler,
  [HOSTFS_RPMSG_MKDIR]     = hostfs_rpmsg_mkdir_handler,
  [HOSTFS_RPMSG_RMDIR]     = hostfs_rpmsg_rmdir_handler,
  [HOSTFS_RPMSG_RENAME]    = hostfs_rpmsg_rename_handler,
  [HOSTFS_RPMSG_STAT]      = hostfs_rpmsg_stat_handler,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int hostfs_rpmsg_open_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_open_s *msg = data;
  int i, ret = -ENOENT;

  nxsem_wait(&priv->sem);
  for (i = 0; i < CONFIG_NFILE_DESCRIPTORS; i++)
    {
      if (!priv->files[i].f_inode)
        {
          ret = file_open(&priv->files[i], msg->pathname, msg->flags, msg->mode);
          if (ret >= 0)
            {
              ret = i;
            }
          break;
        }
    }
  nxsem_post(&priv->sem);

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_close_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_close_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      nxsem_wait(&priv->sem);
      ret = file_close(&priv->files[msg->fd]);
      nxsem_post(&priv->sem);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_read_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_read_s *msg = data;
  struct hostfs_rpmsg_read_s *rsp;
  int ret = -ENOENT;
  uint32_t space;

  rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
  if (!rsp)
    {
      return -ENOMEM;
    }
  *rsp = *msg;

  space -= sizeof(*msg);
  if (space > msg->count)
    {
      space = msg->count;
    }

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_read(&priv->files[msg->fd], rsp->buf, space);
    }

  rsp->header.result = ret;
  return rpmsg_send_nocopy(ept, rsp, (ret < 0 ? 0 : ret) + sizeof(*rsp));
}

static int hostfs_rpmsg_write_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_write_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_write(&priv->files[msg->fd], msg->buf, msg->count);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_lseek_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_lseek_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_seek(&priv->files[msg->fd], msg->offset, msg->whence);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_ioctl_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_ioctl_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_ioctl(&priv->files[msg->fd], msg->request, msg->arg);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_sync_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_sync_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_fsync(&priv->files[msg->fd]);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_dup_handler(struct rpmsg_endpoint *ept,
                                    void *data, size_t len,
                                    uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_dup_s *msg = data;
  int i, ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      nxsem_wait(&priv->sem);
      for (i = 0; i < CONFIG_NFILE_DESCRIPTORS; i++)
        {
          if (!priv->files[i].f_inode)
            {
              ret = file_dup2(&priv->files[msg->fd], &priv->files[i]);
              if (ret >= 0)
                {
                  ret = i;
                }
              break;
            }
        }
      nxsem_post(&priv->sem);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_fstat_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_fstat_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_fstat(&priv->files[msg->fd], &msg->buf);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_ftruncate_handler(struct rpmsg_endpoint *ept,
                                          void *data, size_t len,
                                          uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_ftruncate_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 0 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = file_truncate(&priv->files[msg->fd], msg->length);
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_opendir_handler(struct rpmsg_endpoint *ept,
                                        void *data, size_t len,
                                        uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_opendir_s *msg = data;
  int i, ret = -ENOENT;
  void *dir;

  dir = opendir(msg->pathname);
  if (dir)
    {
      nxsem_wait(&priv->sem);
      for (i = 1; i < CONFIG_NFILE_DESCRIPTORS; i++)
        {
          if (!priv->dirs[i])
            {
              priv->dirs[i] = dir;
              ret = i;
              break;
            }
        }
      nxsem_post(&priv->sem);

      if (ret < 0)
        {
          closedir(dir);
        }
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_readdir_handler(struct rpmsg_endpoint *ept,
                                        void *data, size_t len,
                                        uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_readdir_s *msg = data;
  struct dirent *entry;
  int ret = -ENOENT;

  if (msg->fd >= 1 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      entry = readdir(priv->dirs[msg->fd]);
      if (entry)
        {
          msg->type = entry->d_type;
          strcpy(msg->name, entry->d_name);
          len += strlen(entry->d_name) + 1;
          ret = 0;
        }
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, len);
}

static int hostfs_rpmsg_rewinddir_handler(struct rpmsg_endpoint *ept,
                                          void *data, size_t len,
                                          uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_rewinddir_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 1 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      rewinddir(priv->dirs[msg->fd]);
      ret = 0;
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_closedir_handler(struct rpmsg_endpoint *ept,
                                         void *data, size_t len,
                                         uint32_t src, void *priv_)
{
  struct hostfs_rpmsg_server_s *priv = priv_;
  struct hostfs_rpmsg_closedir_s *msg = data;
  int ret = -ENOENT;

  if (msg->fd >= 1 && msg->fd < CONFIG_NFILE_DESCRIPTORS)
    {
      ret = closedir(priv->dirs[msg->fd]);
      nxsem_wait(&priv->sem);
      priv->dirs[msg->fd] = NULL;
      nxsem_post(&priv->sem);
      ret = ret ? get_errno(ret) : 0;
    }

  msg->header.result = ret;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_statfs_handler(struct rpmsg_endpoint *ept,
                                       void *data, size_t len,
                                       uint32_t src, void *priv)
{
  struct hostfs_rpmsg_statfs_s *msg = data;
  int ret;

  ret = statfs(msg->pathname, &msg->buf);
  msg->header.result = ret ? get_errno(ret) : 0;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_unlink_handler(struct rpmsg_endpoint *ept,
                                       void *data, size_t len,
                                       uint32_t src, void *priv)
{
  struct hostfs_rpmsg_unlink_s *msg = data;
  int ret;

  ret = unlink(msg->pathname);
  msg->header.result = ret ? get_errno(ret) : 0;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_mkdir_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv)
{
  struct hostfs_rpmsg_mkdir_s *msg = data;
  int ret;

  ret = mkdir(msg->pathname, msg->mode);
  msg->header.result = ret ? get_errno(ret) : 0;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_rmdir_handler(struct rpmsg_endpoint *ept,
                                      void *data, size_t len,
                                      uint32_t src, void *priv)
{
  struct hostfs_rpmsg_rmdir_s *msg = data;
  int ret;

  ret = rmdir(msg->pathname);
  msg->header.result = ret ? get_errno(ret) : 0;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_rename_handler(struct rpmsg_endpoint *ept,
                                       void *data, size_t len,
                                       uint32_t src, void *priv)
{
  struct hostfs_rpmsg_rename_s *msg = data;
  char *newpath;
  size_t oldlen;
  int ret;

  oldlen = (strlen(msg->pathname) + 1 + 0x7) & ~0x7;
  newpath = msg->pathname + oldlen;

  ret = rename(msg->pathname, newpath);
  msg->header.result = ret ? get_errno(ret) : 0;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int hostfs_rpmsg_stat_handler(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *priv)
{
  struct hostfs_rpmsg_stat_s *msg = data;
  int ret;

  ret = stat(msg->pathname, &msg->buf);
  msg->header.result = ret ? get_errno(ret) : 0;
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static void hostfs_rpmsg_ns_bind(struct rpmsg_device *rdev, void *priv_,
                                 const char *name, uint32_t dest)
{
  struct hostfs_rpmsg_server_s *priv;
  int ret;

  if (strcmp(name, HOSTFS_RPMSG_EPT_NAME))
    {
      return;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (!priv)
    {
      return;
    }

  priv->ept.priv = priv;
  nxsem_init(&priv->sem, 0, 1);

  ret = rpmsg_create_ept(&priv->ept, rdev, HOSTFS_RPMSG_EPT_NAME,
                         RPMSG_ADDR_ANY, dest,
                         hostfs_rpmsg_ept_cb, hostfs_rpmsg_ns_unbind);
  if (ret)
    {
      nxsem_destroy(&priv->sem);
      kmm_free(priv);
    }
}

static void hostfs_rpmsg_ns_unbind(struct rpmsg_endpoint *ept)
{
  struct hostfs_rpmsg_server_s *priv = ept->priv;
  int i;

  for (i = 0; i < CONFIG_NFILE_DESCRIPTORS; i++)
    {
      if (priv->files[i].f_inode)
        {
          file_close(&priv->files[i]);
        }
    }

  for (i = 0; i < CONFIG_NFILE_DESCRIPTORS; i++)
    {
      if (priv->dirs[i])
        {
          closedir(priv->dirs[i]);
        }
    }

  rpmsg_destroy_ept(&priv->ept);
  nxsem_destroy(&priv->sem);

  kmm_free(priv);
}

static int hostfs_rpmsg_ept_cb(struct rpmsg_endpoint *ept, void *data,
                               size_t len, uint32_t src, void *priv)
{
  struct hostfs_rpmsg_header_s *header = data;
  uint32_t command = header->command;

  if (command < ARRAY_SIZE(g_hostfs_rpmsg_handler))
    {
      return g_hostfs_rpmsg_handler[command](ept, data, len, src, priv);
    }

  return -EINVAL;
}

int hostfs_rpmsg_server_init(void)
{
  return rpmsg_register_callback(NULL,
                                 NULL,
                                 NULL,
                                 hostfs_rpmsg_ns_bind);
}
