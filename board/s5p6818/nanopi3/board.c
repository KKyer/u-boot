/*
 * Copyright (C) Guangzhou FriendlyARM Computer Tech. Co., Ltd.
 * (http://www.friendlyarm.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 */

#include <config.h>
#include <common.h>
#ifdef CONFIG_PWM_NX
#include <pwm.h>
#endif
#include <asm/io.h>

#include <asm/arch/nexell.h>
#include <asm/arch/nx_gpio.h>
#include <asm/arch/display.h>
#include <asm/arch/display_dev.h>

#include "hwrev.h"
#include "onewire.h"
#include "nxp-fb.h"

DECLARE_GLOBAL_DATA_PTR;

enum gpio_group {
	gpio_a, gpio_b, gpio_c, gpio_d, gpio_e,
};

#ifdef CONFIG_PWM_NX
struct pwm_device {
	int grp;
	int bit;
	int io_fn;
};

static inline void bd_pwm_config_gpio(int ch)
{
	struct pwm_device pwm_dev[] = {
		[0] = { .grp = gpio_d, .bit = 1,  .io_fn = 0 },
		[1] = { .grp = gpio_c, .bit = 13, .io_fn = 1 },
		[2] = { .grp = gpio_c, .bit = 14, .io_fn = 1 },
		[3] = { .grp = gpio_d, .bit = 0,  .io_fn = 0 },
	};

	int gp = pwm_dev[ch].grp;
	int io = pwm_dev[ch].bit;

	/* pwm backlight OFF: HIGH, ON: LOW */
	nx_gpio_set_pad_function(gp, io, pwm_dev[ch].io_fn);
	nx_gpio_set_output_value(gp, io, 1);
	nx_gpio_set_output_enable(gp, io, 1);
}
#endif

static void bd_backlight_off(void)
{
#ifdef CONFIG_ONEWIRE
	onewire_set_backlight(0);

#elif defined(CONFIG_BACKLIGHT_CH)
	bd_pwm_config_gpio(CONFIG_BACKLIGHT_CH);
#endif
}

static void bd_backlight_on(void)
{
#ifdef CONFIG_ONEWIRE
	onewire_set_backlight(127);

#elif defined(CONFIG_BACKLIGHT_CH)
	/* pwm backlight ON: HIGH, ON: LOW */
	pwm_init(CONFIG_BACKLIGHT_CH,
		CONFIG_BACKLIGHT_DIV, CONFIG_BACKLIGHT_INV);
	pwm_config(CONFIG_BACKLIGHT_CH,
		TO_DUTY_NS(CONFIG_BACKLIGHT_DUTY, CONFIG_BACKLIGHT_HZ),
		TO_PERIOD_NS(CONFIG_BACKLIGHT_HZ));
#endif
}

static void bd_lcd_config_gpio(void)
{
	int i;

	for (i = 0; i < 28; i++) {
		nx_gpio_set_pad_function(gpio_a, i, 1);
		nx_gpio_set_drive_strength(gpio_a, i, 0);
		nx_gpio_set_pull_mode(gpio_a, i, 2);
	}

	nx_gpio_set_drive_strength(gpio_a, 0, 1);
}

/* DEFAULT mmc dev for eMMC boot (dwmmc.2) */
static int mmc_boot_dev = 0;

int board_mmc_bootdev(void)
{
	return mmc_boot_dev;
}

/* call from common/env_mmc.c */
int mmc_get_env_dev(void)
{
	return mmc_boot_dev;
}

#ifdef CONFIG_DISPLAY_BOARDINFO
int checkboard(void)
{
	printf("Board: %s\n", get_board_name());

	return 0;
}
#endif

int nx_display_fixup_dp(struct nx_display_dev *dp)
{
	struct nxp_lcd *lcd = bd_get_lcd();
	enum lcd_format fmt = bd_get_lcd_format();
	struct nxp_lcd_timing *timing = &lcd->timing;
	struct dp_sync_info *sync = &dp->sync;
	struct dp_plane_info *plane = &dp->planes[0];
	u32 clk = 800000000;
	u32 div;

	sync->h_active_len = lcd->width;
	sync->h_sync_width = timing->h_sw;
	sync->h_back_porch = timing->h_bp;
	sync->h_front_porch = timing->h_fp;
	sync->h_sync_invert = !lcd->polarity.inv_hsync;

	sync->v_active_len = lcd->height;
	sync->v_sync_width = timing->v_sw;
	sync->v_back_porch = timing->v_bp;
	sync->v_front_porch = timing->v_fp;
	sync->v_sync_invert = !lcd->polarity.inv_vsync;

	/* calculates pixel clock */
	div  = timing->h_sw + timing->h_bp + timing->h_fp + lcd->width;
	div *= timing->v_sw + timing->v_bp + timing->v_fp + lcd->height;
	div *= lcd->freq ? : 60;
	clk /= div;

	dp->ctrl.clk_div_lv0 = clk;

	dp->top.screen_width = lcd->width;
	dp->top.screen_height = lcd->height;

	plane->width = lcd->width;
	plane->height = lcd->height;

	/* initialize display device type */
	if (fmt == LCD_RGB) {
		dp->dev_type = DP_DEVICE_RGBLCD;

	} else if (fmt == LCD_HDMI) {
		struct dp_hdmi_dev *dev = (struct dp_hdmi_dev *) dp->device;

		dp->dev_type = DP_DEVICE_HDMI;
		if (lcd->width == 1920 && lcd->height == 1080)
			dev->preset = 1;
		else
			dev->preset = 0;

	} else {
		struct dp_lvds_dev *dev = (struct dp_lvds_dev *) dp->device;

		dp->dev_type = DP_DEVICE_LVDS;
		dev->lvds_format = (fmt & 0x3);
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * intialize board status.
 */

#define	MMC_BOOT_CH0		(0)
#define	MMC_BOOT_CH1		(1 <<  3)
#define	MMC_BOOT_CH2		(1 << 19)

static void bd_bootdev_init(void)
{
	unsigned int rst = readl(PHY_BASEADDR_CLKPWR + SYSRSTCONFIG);

	rst &= (1 << 19) | (1 << 3);
	if (rst == MMC_BOOT_CH0) {
		/* mmc dev 1 for SD boot */
		mmc_boot_dev = 1;
	}
}

static void bd_onewire_init(void)
{
	unsigned char lcd;
	unsigned short fw_ver;

	onewire_init();
	onewire_get_info(&lcd, &fw_ver);
}

static void bd_lcd_init(void)
{
	struct nxp_lcd *cfg;
	int id;
	int ret;

	id = onewire_get_lcd_id();
	/* -1: onwire probe failed
	 *  0: bad
	 * >0: identified */

	ret = bd_setup_lcd_by_id(id);
	if (id <= 0 || ret != id) {
		printf("Panel: N/A (%d)\n", id);
		bd_setup_lcd_by_name("HDMI720P60");

	} else {
		printf("Panel: %s\n", bd_get_lcd_name());

		cfg = bd_get_lcd();
		if (cfg->gpio_init)
			cfg->gpio_init();
	}
}

#ifdef CONFIG_REVISION_TAG
static void set_board_rev(void)
{
	char info[64] = {0, };

	snprintf(info, ARRAY_SIZE(info), "%02x", get_board_rev());
	setenv("board_rev", info);
}
#endif

static void set_dtb_name(void)
{
	char info[64] = {0, };

	snprintf(info, ARRAY_SIZE(info),
			"s5p6818-nanopi3-rev%02x.dtb", get_board_rev());
	setenv("dtb_name", info);
}

static void bd_update_env(void)
{
	char *lcdtype = getenv("lcdtype");
	char *lcddpi = getenv("lcddpi");
	char *bootargs = getenv("bootargs");
	const char *name;
	char *p = NULL;
	int rootdev = board_mmc_bootdev();
	int need_save = 0;

#define CMDLINE_LCD		" lcd="
	char cmdline[CONFIG_SYS_CBSIZE];
	int n = 1;

	if (rootdev != CONFIG_ROOT_DEV && !getenv("firstboot")) {
		setenv_ulong("rootdev", rootdev);
		setenv("firstboot", "0");
		need_save = 1;
	}

	if (lcdtype) {
		/* Setup again as user specified LCD in env */
		bd_setup_lcd_by_name(lcdtype);
	}

	name = bd_get_lcd_name();

	if (bootargs)
		n = strlen(bootargs);	/* isn't 0 for NULL */
	else
		cmdline[0] = '\0';

	if ((n + strlen(name) + sizeof(CMDLINE_LCD)) > sizeof(cmdline)) {
		printf("Error: `bootargs' is too large (%d)\n", n);
		goto __exit;
	}

	if (bootargs) {
		p = strstr(bootargs, CMDLINE_LCD);
		if (p) {
			n = (p - bootargs);
			p += strlen(CMDLINE_LCD);
		}
		strncpy(cmdline, bootargs, n);
	}

	/* add `lcd=NAME,NUMdpi' */
	strncpy(cmdline + n, CMDLINE_LCD, strlen(CMDLINE_LCD));
	n += strlen(CMDLINE_LCD);

	strcpy(cmdline + n, name);
	n += strlen(name);

	if (lcddpi) {
		n += sprintf(cmdline + n, ",%sdpi", lcddpi);
	} else {
		int dpi = bd_get_lcd_density();

		if (dpi > 0 && dpi < 600) {
			n += sprintf(cmdline + n, ",%ddpi", dpi);
		}
	}

	/* copy remaining of bootargs */
	if (p) {
		p = strstr(p, " ");
		if (p) {
			strcpy(cmdline + n, p);
			n += strlen(p);
		}
	}

	/* append `bootdev=2' */
#define CMDLINE_BDEV	" bootdev="
	if (rootdev == 0 && !strstr(cmdline, CMDLINE_BDEV)) {
		n += sprintf(cmdline + n, "%s2", CMDLINE_BDEV);
	}

	/* finally, let's update uboot env & save it */
	if (bootargs && strncmp(cmdline, bootargs, sizeof(cmdline))) {
		setenv("bootargs", cmdline);
		need_save = 1;
	}

__exit:
	if (need_save)
		saveenv();
}

/* --------------------------------------------------------------------------
 * call from u-boot
 */

int board_early_init_f(void)
{
	return 0;
}

int board_init(void)
{
	bd_hwrev_init();
	bd_bootdev_init();
	bd_onewire_init();

	bd_backlight_off();

	bd_lcd_config_gpio();
	bd_lcd_init();

#ifdef CONFIG_SILENT_CONSOLE
	gd->flags |= GD_FLG_SILENT;
#endif

	return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{
	bd_update_env();

#ifdef CONFIG_REVISION_TAG
	set_board_rev();
#endif
	set_dtb_name();

#ifdef CONFIG_SILENT_CONSOLE
	gd->flags &= ~GD_FLG_SILENT;
#endif

	bd_backlight_on();
	printf("\n");

	return 0;
}
#endif

#ifdef CONFIG_SPLASH_SOURCE
#include <splash.h>
static struct splash_location splash_locations[] = {
	{
	.name = "mmc_fs",
	.storage = SPLASH_STORAGE_MMC,
	.flags = SPLASH_STORAGE_FS,
	.devpart = __stringify(CONFIG_ROOT_DEV) ":" __stringify(CONFIG_BOOT_PART),
	},
};

int splash_screen_prepare(void)
{
	int err;
	char *env_cmd = getenv("load_splash");

	if (env_cmd) {
		err = run_command(env_cmd, 0);

	} else {
		char devpart[32] = { 0, };
		int rootdev = getenv_ulong("rootdev", 0, CONFIG_ROOT_DEV);
		int bootpart = getenv_ulong("bootpart", 0, CONFIG_BOOT_PART);

		snprintf(devpart, ARRAY_SIZE(info), "%d:%d", rootdev, bootpart);
		splash_locations[0].devpart = devpart;

		err = splash_source_load(splash_locations,
				ARRAY_SIZE(splash_locations));
	}

	if (!err) {
		char addr[64];

		sprintf(addr, "0x%lx", gd->fb_base);
		setenv("fb_addr", addr);
	}

	return err;
}
#endif

/* u-boot dram initialize */
int dram_init(void)
{
	gd->ram_size = CONFIG_SYS_SDRAM_SIZE;
	return 0;
}

/* u-boot dram board specific */
void dram_init_banksize(void)
{
#define SCR_USER_SIG6_READ		(SCR_ALIVE_BASE + 0x0F0)
	int g_NR_chip = readl(SCR_USER_SIG6_READ) & 0x3;

	/* set global data memory */
	gd->bd->bi_arch_number = machine_arch_type;
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x00000100;

	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->bd->bi_dram[0].size  = CONFIG_SYS_SDRAM_SIZE;

	if (g_NR_chip > 1) {
		gd->bd->bi_dram[1].start = 0x80000000;
		gd->bd->bi_dram[1].size  = 0x40000000;
	}
}