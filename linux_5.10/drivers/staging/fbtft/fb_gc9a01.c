// SPDX-License-Identifier: GPL-2.0+

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME "fb_gc9a01"

#define MADCTL_BGR BIT(3)
#define MADCTL_MV  BIT(5)
#define MADCTL_MX  BIT(6)
#define MADCTL_MY  BIT(7)

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);
	mdelay(120);

	write_reg(par, 0xEF);
	write_reg(par, 0xEB, 0x14);

	write_reg(par, 0xFE);
	write_reg(par, 0xEF);

	write_reg(par, 0xEB, 0x14);

	write_reg(par, 0x84, 0x40);
	write_reg(par, 0x85, 0xFF);
	write_reg(par, 0x86, 0xFF);
	write_reg(par, 0x87, 0xFF);

	write_reg(par, 0x88, 0x0A);
	write_reg(par, 0x89, 0x21);
	write_reg(par, 0x8A, 0x00);
	write_reg(par, 0x8B, 0x80);
	write_reg(par, 0x8C, 0x01);
	write_reg(par, 0x8D, 0x01);
	write_reg(par, 0x8E, 0xFF);
	write_reg(par, 0x8F, 0xFF);

	write_reg(par, 0xB6, 0x00, 0x20);

	write_reg(par, 0x3A, 0x05);

	write_reg(par, 0x90, 0x08, 0x08, 0x08, 0x08);
	write_reg(par, 0xBD, 0x06);
	write_reg(par, 0xBC, 0x00);

	write_reg(par, 0xFF, 0x60, 0x01, 0x04);
	write_reg(par, 0xC3, 0x13);
	write_reg(par, 0xC4, 0x13);
	write_reg(par, 0xC9, 0x22);
	write_reg(par, 0xBE, 0x11);
	write_reg(par, 0xE1, 0x10, 0x0E);
	write_reg(par, 0xDF, 0x21, 0x0C, 0x02);

	write_reg(par, 0xF0, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A);
	write_reg(par, 0xF1, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F);
	write_reg(par, 0xF2, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A);
	write_reg(par, 0xF3, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F);

	write_reg(par, 0xED, 0x1B, 0x0B);
	write_reg(par, 0xAE, 0x77);
	write_reg(par, 0xCD, 0x63);

	write_reg(par, 0x70, 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03);
	write_reg(par, 0xE8, 0x34);

	write_reg(par, 0x62, 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70);
	write_reg(par, 0x63, 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70);
	write_reg(par, 0x64, 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07);
	write_reg(par, 0x66, 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00);
	write_reg(par, 0x67, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98);
	write_reg(par, 0x74, 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00);
	write_reg(par, 0x98, 0x3E, 0x07);

	write_reg(par, 0x35);
	write_reg(par, 0x21);
	write_reg(par, 0x11);
	mdelay(120);
	write_reg(par, 0x29);
	mdelay(20);

	return 0;
}

static int set_var(struct fbtft_par *par)
{
	u8 madctl = 0;

	if (par->bgr)
		madctl |= MADCTL_BGR;
	switch (par->info->var.rotate) {
	case 0:
		break;
	case 90:
		madctl |= (MADCTL_MV | MADCTL_MY);
		break;
	case 180:
		madctl |= (MADCTL_MX | MADCTL_MY);
		break;
	case 270:
		madctl |= (MADCTL_MV | MADCTL_MX);
		break;
	default:
		return -EINVAL;
	}
	write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, madctl);
	return 0;
}

/**
 * blank() - blank the display
 *
 * @par: FBTFT parameter object
 * @on: whether to enable or disable blanking the display
 *
 * Return: 0 on success, < 0 if error occurred.
 */
static int blank(struct fbtft_par *par, bool on)
{
	if (on)
		write_reg(par, MIPI_DCS_SET_DISPLAY_OFF);
	else
		write_reg(par, MIPI_DCS_SET_DISPLAY_ON);
	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS, (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);

	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS, (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);

	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

static struct fbtft_display display = {
	.regwidth = 8,
	.buswidth = 8,
	.width = 240,
	.height = 240,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.blank = blank,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "galaxycore,gc9a01", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:gc9a01");
MODULE_ALIAS("platform:gc9a01");
MODULE_DESCRIPTION("FB driver for the GC9A01 LCD Controller");
MODULE_AUTHOR("TraeIDE");
MODULE_LICENSE("GPL");
