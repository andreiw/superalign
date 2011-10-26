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

#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#include "dev.h"
#include "stats.h"

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

#define returnif(x) do { typeof(x) __x = (x); if (__x < 0) return (__x); } while (0)

bool need_exit = false;

static void on_sigint(int unused)
{
	need_exit = true;
}

/*
 * Linear feedback shift register
 *
 * We use this to randomize the block positions for random-access
 * tests. Unlike real random data, we know that within 2^bits
 * accesses, every possible value up to 2^bits will be seen
 * exactly once, with the exception of zero, for which we have
 * a special treatment.
 */
static int lfsr(unsigned int v, unsigned int bits)
{
	unsigned int bit;

	if (v >= ((unsigned int) 1 << bits)) {
		fprintf(stderr, "sa: internal error\n");
		exit(-EINVAL);
	}

	if (v == (((1 << bits) - 1) & 0xace1))
		return 0;

	if (v == 0)
		v = ((1 << bits) - 1) & 0xace1;

	switch (bits) {
	case 8: /* x^8 + x^6 + x^5 + x^4 + 1 */
		bit = ((v >> 0) ^ (v >> 2) ^ (v >> 3) ^ (v >> 4)) & 1;
		break;
	case 9: /* x^9 + x^5 + 1 */
		bit = ((v >> 0) ^ (v >> 4)) & 1;
		break;
	case 10: /* x^10 + x^7 + 1 */
		bit = ((v >> 0) ^ (v >> 3)) & 1;
		break;
	case 11: /* x^11 + x^9 + 1 */
		bit = ((v >> 0) ^ (v >> 2)) & 1;
		break;
	case 12: /* x^12 + x^11 + x^10 + x^4 + 1 */
		bit = ((v >> 0) ^ (v >> 1) ^ (v >> 2) ^ (v >> 8)) & 1;
		break;
	case 13: /* x^13 + x^12 + x^11 + x^8 + 1 */
		bit = ((v >> 0) ^ (v >> 1) ^ (v >> 2) ^ (v >> 5)) & 1;
		break;
	case 14: /* x^14 + x^13 + x^12 + x^2 + 1 */
		bit = ((v >> 0) ^ (v >> 1) ^ (v >> 2) ^ (v >> 12)) & 1;
		break;
	case 15: /* x^15 + x^14 + 1 */
		bit = ((v >> 0) ^ (v >> 1) ) & 1;
		break;
	case 16: /* x^16 + x^14 + x^13 + x^11 + 1 */
		bit = ((v >> 0) ^ (v >> 2) ^ (v >> 3) ^ (v >> 5)) & 1;
		break;
	case 17: /* x^17 + x^14 + 1 */
		bit = ((v >> 0) ^ (v >> 3)) & 1;
		break;
	case 18: /* x^18 + x^11 + 1 */
		bit = ((v >> 0) ^ (v >> 7)) & 1;
		break;
	case 19: /* x^19 + x^18 + x^17 + x^14 + 1 */
		bit = ((v >> 0) ^ (v >> 1) ^ (v >> 2) ^ (v >> 5)) & 1;
		break;
	case 20: /* x^20 + x^17 + 1 */
		bit = ((v >> 0) ^ (v >> 3)) & 1;
		break;
	case 21: /* x^21 + x^19 + 1 */
		bit = ((v >> 0) ^ (v >> 2)) & 1;
		break;
	case 22: /* x^22 + x^21 + 1 */
		bit = ((v >> 0) ^ (v >> 1)) & 1;
		break;
	case 23: /* x^23 + x^18 + 1 */
		bit = ((v >> 0) ^ (v >> 5)) & 1;
		break;
	case 24: /* x^24 + x^23 + x^22 + x^17 + 1 */
		bit = ((v >> 0) ^ (v >> 1) ^ (v >> 2) ^ (v >> 7)) & 1;
		break;
	case 25: /* x^25 + x^22 + 1 */
		bit = ((v >> 0) ^ (v >> 3)) & 1;
		break;
	case 26: /* x^26 + x^6 + x^2 + x + 1 */
		bit = ((v >> 0) ^ (v >> 20) ^ (v >> 24) ^ (v >> 25)) & 1;
		break;
	case 27: /* x^27 + x^5 + x^2 + x + 1 */
		bit = ((v >> 0) ^ (v >> 22) ^ (v >> 25) ^ (v >> 26)) & 1;
		break;
	case 28: /* x^28 + x^25 + 1 */
		bit = ((v >> 0) ^ (v >> 3)) & 1;
		break;
	case 29: /* x^29 + x^27 + 1 */
		bit = ((v >> 0) ^ (v >> 2)) & 1;
		break;
	case 30: /* x^30 + x^6 + x^4 + x + 1 */
		bit = ((v >> 0) ^ (v >> 24) ^ (v >> 26) ^ (v >> 29)) & 1;
		break;
	case 31: /* x^31 + x^28 + 1 */
		bit = ((v >> 0) ^ (v >> 3)) & 1;
		break;
	case 32: /* x^32 + x^22 + x^2 + x + 1 */
		bit = ((v >> 0) ^ (v >> 10) ^ (v >> 30) ^ (v >> 31)) & 1;
		break;
	default:
		fprintf(stderr, "sa: internal error (unsupported order %d)\n", bits);
		exit(-EINVAL);
	}

	return v >> 1 | bit << (bits - 1);
}

unsigned int get_order(unsigned int count)
{
	unsigned int r = 0;

	while (count >>= 1) // unroll for more speed...
	{
		r++;
	}
	return r;
}

int main(int argc, char **argv)
{
	uintmax_t index = 0;
	uintmax_t rindex = 0;
	struct device dev;
	struct stats stats;
	off64_t size = 0;
	off64_t asize = 0;
	off64_t offset = 0;
	uintmax_t count = 0;
	uintmax_t blocks = 0;
	uintmax_t order = 0;
	off64_t align = 0;
	uintmax_t repeat = 0;
	int verbose = 0;
	uintmax_t pos = 0;
	bool erase = false;
	bool random = false;
	bool do_read = false;
	bool no_direct = false;

	while (1) {
		int c;
		c = getopt(argc, argv, "s:o:a:c:r:veRdf");
		if (c == -1)
			break;

		switch (c) {
		case 'a':
		case 'o':
		case 's':
		{
			char type;
			off64_t *sptr;

			if (c == 's')
				sptr = &size;
			else if (c == 'o')
				sptr = &offset;
			else
				sptr = &align;
			sscanf(optarg, "%ju%c", sptr, &type);
			switch (type) {
				case 'T':
				case 't':
					*sptr <<= 10;
				case 'G':
				case 'g':
					*sptr <<= 10;
				case 'M':
				case 'm':
					*sptr <<= 10;
				case 'K':
				case 'k':
					*sptr <<= 10;
				case '\0':
				case 'B':
				case 'b':
					break;
				case 'S':
				case 's':
					*sptr <<= 9;
				default:
					fprintf(stderr, "Size modifer '%c' not one of [BKMGTS]\n", type);
			}
			break;
		}
		case 'c':
			sscanf(optarg, "%ju", &count);
			break;
		case 'v':
			verbose++;
			break;
		case 'e':
			erase = true;
			break;
		case 'r':
			sscanf(optarg, "%ju", &repeat);
			break;
		case 'R':
			random = true;
			break;
		case 'd':
			do_read = true;
			break;
		case 'f':
			no_direct = true;
			break;
		};
	}

	if (!size || optind != (argc - 1)) {
		printf("%s -s size [-f] [-n] [-o offset] [-a align] [-c count] [-r repeats] dev\n",
			argv[0]);
		return -1;
	};

	returnif(setup_dev(&dev, argv[optind], no_direct));
	signal(SIGINT, &on_sigint);
	siginterrupt(SIGINT, true);

	if (align) {
		asize = (size + align - 1) & ~(align - 1);

		/* Ensure sequential accesses are not consecutive. */
		if (asize == size)
			asize += align;
	} else {
		asize = size;
	}

	/* We're going to start unaligned writes of a certain size up to the end .*/
	blocks = (dev.size - offset) / asize;
	if (!count)
		count = blocks;
	order = get_order(blocks);

	returnif(stats_init(&stats, count, size, verbose, do_read ? "read" : "write"));
	if (verbose) {
		printf("Test configuration: %s\n", do_read ? "reads" : "writes");
		printf("\tsize: %ju\n", size);
		printf("\tcount: %ju, blocks: %ju, order = %ju\n", count, blocks, order);
		printf("\toffset: %ju\n", offset);
		printf("\talign-on: %ju\n", align);
		printf("\taligned size: %ju\n", asize);
		printf("\tdevice size = %ju\n", dev.size);
		if (random)
			printf("\tLFSR-random accesses\n");
		if (no_direct)
			printf("\tnon-O_DIRECT I/O\n");
	}

	if (!repeat)
		repeat = 1;

	for (rindex = 0; rindex < repeat; rindex++) {
		if (erase)
			if (erase_dev(&dev)) {
				perror("erase");
				goto out;
			}

		for (index = 0; index < count; index++) {
			long double t;

			if (random)
				pos = lfsr(pos, order);
			else
				pos = index;

			if (need_exit)
				break;

			if (do_read)
				t = time_read(&dev, offset + pos * asize, size);
			else
				t = time_write(&dev, offset + pos * asize, size, WBUF_RAND);
			if (t == 0) {

				/* Failed in time_read or time_write. */
				goto out;
			}

			if (stats_do(&stats, t,  offset + pos * asize)) {
				fprintf(stderr, "Error processing stats\n");
				goto out;
			}
		}

		if (need_exit)
			break;
	}

out:
	stats_print(&stats);
	stats_fini(&stats);
	return 0;
}
