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
#ifndef __RPI_IRQ_H__
#define __RPI_IRQ_H__

#include <stdint.h>
#include <unistd.h>
#include <gpiod.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPI_REV1 1
#define RPI_REV2 2

enum PIN_DIRECTION { RPI_INPUT, RPI_OUTPUT };
enum PIN_EDGE_MODE { EDGE_NONE = 0, EDGE_RISING, EDGE_FALLING, EDGE_BOTH };

// revision 1 - old, 2 - new, including P5 pins
// must be called before any other rpi_pin_* functions if revision is 1
int rpi_pin_init(int pi_revision);

int rpi_pin_set(struct gpiod_line *ledline, uint8_t value); // set output value 0-1

// enables POLLPRI event on edge detection, pin must be in INPUT mode
int rpi_pin_poll_enable(uint8_t pin, struct gpiod_chip **pchip, struct gpiod_line **pline);

int rpi_pin_enable_led (uint8_t pin, struct gpiod_chip *chip, struct gpiod_line **pledline);

#ifdef __cplusplus
}
#endif

#endif
