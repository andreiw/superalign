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

	if (v >= (1 << bits)) {
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
	unsigned int index = 0;
	unsigned int rindex = 0;
	struct device dev;
	int size = 0;
	int asize = 0;
	int offset = 0;
	unsigned int count = 0;
	unsigned int blocks = 0;
	unsigned int order = 0;
	int align = 0;
	ns_t time = 0;
	ns_t rtime = 0;
	unsigned int repeat = 0;
	int verbose = 0;
	unsigned int pos = 0;
	bool erase = false;
	bool random = false;
	bool do_read = false;
	unsigned long long max = 0;
	unsigned long long min = -1ULL;
	long double oldm = 0;
	long double newm = 0;
	long double olds = 0;
	long double news = 0;
	unsigned long long range[2];

	while (1) {
		int c;
		c = getopt(argc, argv, "s:o:a:c:r:veRd");
		if (c == -1)
			break;

		switch (c) {
		case 's':
			size = atoi(optarg);
			break;
		case 'o':
			offset = atoi(optarg);
			break;
		case 'a':
			align = atoi(optarg);
			break;
		case 'c':
			count = atoi(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'e':
			erase = true;
			break;
		case 'r':
			repeat = atoi(optarg);
			break;
		case 'R':
			random = true;
			break;
		case 'd':
			do_read = true;
			break;
		};
	}

	if (!size || optind != (argc - 1)) {
		printf("%s -s size [-d] [-o offset] [-a align] [-c count] [-r repeats] dev\n",
			argv[0]);
		return -1;
	};

	returnif(setup_dev(&dev, argv[optind]));
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
		printf("size: %d\n", size);
		printf("count: %d, blocks: %d, order = %d\n", count, blocks, order);
		printf("offset: %d\n", offset);
		printf("align-on: %d\n", align);
		printf("possibly aligned size: %d\n", asize);
		printf("device size = %lld\n", dev.size);
		if (random)
			printf("LFSR-random accesses\n");
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
		rindex++;
		index = 0;
		time = 0;
		while (index < count) {
			ns_t t;
			index++;

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

			if ((unsigned long long) t > max) max = t;
			if ((unsigned long long) t < min) min = t;
			if (rindex == 1 && index == 1) {
				oldm = newm = t;
				olds = 0;
			} else {
				newm = oldm + ((long double) t - oldm) / (((rindex - 1) * count + index));
				news = olds + ((long double) t - oldm) * ((long double) t - newm);
				oldm = newm;
				olds = news;
			}

			time +=t;
			if (verbose > 1) {
				printf("finished %d/%d t=", index, count);
				print_ns(t);
			}
		}

		if (need_exit)
			break;

		if (index) {
			printf("Repeat %d total %s time = ", rindex, do_read ? "read" : "write");
			print_ns(time);
			printf("Repeat %d avg %s time = ", rindex, do_read ? "read" : "write");
			print_ns(time / index);
			rtime += time/index;
		}
	}

	if (rindex) {
		printf("Average of repeat averages: ");
		print_ns(rtime / rindex);
		printf("Min %s = ", do_read ? "read" : "write");
		print_ns(min);
		printf("Max %s = ", do_read ? "read" : "write");
		print_ns(max);
		printf("Mean: %LG\n", newm);
		printf("Variance: %LG\n", news / (count * repeat - 1));
		printf("StdDev: %G\n", sqrt(news / (count * repeat - 1)));

	}
	return 0;
}
