/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2001-2005, Digium
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <stdio.h>

#include "libpri.h"
#include "pri_internal.h"


static int maxsched = 0;

/* Scheduler routines */
int pri_schedule_event(struct pri *pri, int ms, void (*function)(void *data), void *data)
{
	int x;
	struct timeval tv;
	for (x=1;x<MAX_SCHED;x++)
		if (!pri->pri_sched[x].callback)
			break;
	if (x == MAX_SCHED) {
		pri_error(pri, "No more room in scheduler\n");
		return -1;
	}
	if (x > maxsched)
		maxsched = x;
	gettimeofday(&tv, NULL);
	tv.tv_sec += ms / 1000;
	tv.tv_usec += (ms % 1000) * 1000;
	if (tv.tv_usec > 1000000) {
		tv.tv_usec -= 1000000;
		tv.tv_sec += 1;
	}
	pri->pri_sched[x].when = tv;
	pri->pri_sched[x].callback = function;
	pri->pri_sched[x].data = data;
	return x;
}

struct timeval *pri_schedule_next(struct pri *pri)
{
	struct timeval *closest = NULL;
	int x;
	/* Check subchannels */
	if (pri->subchannel)
		closest = pri_schedule_next(pri->subchannel);
	for (x=1;x<MAX_SCHED;x++) {
		if (pri->pri_sched[x].callback && 
			(!closest || (closest->tv_sec > pri->pri_sched[x].when.tv_sec) ||
				((closest->tv_sec == pri->pri_sched[x].when.tv_sec) && 
				 (closest->tv_usec > pri->pri_sched[x].when.tv_usec))))
				 	closest = &pri->pri_sched[x].when;
	}
	return closest;
}

static pri_event *__pri_schedule_run(struct pri *pri, struct timeval *tv)
{
	int x;
	void (*callback)(void *);
	void *data;
	pri_event *e;
	if (pri->subchannel) {
		if ((e = __pri_schedule_run(pri->subchannel, tv))) {
			return e;
		}
	}
	for (x=1;x<MAX_SCHED;x++) {
		if (pri->pri_sched[x].callback &&
			((pri->pri_sched[x].when.tv_sec < tv->tv_sec) ||
			 ((pri->pri_sched[x].when.tv_sec == tv->tv_sec) &&
			  (pri->pri_sched[x].when.tv_usec <= tv->tv_usec)))) {
			        pri->schedev = 0;
			  	callback = pri->pri_sched[x].callback;
				data = pri->pri_sched[x].data;
				pri->pri_sched[x].callback = NULL;
				pri->pri_sched[x].data = NULL;
				callback(data);
            if (pri->schedev)
                  return &pri->ev;
	    }
	}
	return NULL;
}

pri_event *pri_schedule_run(struct pri *pri)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return __pri_schedule_run(pri, &tv);
}


void pri_schedule_del(struct pri *pri,int id)
{
	if ((id >= MAX_SCHED) || (id < 0)) 
		pri_error(pri, "Asked to delete sched id %d???\n", id);
	pri->pri_sched[id].callback = NULL;
}
