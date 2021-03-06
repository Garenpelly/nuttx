/****************************************************************************
 * drivers/clk/song/clk-pll-lf.c
 *
 *   Copyright (C) 2017 Pinecone Inc. All rights reserved.
 *   Author: zhuyanlin<zhuyanlin@pinecone.net>
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

#include <nuttx/clk/clk-provider.h>

#include <debug.h>

#include "song-clk.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PLL_FBDIV_SHIFT     0
#define PLL_FBDIV_MASK      0x3FFF
#define PLL_POSTDIV_SHIFT   16
#define PLL_POSTDIV_MASK    0xF
#define PLL_VCODIVSEL_SHIFT 20

/* pll ic require freq*fbdiv in 15M~90M */
#define PLL_FBDIV_MIN_FREQ  15000000
#define PLL_FBDIV_MAX_FREQ  90000000

#define to_clk_pll_lf(_clk) (struct clk_pll_lf *)(_clk->private_data)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t clk_pll_lf_recalc_rate(struct clk *clk, uint32_t parent_rate)
{
  uint32_t val;
  uint32_t fbdiv, postdiv;
  struct clk_pll_lf *pll = to_clk_pll_lf(clk);

  val     = clk_read(pll->cfg0_reg);
  fbdiv   = (val >> PLL_FBDIV_SHIFT) & PLL_FBDIV_MASK;
  postdiv = (val >> PLL_POSTDIV_SHIFT) & PLL_POSTDIV_MASK;

  return ((uint64_t)parent_rate * fbdiv) / (postdiv + 1);
}

static uint32_t clk_pll_lf_round_rate(struct clk *clk, uint32_t rate,
                                      uint32_t *best_parent_rate)
{
  uint32_t fbdiv, postdiv = 1;

  fbdiv = *best_parent_rate / rate;

  while (rate * fbdiv < PLL_FBDIV_MIN_FREQ)
    {
      fbdiv *= 2;
      postdiv *= 2;
    }

  if (rate * fbdiv > PLL_FBDIV_MAX_FREQ)
    {
      fbdiv = PLL_FBDIV_MAX_FREQ / rate;
    }

  return ((uint64_t)*best_parent_rate * fbdiv) / postdiv;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct clk_ops clk_pll_lf_ops =
{
  .recalc_rate = clk_pll_lf_recalc_rate,
  .round_rate = clk_pll_lf_round_rate,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct clk *clk_register_pll_lf(const char *name, const char *parent_name,
                                uint8_t flags, uint32_t cfg0_reg, uint32_t cfg1_reg)
{
  struct clk_pll_lf pll;
  const char **parent_names;
  uint8_t num_parents;

  parent_names = parent_name ? &parent_name : NULL;
  num_parents = parent_name ? 1 : 0;

  pll.cfg0_reg = cfg0_reg;
  pll.cfg1_reg = cfg1_reg;

  return clk_register(name, parent_names, num_parents, flags,
                      &clk_pll_lf_ops, &pll, sizeof(pll));
}
