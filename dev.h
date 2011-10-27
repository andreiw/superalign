/*
   Superalign - a block benching tool.

   Copyright (C) 2011 Andrei Warkentin <andreiw@vmvware.com>

   This module is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This module is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this module; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef SA_DEV_H
#define SA_DEV_H

#include <unistd.h>

struct device {
	void *readbuf;
	void *writebuf[3];
	int fd;
	off64_t size;
};

enum writebuf {
	WBUF_ZERO,
	WBUF_ONE,
	WBUF_RAND,
};

#define DEV_NO_SYNC   (1 << 0)
#define DEV_NO_DIRECT (1 << 1)

extern int setup_dev(struct device *dev, const char *filename, unsigned flags);

long long time_write(struct device *dev, off64_t pos, size_t size, enum writebuf which);

long long time_read(struct device *dev, off64_t pos, size_t size);

int erase_dev(struct device *dev);

#endif /* SA_DEV_H */
