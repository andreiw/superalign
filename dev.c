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

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>

#include <linux/fs.h>

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

#include "dev.h"

#define MAX_BUFSIZE (64 * 1024 * 1024)

static inline long long time_to_ns(struct timespec *ts)
{
	return ((long long) ts->tv_sec) * 1000 * 1000 * 1000 + ts->tv_nsec;
}

static long long get_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return time_to_ns(&ts);
}

#if !(_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600)
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *ptr;

	if (alignment % sizeof(void *))
		return EINVAL;

	ptr = memalign(alignment, size);
	if (!ptr)
		return ENOMEM;

	*memptr = ptr;
	return 0;
}
#endif


long long time_read(struct device *dev, off64_t pos, size_t size)
{
	long long now = get_ns();
	ssize_t ret;

	if (size > MAX_BUFSIZE)
		return -ENOMEM;

	do {
		ret = pread64(dev->fd, dev->readbuf, size, (unsigned long long) pos % dev->size);
		if (ret > 0) {
			size -= ret;
			pos += ret;
		}
	} while (ret > 0 || errno == -EAGAIN);

	if (ret) {
		perror("time_read");
		return 0;
	}

	return get_ns() - now;
}

long long time_write(struct device *dev, off64_t pos, size_t size, enum writebuf which)
{
	long long now = get_ns();
	ssize_t ret;
	unsigned long *p;

	if (size > MAX_BUFSIZE)
		return -ENOMEM;
	p = dev->writebuf[which];

	do {
		ret = pwrite64(dev->fd, p, size, (unsigned long long) pos % dev->size);
		if (ret > 0) {
			size -= ret;
			pos += ret;
		}
	} while (ret > 0 || errno == -EAGAIN);

	if (ret) {
		perror("time_write");
		return 0;
	}

	return get_ns() - now;
}

int erase_dev(struct device *dev)
{
	off64_t range[2];
	range[0] = 0;
	range[1] = dev->size;
	return ioctl(dev->fd, BLKDISCARD, range);
}

static void set_rtprio(void)
{
	int ret;
	struct sched_param p = {
		.sched_priority = 10,
	};
	ret = sched_setscheduler(0, SCHED_FIFO, &p);
	if (ret)
		perror("sched_setscheduler");
}


int setup_dev(struct device *dev,
	      const char *filename,
	      bool no_direct)
{
	int err;
	void *p;
	int flags = O_RDWR | O_SYNC | O_NOATIME;
	if (!no_direct)
		flags= O_DIRECT;

	set_rtprio();

	dev->fd = open(filename, flags);
	if (dev->fd < 0) {
		perror(filename);
		return -errno;
	}

	dev->size = lseek64(dev->fd, 0, SEEK_END);
	if (dev->size < 0) {
		perror("seek");
		return -errno;
	} else if (!dev->size) {
		fprintf(stderr, "Device/file is zero bytes big?\n");
		return -EINVAL;
	}

	err = posix_memalign(&dev->readbuf, 4096, MAX_BUFSIZE);
	if (err)
		return -err;

	err = posix_memalign(&p, 4096, MAX_BUFSIZE);
	if (err)
		return -err;
	memset(p, 0, MAX_BUFSIZE);
	dev->writebuf[WBUF_ZERO] = p;

	err = posix_memalign(&p,  4096, MAX_BUFSIZE);
	if (err)
		return -err;
	memset(p, 0xff, MAX_BUFSIZE);
	dev->writebuf[WBUF_ONE] = p;

	err = posix_memalign(&p , 4096, MAX_BUFSIZE);
	if (err)
		return -err;
	memset(p, 0x5a, MAX_BUFSIZE);
	dev->writebuf[WBUF_RAND] = p;

	return 0;
}
