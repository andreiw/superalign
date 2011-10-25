#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <math.h>

#include "dev.h"

typedef long long ns_t;

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

#define returnif(x) do { typeof(x) __x = (x); if (__x < 0) return (__x); } while (0)

static void format_ns(char *out, ns_t ns)
{
	if (ns < 1000)
		snprintf(out, 8, "%lldns", ns);
	else if (ns < 1000 * 1000)
		snprintf(out, 8, "%.3gµs", ns / 1000.0);
	else if (ns < 1000 * 1000 * 1000)
		snprintf(out, 8, "%.3gms", ns / 1000000.0);
	else {
		snprintf(out, 8, "%.4gs", ns / 1000000000.0);
	}
}

static inline void print_ns(ns_t ns)
{
	char buf[8];
	format_ns(buf, ns);
	puts(buf);
}

static inline void print_f_ns(long double nano, char *postfix)
{
	if (nano < 1000)
		printf("%.3Lgn", nano);
	else if (nano < 1000 * 1000)
		printf("%.3Lgµ", nano / 1000.0);
	else if (nano < 1000 * 1000 * 1000)
		printf("%.3Lgm", nano / 1000000.0);
	else {
		printf("%.4Lg", nano / 1000000000.0);
	}
	printf("%s", postfix);
}

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
	uintmax_t gindex = 0;
	struct device dev;
	off64_t size = 0;
	off64_t asize = 0;
	off64_t offset = 0;
	uintmax_t count = 0;
	uintmax_t blocks = 0;
	uintmax_t order = 0;
	off64_t align = 0;
	ns_t time = 0;
	ns_t rtime = 0;
	uintmax_t repeat = 0;
	int verbose = 0;
	uintmax_t pos = 0;
	bool erase = false;
	bool random = false;
	bool do_read = false;
	bool no_direct = false;
	uintmax_t max = 0;
	uintmax_t min = -1ULL;
	long double oldm = 0;
	long double newm = 0;
	long double olds = 0;
	long double news = 0;
	uintmax_t range[2];

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

	if (verbose) {
		printf("size: %ju\n", size);
		printf("count: %ju, blocks: %ju, order = %ju\n", count, blocks, order);
		printf("offset: %ju\n", offset);
		printf("align-on: %ju\n", align);
		printf("aligned size: %ju\n", asize);
		printf("device size = %ju\n", dev.size);
		if (random)
			printf("LFSR-random accesses\n");
		if (no_direct)
			printf("Using non-O_DIRECT I/O\n");
	}

	if (!repeat)
		repeat = 1;

	rindex = 0;
	while (rindex < repeat) {
		if (erase) {
			if (verbose)
				printf("start erase\n");
			range[0] = 0;
			range[1] = dev.size;
			if(ioctl(dev.fd, BLKDISCARD, range))
				perror("discard");
			if (verbose)
				printf("finish erase\n");
		}
		index = 0;
		time = 0;
		while (index < count) {
			ns_t t;
			index++;
			gindex++;

			if (random) {
				pos = lfsr(pos, order);
			} else {
				pos = index;
			}
			if (need_exit)
				break;

			if (do_read)
				t = time_read(&dev, offset + pos * asize, size);
			else
				t = time_write(&dev, offset + pos * asize, size, WBUF_RAND);
			if (t < 0) {
				if (do_read)
					printf("read error\n");
				else
					printf("write error\n");
				return -1;
			}

			if ((uintmax_t) t > max) max = t;
			if ((uintmax_t) t < min) min = t;
			if (gindex == 0) {
				oldm = newm = t;
				olds = 0;
			} else {
				newm = oldm + ((long double) t - oldm) / gindex;
				news = olds + ((long double) t - oldm) * ((long double) t - newm);
				oldm = newm;
				olds = news;
			}

			time +=t;
			if (verbose > 1) {
				printf("finished %ju/%ju t=", index, count);
				print_ns(t);
			}
		}

		if (need_exit)
			break;

		if (index) {
			printf("Repeat %ju total %s time = ", rindex, do_read ? "read" : "write");
			print_ns(time);
			printf("Repeat %ju avg %s time = ", rindex, do_read ? "read" : "write");
			print_ns(time / index);
			rtime += time/index;
		}
		rindex++;
	}

	if (rindex) {
		printf("Average of repeat averages: ");
		print_ns(rtime / rindex);
	}

	if (gindex) {
		printf("Global stats:\n");
		printf("Min %s = ", do_read ? "read" : "write");
		print_ns(min);
		printf("Max %s = ", do_read ? "read" : "write");
		print_ns(max);
		printf("Mean: ");
		print_f_ns(newm, "s\n");
		printf("Variance: %LG ns^2\n", news / (gindex - 1));
		printf("StdDev: %LG ns\n", sqrtl(news / (gindex - 1)));
	}
	return 0;
}
