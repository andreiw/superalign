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

#ifndef SA_STATS_H
#define SA_STATS_H

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>

struct stats {
	long double max;
	long double min;
	long double oldm;
	long double newm;
	long double olds;
	long double news;
	long double per_time;
	long double avg_per_time;
	uintmax_t gindex;
	uintmax_t count;
	uintmax_t repeats;
	int verbose;
	char *op;
};

int stats_init(struct stats *stats,
	       uintmax_t count,
	       int verbose,
	       char *op);
int stats_do(struct stats *stats,
	     long double ns,
	     off64_t pos);
void stats_print(struct stats *stats);
int stats_fini(struct stats *stats);

#endif
