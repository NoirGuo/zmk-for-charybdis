/*
 * Copyright (c) 2023 Mr Beam Lasers GmbH.
 * Copyright (c) 2023 Amrith Venkat Kesavamoorthi <amrith@mr-beam.org>
 * Copyright (c) 2023 Martin Kiepfer <mrmarteng@teleschirm.org>
 * Copyright (c) 2026 NoirGuo
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef GC9A01_DISPLAY_DRIVER_H__
#define GC9A01_DISPLAY_DRIVER_H__

#include <zephyr/kernel.h>

#define GC9A01_CMD_NONE				0xff

#define GC9A01_CMD_SW_RESET			0x01

#define GC9A01_CMD_SLPIN			0x10
#define GC9A01_CMD_SLPOUT			0x11
#define GC9A01_CMD_INV_OFF			0x20
#define GC9A01_CMD_INV_ON			0x21
#define GC9A01_CMD_DISP_OFF			0x28
#define GC9A01_CMD_DISP_ON			0x29

#define GC9A01_CMD_CASET			0x2a
#define GC9A01_CMD_RASET			0x2b
#define GC9A01_CMD_RAMWR			0x2c

#define GC9A01_CMD_TEON			0x35

#define GC9A01_CMD_MADCTL			0x36
#define GC9A01_MADCTL_MY_BOTTOM_TO_TOP		0x80
#define GC9A01_MADCTL_MX_RIGHT_TO_LEFT		0x40
#define GC9A01_MADCTL_MV_REVERSE_MODE		0x20
#define GC9A01_MADCTL_ML			0x10
#define GC9A01_MADCTL_BGR			0x08
#define GC9A01_MADCTL_MH_RIGHT_TO_LEFT		0x04

#define GC9A01_CMD_COLMOD			0x3a
#define GC9A01_COLMOD_RGB565			(0x50 | 0x05)	/* 16-bit MCU + 16-bit RGB */

#define GC9A01_CMD_DFUNCTR			0xb6
#define GC9A01_CMD_PWRCTRL1			0xc1
#define GC9A01_CMD_PWRCTRL2			0xc3
#define GC9A01_CMD_PWRCTRL3			0xc4
#define GC9A01_CMD_PWRCTRL4			0xc9

#define GC9A01_CMD_FRAMERATE			0xe8

#define GC9A01_CMD_GAMMA1			0xf0	/* negative polarity */
#define GC9A01_CMD_GAMMA2			0xf1
#define GC9A01_CMD_GAMMA3			0xf2	/* positive polarity */
#define GC9A01_CMD_GAMMA4			0xf3

#define GC9A01_CMD_INREGEN1			0xfe	/* inter-register enable 1 */
#define GC9A01_CMD_INREGEN2			0xef	/* inter-register enable 2 */

/* Enter / exit sleep duration per GC9A01 datasheet. */
#define GC9A01_SLEEP_IN_OUT_DURATION_MS		120

#endif
