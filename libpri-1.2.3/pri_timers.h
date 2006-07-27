/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Mark Spencer <markster@linux-support.net>
 *
 * Copyright (C) 2001, Linux Support Services, Inc.
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
 
#ifndef _PRI_TIMERS_H
#define _PRI_TIMERS_H

/* -1 means we dont currently support the timer/counter */
#define PRI_TIMERS_DEFAULT {	3,	/* N200 */ \
				-1,	/* N201 */ \
				-1,	/* N202 */ \
				7,	/* K */ \
				1000,	/* T200 */ \
				-1,	/* T201 */ \
				-1,	/* T202 */ \
				10000,	/* T203 */ \
				-1,	/* T300 */ \
				-1,	/* T301 */ \
				-1,	/* T302 */ \
				-1,	/* T303 */ \
				-1,	/* T304 */ \
				30000,	/* T305 */ \
				-1,	/* T306 */ \
				-1,	/* T307 */ \
				4000,	/* T308 */ \
				-1,	/* T309 */ \
				-1,	/* T310 */ \
				4000,	/* T313 */ \
				-1,	/* T314 */ \
				-1,	/* T316 */ \
				-1,	/* T317 */ \
				-1,	/* T318 */ \
				-1,	/* T319 */ \
				-1,	/* T320 */ \
				-1,	/* T321 */ \
				-1	/* T322 */ \
			}

/* XXX Only our default timers are setup now XXX */
#define PRI_TIMERS_UNKNOWN PRI_TIMERS_DEFAULT
#define PRI_TIMERS_NI2 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_DMS100 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_LUCENT5E PRI_TIMERS_DEFAULT
#define PRI_TIMERS_ATT4ESS PRI_TIMERS_DEFAULT
#define PRI_TIMERS_EUROISDN_E1 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_EUROISDN_T1 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_NI1 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_GR303_EOC PRI_TIMERS_DEFAULT
#define PRI_TIMERS_GR303_TMC PRI_TIMERS_DEFAULT
#define PRI_TIMERS_QSIG PRI_TIMERS_DEFAULT
#define __PRI_TIMERS_GR303_EOC_INT PRI_TIMERS_DEFAULT
#define __PRI_TIMERS_GR303_TMC_INT PRI_TIMERS_DEFAULT

#define PRI_TIMERS_ALL {	PRI_TIMERS_UNKNOWN, \
				PRI_TIMERS_NI2, \
				PRI_TIMERS_DMS100, \
				PRI_TIMERS_LUCENT5E, \
				PRI_TIMERS_ATT4ESS, \
				PRI_TIMERS_EUROISDN_E1, \
				PRI_TIMERS_EUROISDN_T1, \
				PRI_TIMERS_NI1, \
				PRI_TIMERS_QSIG, \
				PRI_TIMERS_GR303_EOC, \
				PRI_TIMERS_GR303_TMC, \
				__PRI_TIMERS_GR303_EOC_INT, \
				__PRI_TIMERS_GR303_TMC_INT, \
			}

#endif
