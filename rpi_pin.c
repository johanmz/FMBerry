/*
	The MIT License (MIT)

	Copyright (c) 2013 Andrey Chilikin (achilikin@gmail.com)

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include <gpiod.h>

#include "rpi_pin.h"

#if _DEBUG
#define _IFDEB(x) do { x; } while(0)
#else
#define _IFDEB(x)
#endif

static const char *irq_mode[] = { "none\n", "rising\n", "falling\n", "both\n" };

static uint8_t valid_pins_r1[64] =
{
//  pin index
//  0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	1,1,0,0,1,0,0,1,1,1,1,1,0,0,1,1,0,1,1,0,0,1,1,1,1,1,0,0,0,0,0,0
};

static uint8_t valid_pins_r2[64] =
{
//  pin index
//  0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	0,0,1,1,1,0,0,1,1,1,1,1,0,0,1,1,0,1,1,0,0,0,1,1,1,1,0,1,1,1,1,1
};

static uint8_t *pvalid_pins = valid_pins_r2;

static int pin_fds[64];

#define FPIN_EXPORTED  0x0001
#define FPIN_DIR_INPUT 0x0020

static int pin_flags[64];

int rpi_pin_init(int pi_revision)
{
	if (pi_revision == 1)
		pvalid_pins = valid_pins_r1;

	return 0;
}

// set output value for the led
int rpi_pin_set(struct gpiod_line *ledline, uint8_t value)
{
	int ret;
	ret = gpiod_line_set_value(ledline, value);
	if (ret < 0) {
		syslog(LOG_ERR, "Could not set value for LED\n");
		gpiod_line_release(ledline);
	}
}

int rpi_pin_enable_led (uint8_t pin, struct gpiod_chip *chip, struct gpiod_line **pledline)
{
	struct gpiod_line *ledline;
	int ret;

	if ((pin > 63) || !pvalid_pins[pin]) {
	syslog(LOG_ERR, "Invalid pin for LED\n");
	return -1;
	}

	*pledline = gpiod_chip_get_line(chip, pin);
	ledline=*pledline;
	if (!ledline) {
		syslog(LOG_ERR, "Request line for LED failed\n");
		return -1;
	}

	ret = gpiod_line_request_output(ledline, "Consumer", 0);
	if (ret < 0) {
		syslog(LOG_ERR, "Request output for LED failed\n");
		gpiod_line_release(ledline);
		return -1;
	}

	return 0;

}

// sets up the RDS pin, for signalling when the MMR70 is ready to receive RDS data
// returns -1 as error
// sets *line, used in the main loop for the wait event of the MMR70 to receive RDS data
int rpi_pin_poll_enable(uint8_t pin, struct gpiod_chip **pchip, struct gpiod_line **pline)
{
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	int ret;

	if ((pin > 63) || !pvalid_pins[pin])
		return -1;

    *pchip = gpiod_chip_open_by_name("gpiochip0");
	chip = *pchip;
	if (!chip) {
		syslog(LOG_ERR, "RDS open gpiochip0 failed\n");
		return -1;
	}

	*pline = gpiod_chip_get_line(chip, pin);
	line = *pline;
	if (!line) {
		syslog(LOG_ERR, "RDS get line failed\n");
		gpiod_chip_close(chip);
		return -1;
	}

	ret = gpiod_line_request_falling_edge_events(line, "Consumer");
	if (ret < 0) {
		syslog(LOG_ERR, "RDS request falling edge event failed\n");
		gpiod_line_release(line);
		gpiod_chip_close(chip);
		return -1;
	}
	
	return 0;
}

