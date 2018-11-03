/****************************************************************************
 * drivers/mtd/song_onchip_flash.c
 *
 *   Copyright (C) 2017 Pinecone Inc. All rights reserved.
 *   Author: Zhuyanlin<zhuyanlin@pinecone.net>
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

#include <nuttx/arch.h>
#include <nuttx/clk/clk.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mtd/song_onchip_flash.h>

#include <string.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

/* flash controller Registers *******************************************************/
#define INT_RAW               0x28
#define INT_EN                0x2c
#define INT_MASK              0x30
#define INT_STATUS            0x34
#define PRIO_CTRL             0x3c
#define OP_START              0x44
#define XADDR                 0x48
#define YADDR                 0x4c
#define DIN0                  0x58
#define DIN1                  0x5c
#define DIN2                  0x60
#define DIN3                  0x64
#define CPU0_ACCESS_MEM_SIZE  0x6c
#define CPU1_ACCESS_MEM_SIZE  0x70
#define CPU2_ACCESS_MEM_SIZE  0x74
#define CTRL_STATE            0x78

/* Identifies the flash write priority */
#define PRIO_WR_LOW           (1 << 1)

/* Identifies the flash operation commands */
#define CMD_WRITE_PREPARE     (1 << 0)
#define CMD_WRITE             (1 << 1)
#define CMD_WRITE_END         (1 << 2)
#define CMD_ERASE             (1 << 3)
#define CMD_MAS_ERASE         (1 << 4)

/* Identifies the flash smallest block write unit */
#define BLOCK_SIZE            B2C(16)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct song_onchip_flash_dev_s
{
  struct mtd_dev_s mtd;
  FAR const struct song_onchip_flash_config_s *cfg;
};

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

static inline uint32_t flash_regread(FAR struct song_onchip_flash_dev_s *priv, uint32_t offset);
static inline void flash_regwrite(FAR struct song_onchip_flash_dev_s *priv,
                          uint32_t offset, uint32_t val);
static void flash_timing_configure(FAR struct song_onchip_flash_dev_s *priv,
                          FAR const struct song_onchip_flash_timing_s *timing);
static void flash_int_configure(FAR struct song_onchip_flash_dev_s *priv);
static void flash_sendop_wait(FAR struct song_onchip_flash_dev_s *priv,
                          uint32_t opt_cmd);

/* MTD driver methods */
static int song_onchip_flash_mas_erase(FAR struct mtd_dev_s *dev);
static int song_onchip_flash_erase(FAR struct mtd_dev_s *dev, off_t startblock, size_t nblocks);
static ssize_t song_onchip_flash_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                          size_t nblocks, FAR uint8_t *buf);
static ssize_t song_onchip_flash_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                          size_t nblocks, FAR const uint8_t *buf);
static ssize_t song_onchip_flash_read(FAR struct mtd_dev_s *dev, off_t offset, size_t nbytes,
                          FAR uint8_t *buffer);
static int song_onchip_flash_ioctl(FAR struct mtd_dev_s *dev, int cmd, unsigned long arg);

/************************************************************************************
 * Private Functions
 ************************************************************************************/

static inline volatile uint32_t flash_regread(FAR struct song_onchip_flash_dev_s *priv,
                          uint32_t offset)
{
  return (*(volatile uint32_t *)(priv->cfg->base + B2C(offset)));
}

static inline void flash_regwrite(FAR struct song_onchip_flash_dev_s *priv,
                          uint32_t offset, uint32_t val)
{
  *(volatile uint32_t *)(priv->cfg->base + B2C(offset)) = val;
}

static void flash_int_configure(FAR struct song_onchip_flash_dev_s *priv)
{
  uint32_t value;

  value = CMD_WRITE_PREPARE |
          CMD_WRITE |
          CMD_WRITE_END |
          CMD_ERASE |
          CMD_MAS_ERASE;

  /* enable write, erase interrupt */
  flash_regwrite(priv, INT_EN, value);
  /* mask write, erase interrupt, use polling */
  flash_regwrite(priv, INT_MASK, value);
}

static void flash_timing_configure(FAR struct song_onchip_flash_dev_s *priv,
                          FAR const struct song_onchip_flash_timing_s *timing)
{
  while (timing->offset != -1)
    {
      flash_regwrite(priv, timing->offset, timing->value);
      timing++;
    }
}

__ramfunc__ static void flash_sendop_wait(FAR struct song_onchip_flash_dev_s *priv,
                          uint32_t opt_cmd)
{
  /* send operation cmd to flash controller */
  flash_regwrite(priv, OP_START, opt_cmd);

  while (!(flash_regread(priv, INT_RAW) & opt_cmd));
  flash_regwrite(priv, INT_STATUS, opt_cmd);
}

__ramfunc__ static int song_onchip_flash_mas_erase(FAR struct mtd_dev_s *dev)
{
  FAR struct song_onchip_flash_dev_s *priv = (FAR struct song_onchip_flash_dev_s *)dev;
  irqstate_t flags;

  flags = enter_critical_section();
  flash_sendop_wait(priv, CMD_MAS_ERASE);
  leave_critical_section(flags);
  up_invalidate_icache_all();

  return 0;
}

__ramfunc__ static int song_onchip_flash_erase(FAR struct mtd_dev_s *dev,
                          off_t startblock, size_t nblocks)
{
  FAR struct song_onchip_flash_dev_s *priv = (FAR struct song_onchip_flash_dev_s *)dev;
  FAR const struct song_onchip_flash_config_s *cfg = priv->cfg;
  uint32_t erasesize = (1 << (cfg->xaddr_shift + cfg->yaddr_shift)) * BLOCK_SIZE;
  irqstate_t flags;
  size_t i;

  for (i = 0; i < nblocks; i++)
    {
      flags = enter_critical_section();
      flash_regwrite(priv, XADDR, (startblock + i) << cfg->xaddr_shift);
      flash_sendop_wait(priv, CMD_ERASE);
      leave_critical_section(flags);
    }

  up_invalidate_icache(startblock * erasesize,
      (startblock + nblocks) * erasesize);

  return nblocks;
}

__ramfunc__ static ssize_t song_onchip_flash_read(FAR struct mtd_dev_s *dev,
                          off_t offset, size_t nbytes, FAR uint8_t *buffer)
{
  FAR struct song_onchip_flash_dev_s *priv = (FAR struct song_onchip_flash_dev_s *)dev;
  FAR const struct song_onchip_flash_config_s *cfg = priv->cfg;

  memcpy(buffer, (void *)(cfg->cpu_base + offset), nbytes);

  return nbytes;
}

__ramfunc__ static ssize_t song_onchip_flash_bread(FAR struct mtd_dev_s *dev,
                          off_t startblock, size_t nblocks, FAR uint8_t *buf)
{
  FAR struct song_onchip_flash_dev_s *priv = (FAR struct song_onchip_flash_dev_s *)dev;
  FAR const struct song_onchip_flash_config_s *cfg = priv->cfg;

  memcpy(buf, (void *)(cfg->cpu_base + startblock * BLOCK_SIZE),
        nblocks * BLOCK_SIZE);

  return nblocks;
}

__ramfunc__ static ssize_t song_onchip_flash_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                          size_t nblocks, FAR const uint8_t *buf)
{
  FAR struct song_onchip_flash_dev_s *priv = (FAR struct song_onchip_flash_dev_s *)dev;
  FAR const struct song_onchip_flash_config_s *cfg = priv->cfg;
  uint32_t xaddr, yaddr, yaddr_mask = (1 << cfg->yaddr_shift) - 1;
  uint32_t *ptr = (uint32_t *)buf;
  size_t i, remain = nblocks;
  irqstate_t flags;

  while (remain > 0)
    {
      xaddr = startblock >> cfg->yaddr_shift;
      yaddr = startblock & yaddr_mask;

      flags = enter_critical_section();

      flash_regwrite(priv, DIN0, 0xffffffff);
      flash_regwrite(priv, DIN1, 0xffffffff);
      flash_regwrite(priv, DIN2, 0xffffffff);
      flash_regwrite(priv, DIN3, 0xffffffff);

      flash_regwrite(priv, XADDR, xaddr);
      flash_regwrite(priv, YADDR, yaddr);
      flash_sendop_wait(priv, CMD_WRITE_PREPARE);

      for (i = 0; i < BLOCK_SIZE/4; i++)
        {
          flash_regwrite(priv, DIN0 + i * 4, *ptr++);
          flash_sendop_wait(priv, CMD_WRITE);

          flash_regwrite(priv, DIN0 + i * 4, 0xffffffff);
        }

      flash_sendop_wait(priv, CMD_WRITE_END);
      leave_critical_section(flags);

      startblock++;
      remain--;
    }

  up_invalidate_icache(startblock * BLOCK_SIZE,
      (startblock + nblocks) * BLOCK_SIZE);

  return nblocks - remain;
}

__ramfunc__ static int song_onchip_flash_ioctl(FAR struct mtd_dev_s *dev, int cmd, unsigned long arg)
{
  FAR struct song_onchip_flash_dev_s *priv = (FAR struct song_onchip_flash_dev_s *)dev;
  int ret = OK;

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          FAR struct mtd_geometry_s *geo = (FAR struct mtd_geometry_s *)((uintptr_t)arg);
          if (geo)
            {
              FAR const struct song_onchip_flash_config_s *cfg = priv->cfg;

              geo->blocksize    = BLOCK_SIZE;
              geo->erasesize    = (1 << (cfg->xaddr_shift + cfg->yaddr_shift)) * BLOCK_SIZE;
              geo->neraseblocks = cfg->neraseblocks;
            }
        }
        break;

      case MTDIOC_BULKERASE:
        {
            /* Erase the entire device */
          ret = song_onchip_flash_mas_erase(dev);
        }
        break;

      default:
        ret = -ENOTTY; /* Bad command */
        break;
    }

  return ret;
}

/************************************************************************************
 * Public Functions
 ************************************************************************************/

/****************************************************************************
 * Name: song_onchip_flash_initialize
 *
 * Description:
 *  Create and initialize a song on-chip flash controller MTD device instance
 *  that can be used to access the on-chip program memory.
 *
 ****************************************************************************/

FAR struct mtd_dev_s *song_onchip_flash_initialize(FAR const struct song_onchip_flash_config_s *cfg)
{
  FAR struct song_onchip_flash_dev_s *priv;
  struct clk *mclk;

  priv = kmm_zalloc(sizeof(struct song_onchip_flash_dev_s));
  if (priv == NULL)
    {
      return NULL;
    }

  mclk = clk_get(cfg->mclk);
  if (!mclk)
    goto fail;

  if (clk_enable(mclk))
    goto fail;

  if (cfg->rate)
    {
      if (clk_set_rate(mclk, cfg->rate))
        goto fail;
    }

  priv->mtd.erase  = song_onchip_flash_erase;
  priv->mtd.bread  = song_onchip_flash_bread;
  priv->mtd.bwrite = song_onchip_flash_bwrite;
  priv->mtd.read   = song_onchip_flash_read;
  priv->mtd.ioctl  = song_onchip_flash_ioctl;
  priv->mtd.name   = "onchip";

  priv->cfg        = cfg;

  flash_timing_configure(priv, cfg->timing);
  flash_int_configure(priv);

  return (FAR struct mtd_dev_s *)priv;

fail:
  kmm_free(priv);
  return NULL;
}
