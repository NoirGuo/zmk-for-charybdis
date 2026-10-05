/*
 * Copyright (c) 2023 Mr Beam Lasers GmbH.
 * Copyright (c) 2023 Amrith Venkat Kesavamoorthi <amrith@mr-beam.org>
 * Copyright (c) 2023 Martin Kiepfer <mrmarteng@teleschirm.org>
 * Copyright (c) 2026 NoirGuo
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GC9A01 1.28" 240x240 round TFT, raw-SPI direct drive.
 *
 * Ported from the Zephyr v4.1.0 gc9x01x MIPI-DBI driver to the direct-SPI
 * layout used by the Noirix44 monitor (same style as display_st7789v.c).
 * The register init sequence (INREGEN1/2 + default_init_regs + pwrctrl +
 * gamma + framerate + TEON) is taken verbatim from
 * drivers/display/display_gc9x01x.c in Zephyr v4.1.0.
 */

#define DT_DRV_COMPAT noirix_gc9a01

#include "display_gc9a01.h"

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>

#define LOG_LEVEL CONFIG_DISPLAY_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display_gc9a01);

/* One entry of the fixed init sequence. */
struct gc9a01_init_cmd {
	uint8_t cmd;
	uint8_t len;
	const uint8_t data[12];
};

static const struct gc9a01_init_cmd default_init_regs[] = {
	{0xEB, 1, {0x14}},
	{0x84, 1, {0x40}},
	{0x85, 1, {0xFF}},
	{0x86, 1, {0xFF}},
	{0x87, 1, {0xFF}},
	{0x88, 1, {0x0A}},
	{0x89, 1, {0x21}},
	{0x8A, 1, {0x00}},
	{0x8B, 1, {0x80}},
	{0x8C, 1, {0x01}},
	{0x8D, 1, {0x01}},
	{0x8E, 1, {0xFF}},
	{0x8F, 1, {0xFF}},
	{0xB6, 2, {0x00, 0x20}},
	{0x90, 4, {0x08, 0x08, 0x08, 0x08}},
	{0xBD, 1, {0x06}},
	{0xBC, 1, {0x00}},
	{0xFF, 3, {0x60, 0x01, 0x04}},
	{0xBE, 1, {0x11}},
	{0xE1, 2, {0x10, 0x0E}},
	{0xDF, 3, {0x21, 0x0C, 0x02}},
	{0xED, 2, {0x1B, 0x0B}},
	{0xAE, 1, {0x77}},
	{0xCD, 1, {0x63}},
	{0x70, 9, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}},
	{0x62, 12, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}},
	{0x63, 12, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}},
	{0x64, 7, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}},
	{0x66, 10, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}},
	{0x67, 10, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}},
	{0x74, 7, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}},
	{0x98, 2, {0x3E, 0x07}},
};

struct gc9a01_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec cmd_data_gpio;
	struct gpio_dt_spec reset_gpio;
	uint8_t mdac;
	uint8_t colmod;
	bool inversion;
	uint8_t pwrctrl1[1];
	uint8_t pwrctrl2[1];
	uint8_t pwrctrl3[1];
	uint8_t pwrctrl4[1];
	uint8_t gamma1[6];
	uint8_t gamma2[6];
	uint8_t gamma3[6];
	uint8_t gamma4[6];
	uint8_t framerate[1];
	uint16_t width;
	uint16_t height;
};

struct gc9a01_data {
	uint16_t x_offset;
	uint16_t y_offset;
	uint8_t bytes_per_pixel;
	enum display_orientation orientation;
};

static void gc9a01_transmit(const struct device *dev, uint8_t cmd, uint8_t *tx_data,
			    size_t tx_count)
{
	const struct gc9a01_config *config = dev->config;

	struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
	struct spi_buf_set tx_bufs = {.buffers = &tx_buf, .count = 1};

	if (cmd != GC9A01_CMD_NONE) {
		gpio_pin_set_dt(&config->cmd_data_gpio, 1);
		spi_write_dt(&config->bus, &tx_bufs);
	}

	if (tx_data != NULL) {
		tx_buf.buf = tx_data;
		tx_buf.len = tx_count;
		gpio_pin_set_dt(&config->cmd_data_gpio, 0);
		spi_write_dt(&config->bus, &tx_bufs);
	}
}

static void gc9a01_reset_display(const struct device *dev)
{
	const struct gc9a01_config *config = dev->config;

	if (config->reset_gpio.port != NULL) {
		k_sleep(K_MSEC(1));
		gpio_pin_set_dt(&config->reset_gpio, 1);
		k_sleep(K_MSEC(6));
		gpio_pin_set_dt(&config->reset_gpio, 0);
		k_sleep(K_MSEC(20));
	} else {
		gc9a01_transmit(dev, GC9A01_CMD_SW_RESET, NULL, 0);
		k_sleep(K_MSEC(5));
	}
}

static int gc9a01_blanking_on(const struct device *dev)
{
	gc9a01_transmit(dev, GC9A01_CMD_DISP_OFF, NULL, 0);
	return 0;
}

static int gc9a01_blanking_off(const struct device *dev)
{
	gc9a01_transmit(dev, GC9A01_CMD_DISP_ON, NULL, 0);
	return 0;
}

static void gc9a01_exit_sleep(const struct device *dev)
{
	gc9a01_transmit(dev, GC9A01_CMD_SLPOUT, NULL, 0);
	k_sleep(K_MSEC(GC9A01_SLEEP_IN_OUT_DURATION_MS));
}

static void gc9a01_set_mem_area(const struct device *dev, const uint16_t x, const uint16_t y,
				const uint16_t w, const uint16_t h)
{
	struct gc9a01_data *data = dev->data;
	uint16_t spi_data[2];

	uint16_t ram_x = x + data->x_offset;
	uint16_t ram_y = y + data->y_offset;

	spi_data[0] = sys_cpu_to_be16(ram_x);
	spi_data[1] = sys_cpu_to_be16(ram_x + w - 1);
	gc9a01_transmit(dev, GC9A01_CMD_CASET, (uint8_t *)&spi_data[0], 4);

	spi_data[0] = sys_cpu_to_be16(ram_y);
	spi_data[1] = sys_cpu_to_be16(ram_y + h - 1);
	gc9a01_transmit(dev, GC9A01_CMD_RASET, (uint8_t *)&spi_data[0], 4);
}

static int gc9a01_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct gc9a01_data *data = dev->data;
	const uint8_t *write_data_start = (uint8_t *)buf;
	uint16_t nbr_of_writes;
	uint16_t write_h;

	__ASSERT(desc->width <= desc->pitch, "Pitch is smaller than width");
	__ASSERT((desc->pitch * data->bytes_per_pixel * desc->height) <= desc->buf_size,
		 "Input buffer too small");

	LOG_DBG("Writing %dx%d (w,h) @ %dx%d (x,y)", desc->width, desc->height, x, y);
	gc9a01_set_mem_area(dev, x, y, desc->width, desc->height);

	if (desc->pitch > desc->width) {
		write_h = 1U;
		nbr_of_writes = desc->height;
	} else {
		write_h = desc->height;
		nbr_of_writes = 1U;
	}

	for (uint16_t write_cnt = 0U; write_cnt < nbr_of_writes; ++write_cnt) {
		gc9a01_transmit(dev, write_cnt == 0U ? GC9A01_CMD_RAMWR : GC9A01_CMD_NONE,
				(void *)write_data_start,
				desc->width * data->bytes_per_pixel * write_h);
		write_data_start += (desc->pitch * data->bytes_per_pixel);
	}

	return 0;
}

static void gc9a01_get_capabilities(const struct device *dev,
				    struct display_capabilities *capabilities)
{
	const struct gc9a01_config *config = dev->config;
	const struct gc9a01_data *data = dev->data;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
	capabilities->current_orientation = data->orientation;
}

static int gc9a01_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pixel_format)
{
	const struct gc9a01_config *config = dev->config;
	struct gc9a01_data *data = dev->data;
	uint8_t tx_data = config->colmod;

	if (pixel_format != PIXEL_FORMAT_RGB_565) {
		LOG_ERR("Pixel format change not implemented");
		return -ENOTSUP;
	}

	gc9a01_transmit(dev, GC9A01_CMD_COLMOD, &tx_data, 1U);
	data->bytes_per_pixel = 2U;

	return 0;
}

static int gc9a01_set_orientation(const struct device *dev,
				  const enum display_orientation orientation)
{
	const struct gc9a01_config *config = dev->config;
	struct gc9a01_data *data = dev->data;

	/* Only modify the MY/MX/MV bits, keep the existing MDAC base config
	 * (BGR etc.) intact. Same mapping as the Zephyr gc9x01x driver. */
	uint8_t tx_data = config->mdac & (GC9A01_MADCTL_ML | GC9A01_MADCTL_BGR |
					  GC9A01_MADCTL_MH_RIGHT_TO_LEFT);

	switch (orientation) {
	case DISPLAY_ORIENTATION_NORMAL:
		break;

	case DISPLAY_ORIENTATION_ROTATED_90:
		tx_data |= GC9A01_MADCTL_MV_REVERSE_MODE | GC9A01_MADCTL_MY_BOTTOM_TO_TOP;
		break;

	case DISPLAY_ORIENTATION_ROTATED_180:
		tx_data |= GC9A01_MADCTL_MY_BOTTOM_TO_TOP | GC9A01_MADCTL_MX_RIGHT_TO_LEFT |
			   GC9A01_MADCTL_MH_RIGHT_TO_LEFT;
		break;

	case DISPLAY_ORIENTATION_ROTATED_270:
		tx_data |= GC9A01_MADCTL_MV_REVERSE_MODE | GC9A01_MADCTL_MX_RIGHT_TO_LEFT;
		break;

	default:
		LOG_ERR("Error changing display orientation");
		return -ENOTSUP;
	}

	gc9a01_transmit(dev, GC9A01_CMD_MADCTL, &tx_data, 1U);
	data->orientation = orientation;
	LOG_INF("Changed orientation to: '%d'", data->orientation);

	return 0;
}

static void gc9a01_lcd_init(const struct device *dev)
{
	const struct gc9a01_config *config = dev->config;

	/* Enable inter-register access. */
	gc9a01_transmit(dev, GC9A01_CMD_INREGEN1, NULL, 0);
	gc9a01_transmit(dev, GC9A01_CMD_INREGEN2, NULL, 0);

	/* Fixed vendor init sequence (from Zephyr gc9x01x). */
	for (size_t i = 0; i < ARRAY_SIZE(default_init_regs); ++i) {
		gc9a01_transmit(dev, default_init_regs[i].cmd,
				(uint8_t *)default_init_regs[i].data, default_init_regs[i].len);
	}

	/* Power control / gamma / framerate (DT-tunable). */
	gc9a01_transmit(dev, GC9A01_CMD_PWRCTRL1, (uint8_t *)config->pwrctrl1,
			sizeof(config->pwrctrl1));
	gc9a01_transmit(dev, GC9A01_CMD_PWRCTRL2, (uint8_t *)config->pwrctrl2,
			sizeof(config->pwrctrl2));
	gc9a01_transmit(dev, GC9A01_CMD_PWRCTRL3, (uint8_t *)config->pwrctrl3,
			sizeof(config->pwrctrl3));
	gc9a01_transmit(dev, GC9A01_CMD_PWRCTRL4, (uint8_t *)config->pwrctrl4,
			sizeof(config->pwrctrl4));
	gc9a01_transmit(dev, GC9A01_CMD_GAMMA1, (uint8_t *)config->gamma1,
			sizeof(config->gamma1));
	gc9a01_transmit(dev, GC9A01_CMD_GAMMA2, (uint8_t *)config->gamma2,
			sizeof(config->gamma2));
	gc9a01_transmit(dev, GC9A01_CMD_GAMMA3, (uint8_t *)config->gamma3,
			sizeof(config->gamma3));
	gc9a01_transmit(dev, GC9A01_CMD_GAMMA4, (uint8_t *)config->gamma4,
			sizeof(config->gamma4));
	gc9a01_transmit(dev, GC9A01_CMD_FRAMERATE, (uint8_t *)config->framerate,
			sizeof(config->framerate));

	/* Enable tearing-effect line. */
	gc9a01_transmit(dev, GC9A01_CMD_TEON, NULL, 0);

	/* Memory Data Access Control. */
	uint8_t tmp = config->mdac;
	gc9a01_transmit(dev, GC9A01_CMD_MADCTL, &tmp, 1);

	/* Interface Pixel Format. */
	tmp = config->colmod;
	gc9a01_transmit(dev, GC9A01_CMD_COLMOD, &tmp, 1);

	/* Display inversion (default off). */
	gc9a01_transmit(dev, config->inversion ? GC9A01_CMD_INV_ON : GC9A01_CMD_INV_OFF, NULL, 0);
}

static int gc9a01_init(const struct device *dev)
{
	const struct gc9a01_config *config = dev->config;

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}

	if (config->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			LOG_ERR("Reset GPIO device not ready");
			return -ENODEV;
		}

		if (gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE)) {
			LOG_ERR("Couldn't configure reset pin");
			return -EIO;
		}
	}

	if (!gpio_is_ready_dt(&config->cmd_data_gpio)) {
		LOG_ERR("CMD/DATA GPIO device not ready");
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(&config->cmd_data_gpio, GPIO_OUTPUT)) {
		LOG_ERR("Couldn't configure CMD/DATA pin");
		return -EIO;
	}

	gc9a01_reset_display(dev);

	gc9a01_blanking_on(dev);

	gc9a01_lcd_init(dev);

	gc9a01_exit_sleep(dev);

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int gc9a01_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret = 0;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		gc9a01_exit_sleep(dev);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		gc9a01_transmit(dev, GC9A01_CMD_SLPIN, NULL, 0);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

static const struct display_driver_api gc9a01_api = {
	.blanking_on = gc9a01_blanking_on,
	.blanking_off = gc9a01_blanking_off,
	.write = gc9a01_write,
	.get_capabilities = gc9a01_get_capabilities,
	.set_pixel_format = gc9a01_set_pixel_format,
	.set_orientation = gc9a01_set_orientation,
};

#define GC9A01_INIT(inst)                                                                          \
	static const struct gc9a01_config gc9a01_config_##inst = {                                 \
		.bus = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),         \
		.cmd_data_gpio = GPIO_DT_SPEC_INST_GET(inst, cmd_data_gpios),                       \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {}),                      \
		.mdac = DT_INST_PROP(inst, mdac),                                                   \
		.colmod = DT_INST_PROP(inst, colmod),                                               \
		.inversion = DT_INST_PROP(inst, display_inversion),                                 \
		.pwrctrl1 = DT_INST_PROP(inst, pwrctrl1),                                      \
		.pwrctrl2 = DT_INST_PROP(inst, pwrctrl2),                                      \
		.pwrctrl3 = DT_INST_PROP(inst, pwrctrl3),                                      \
		.pwrctrl4 = DT_INST_PROP(inst, pwrctrl4),                                      \
		.gamma1 = DT_INST_PROP(inst, gamma1),                                        \
		.gamma2 = DT_INST_PROP(inst, gamma2),                                        \
		.gamma3 = DT_INST_PROP(inst, gamma3),                                        \
		.gamma4 = DT_INST_PROP(inst, gamma4),                                        \
		.framerate = DT_INST_PROP(inst, framerate),                                    \
		.width = DT_INST_PROP(inst, width),                                                 \
		.height = DT_INST_PROP(inst, height),                                               \
	};                                                                                          \
                                                                                                   \
	static struct gc9a01_data gc9a01_data_##inst = {                                           \
		.x_offset = DT_INST_PROP_OR(inst, x_offset, 0),                                    \
		.y_offset = DT_INST_PROP_OR(inst, y_offset, 0),                                    \
		.bytes_per_pixel = 2U,                                                              \
		.orientation = DISPLAY_ORIENTATION_NORMAL,                                          \
	};                                                                                          \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, gc9a01_pm_action);                                          \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &gc9a01_init, PM_DEVICE_DT_INST_GET(inst),                     \
			      &gc9a01_data_##inst, &gc9a01_config_##inst, POST_KERNEL,             \
			      CONFIG_DISPLAY_INIT_PRIORITY, &gc9a01_api);

DT_INST_FOREACH_STATUS_OKAY(GC9A01_INIT)
