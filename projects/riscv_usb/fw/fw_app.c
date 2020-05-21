/*
 * fw_app.c
 *
 * Copyright (C) 2019 Sylvain Munaut
 * All rights reserved.
 *
 * LGPL v3+, see LICENSE.lgpl3
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"

#include "console.h"
#include "e1.h"
#include "led.h"
#include "misc.h"
#include "mini-printf.h"
#include "spi.h"
#include "usb.h"
#include "usb_dfu_rt.h"
#include "utils.h"


extern const struct usb_stack_descriptors app_stack_desc;

static void
serial_no_init()
{
	uint8_t buf[8];
	char *id, *desc;
	int i;

	flash_manuf_id(buf);
	printf("Flash Manufacturer : %s\n", hexstr(buf, 3, true));

	flash_unique_id(buf);
	printf("Flash Unique ID    : %s\n", hexstr(buf, 8, true));

	/* Overwrite descriptor string */
		/* In theory in rodata ... but nothing is ro here */
	id = hexstr(buf, 8, false);
	desc = (char*)app_stack_desc.str[1];
	for (i=0; i<16; i++)
		desc[2 + (i << 1)] = id[i];
}

static void
boot_dfu(void)
{
	/* Force re-enumeration */
	usb_disconnect();

	/* Boot firmware */
	volatile uint32_t *boot = (void*)0x80000000;
	*boot = (1 << 2) | (1 << 0);
}

void
usb_dfu_rt_cb_reboot(void)
{
        boot_dfu();
}


int clk_hi = 2008;
int clk_lo = 2048;


#define USB_RT_CLK_SET	((0 << 8) | (USB_REQ_TYPE_VENDOR | USB_REQ_RCPT_DEV))
#define USB_RT_CLK_GET	((1 << 8) | (USB_REQ_TYPE_VENDOR | USB_REQ_RCPT_DEV | USB_REQ_READ))

static bool
_vendor_clk_ctrl_req(struct usb_ctrl_req *req, struct usb_xfer *xfer)
{
	int *v;

	/* Device req */
	if (USB_REQ_RCPT(req) != USB_REQ_RCPT_DEV)
		return USB_FND_CONTINUE;

	if (USB_REQ_TYPE(req) != USB_REQ_TYPE_VENDOR)
		return USB_FND_CONTINUE;

	switch (req->wIndex) {
	case 0:  v = &clk_hi; break;
	case 1:  v = &clk_lo; break;
	default: return USB_FND_ERROR;
	}

	switch (req->wRequestAndType)
	{
	case USB_RT_CLK_SET:
		*v = req->wValue;
		xfer->len = 0;
		break;

	case USB_RT_CLK_GET:
		xfer->len = 2;
		xfer->data[0] = (*v     ) & 0xff;
		xfer->data[1] = (*v >> 8) & 0xff;
		break;

	default:
		return USB_FND_ERROR;
	}

	pdm_set(PDM_CLK_HI, true, clk_hi, false);
	pdm_set(PDM_CLK_LO, true, clk_lo, false);

	return USB_FND_SUCCESS;
}

static struct usb_fn_drv _clk_drv = {
	.ctrl_req = _vendor_clk_ctrl_req,
};




static volatile uint32_t * const misc_regs = (void*)(MISC_BASE);

void main()
{
	bool e1_active = false;
	int cmd = 0;

	/* Init console IO */
	console_init();
	puts("Booting App image..\n");

	/* LED */
	led_init();

	/* SPI */
	spi_init();

	/* Setup E1 Vref */
	int d = 25;
	pdm_set(PDM_E1_CT, true, 128, false);
	pdm_set(PDM_E1_P,  true, 128 - d, false);
	pdm_set(PDM_E1_N,  true, 128 + d, false);

	/* Setup clock tuning */
	pdm_set(PDM_CLK_HI, true, clk_hi, false);
	pdm_set(PDM_CLK_LO, true, clk_lo, false);

	/* Enable USB directly */
	serial_no_init();
	usb_init(&app_stack_desc);
	usb_dfu_rt_init();
	usb_e1_init();
	usb_register_function_driver(&_clk_drv);

	usb_connect();
	e1_init(false);
	e1_active = true;
	led_state(true);

	/* Main loop */
	while (1)
	{
		/* Prompt ? */
		if (cmd >= 0)
			printf("Command> ");

		/* Poll for command */
		cmd = getchar_nowait();

		if (cmd >= 0) {
			if (cmd > 32 && cmd < 127) {
				putchar(cmd);
				putchar('\r');
				putchar('\n');
			}

			switch (cmd)
			{
			case 'p':
				usb_debug_print();
				break;
			case 'b':
				boot_dfu();
				break;
			case 'o':
				e1_debug_print(false);
				break;
			case 'O':
				e1_debug_print(true);
				break;
			case 't':
				printf("%08x\n", misc_regs[0]);
			case 'e':
				e1_init(true);
				e1_active = true;
				led_state(true);
				break;
			case 'E':
				e1_init(false);
				e1_active = true;
				led_state(true);
				break;
			case 'c':
				usb_connect();
				break;
			case 'd':
				usb_disconnect();
				break;
			default:
				break;
			}
		}

		/* USB poll */
		usb_poll();

		/* E1 poll */
		if (e1_active) {
			e1_poll();
			usb_e1_run();
		}
	}
}
