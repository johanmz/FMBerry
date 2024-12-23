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



 
