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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "stats.h"

static inline void print_f_u(long double nano, char *postfix)
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

int stats_init(struct stats *stats,
	       uintmax_t count,
	       int verbose,
	       char *op)
{
	memset(stats, 0, sizeof(*stats));
	stats->min = -1ULL;
	stats->verbose = verbose;
	stats->count = count;
	stats->op = op;
	return 0;
}

int stats_do(struct stats *stats,
	     long double ns,
	     off64_t pos)
{
	if (stats->verbose &&
	    (stats->gindex % stats->count == 0)) {
		printf("Repeat %ju:\n", stats->repeats);
	}

	if (stats->verbose > 1) {
		printf("\t(%ju/%ju) -> ", (stats->gindex % stats->count) + 1,
		       stats->count);
		print_f_u(ns, "s\n");
	}

	stats->gindex++;

	stats->per_time += ns;
	if (ns > stats->max)
		stats->max = ns;
	if (ns < stats->min)
		stats->min = ns;
	if (stats->gindex == 0) {
		stats->oldm = stats->newm = ns;
		stats->olds = 0;
	} else {
		stats->newm = stats->oldm + (ns - stats->oldm) /
			stats->gindex;
		stats->news = stats->olds + (ns - stats->oldm) *
			(ns - stats->newm);
		stats->oldm = stats->newm;
		stats->olds = stats->news;
	}

	if (stats->gindex % stats->count == 0) {
		stats->avg_per_time += stats->per_time / stats->count;
		if (stats->verbose) {
			printf("\ttotal time = ");
			print_f_u(stats->per_time, "s\n\taverage time = ");
			print_f_u(stats->per_time / stats->count, "s\n");
		}
		stats->repeats++;
		stats->per_time = 0;		
	}

	return 0;
}

void stats_print(struct stats *stats)
{
	if (stats->repeats > 1) {
		printf("Average of repeat averages: ");
		print_f_u(stats->avg_per_time / stats->repeats, "s\n");
	}

	if (stats->gindex) {
		printf("Global stats:\n");
		printf("\tMin %s speed: ", stats->op);
		print_f_u(stats->min, "s\n");
		printf("\tMax %s speed: ", stats->op);
		print_f_u(stats->max, "s\n");
		printf("\tMean %s speed: ", stats->op);
		print_f_u(stats->newm, "s\n");
		printf("\tVariance: %LG ns^2\n", stats->news / (stats->gindex - 1));
		printf("\tStdDev: %LG ns\n", sqrtl(stats->news / (stats->gindex - 1)));
	}
}

int stats_fini(struct stats *stats)
{
	return 0;
}
