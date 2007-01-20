/*
 * Zapata Telephony Interface Driver
 *
 * Written by Mark Spencer <markster@linux-support.net>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
 * 
 * Special thanks to Steve Underwood <steve@coppice.org>
 * for substantial contributions to signal processing functions 
 * in zaptel and the zapata library.
 *
 * Yury Bokhoncovich <byg@cf1.ru>
 * Adaptation for 2.4.20+ kernels (HDLC API was changed)
 * The work has been performed as a part of our move
 * from Cisco 3620 to IBM x305 here in F1 Group
 *
 * Solaris version by simon@slimey.org
 * Solaris patches by joe@thrallingpenguin.com
 *
 * Copyright (C) 2001 Jim Dixon / Zapata Telephony.
 * Copyright (C) 2001 Linux Support Services, Inc.
 *
 * All rights reserved.
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
 * $Id: zaptel.c,v 1.98 2004/11/04 20:04:53 jim Exp $
 */

/* Solaris port by Simon Lockhart. Started 2004-11-28
 * Solaris port continued by Joseph Benden and SolarisVoip.com
 *
 */

#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/poll.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <stddef.h>

/* Must be after other includes */
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include "zconfig.h"

#ifndef PPP_INITFCS
#define PPP_INITFCS 0xffff
#define PPP_GOODFCS     0xf0b8  /* Good final FCS value */
#define PPP_FCS(fcs, c) crc_ccitt_byte(fcs, c)

#endif


#define DDI_NT_ZAP "ddi_zaptel"

#define __ECHO_STATE_MUTE			(1 << 8)
#define ECHO_STATE_IDLE				(0)
#define ECHO_STATE_PRETRAINING		(1 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_STARTTRAINING	(2 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_AWAITINGECHO		(3 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_TRAINING			(4 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_ACTIVE			(5)

/* #define BUF_MUNGE */

/* Grab fasthdlc with tables */
#define FAST_HDLC_NEED_TABLES
#include "fasthdlc.h"

#include "zaptel.h"

/* Get helper arithmetic */
#include "arith.h"

/* macro-oni for determining a unit (channel) number */
#define	UNIT(file) MINOR(file->f_dentry->d_inode->i_rdev)

/* names of tx level settings */
static char *zt_txlevelnames[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
} ;

/* There is a table like this in the PPP driver, too */

static int deftaps = 64;

static 
unsigned short fcstab[256] =
{
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

static inline unsigned int crc_ccitt_byte(unsigned int crc, const unsigned char c)
{
        return (crc >> 8) ^ fcstab[(crc ^ c) & 0xff];
}

static int debug = 0;

/* states for transmit signalling */
typedef enum {ZT_TXSTATE_ONHOOK,ZT_TXSTATE_OFFHOOK,ZT_TXSTATE_START,
	ZT_TXSTATE_PREWINK,ZT_TXSTATE_WINK,ZT_TXSTATE_PREFLASH,
	ZT_TXSTATE_FLASH,ZT_TXSTATE_DEBOUNCE,ZT_TXSTATE_AFTERSTART,
	ZT_TXSTATE_RINGON,ZT_TXSTATE_RINGOFF,ZT_TXSTATE_KEWL,
	ZT_TXSTATE_AFTERKEWL,ZT_TXSTATE_PULSEBREAK,ZT_TXSTATE_PULSEMAKE,
	ZT_TXSTATE_PULSEAFTER
	} ZT_TXSTATE_t;

typedef short sumtype[ZT_MAX_CHUNKSIZE];

static sumtype sums[(ZT_MAX_CONF + 1) * 3];

/* Translate conference aliases into actual conferences 
   and vice-versa */
static short confalias[ZT_MAX_CONF + 1];
static short confrev[ZT_MAX_CONF + 1];

static sumtype *conf_sums_next;
static sumtype *conf_sums;
static sumtype *conf_sums_prev;

static struct zt_span *master;

static struct
{
	int	src;	/* source conf number */
	int	dst;	/* dst conf number */
} conf_links[ZT_MAX_CONF + 1];


/* There are three sets of conference sum accumulators. One for the current
sample chunk (conf_sums), one for the next sample chunk (conf_sums_next), and
one for the previous sample chunk (conf_sums_prev). The following routine 
(rotate_sums) "rotates" the pointers to these accululator arrays as part
of the events of sample chink processing as follows:

The following sequence is designed to be looked at from the reference point
of the receive routine of the master span.

1. All (real span) receive chunks are processed (with putbuf). The last one
to be processed is the master span. The data received is loaded into the
accumulators for the next chunk (conf_sums_next), to be in alignment with
current data after rotate_sums() is called (which immediately follows).
Keep in mind that putbuf is *also* a transmit routine for the pseudo parts
of channels that are in the REALANDPSEUDO conference mode. These channels
are processed from data in the current sample chunk (conf_sums), being
that this is a "transmit" function (for the pseudo part).

2. rotate_sums() is called.

3. All pseudo channel receive chunks are processed. This data is loaded into
the current sample chunk accumulators (conf_sums).

4. All conference links are processed (being that all receive data for this
chunk has already been processed by now).

5. All pseudo channel transmit chunks are processed. This data is loaded from
the current sample chunk accumulators (conf_sums).

6. All (real span) transmit chunks are processed (with getbuf).  This data is
loaded from the current sample chunk accumulators (conf_sums). Keep in mind
that getbuf is *also* a receive routine for the pseudo part of channels that
are in the REALANDPSEUDO conference mode. These samples are loaded into
the next sample chunk accumulators (conf_sums_next) to be processed as part
of the next sample chunk's data (next time around the world).

*/

#define DIGIT_MODE_DTMF 	0
#define DIGIT_MODE_MFV1		1
#define DIGIT_MODE_PULSE	2

#include "digits.h"

static int zt_chan_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp);

static struct zt_timer {
	int dev;		/* Which dev number */
	int ms;			/* Countdown */
	int pos;		/* Position */
	int ping;		/* Whether we've been ping'd */
	int tripped;	/* Whether we're tripped */
	struct zt_timer *next;	/* Linked list */
	struct pollhead sel;
} *zaptimers = NULL;

static kmutex_t zaptimerlock; /* = SPIN_LOCK_UNLOCKED;*/

static kmutex_t bigzaplock; /* = SPIN_LOCK_UNLOCKED; */

struct zt_zone {
	size_t allocsize;
	char name[40];	/* Informational, only */
	int ringcadence[ZT_MAX_CADENCE];
	struct zt_tone *tones[ZT_TONE_MAX]; 
	/* Each of these is a circular list
	   of zt_tones to generate what we
	   want.  Use NULL if the tone is
	   unavailable */
};

#define ZT_DEV_TIMER_BASE	5000
#define ZT_DEV_TIMER_COUNT	1000

#define ZT_DEV_CHAN_BASE	7000
#define ZT_DEV_CHAN_COUNT	1000

static struct zt_span *spans[ZT_MAX_SPANS];
static struct zt_chan *chans[ZT_MAX_CHANNELS]; 

static int chan_map[ZT_DEV_CHAN_COUNT];
static struct zt_timer *chan_timer_map[ZT_DEV_TIMER_COUNT];

static int maxspans = 0;
static int maxchans = 0;
static int maxconfs = 0;
static int maxlinks = 0;

static int default_zone = DEFAULT_TONE_ZONE;

short __zt_mulaw[256];
short __zt_alaw[256];

#ifndef CONFIG_CALC_XLAW
u_char __zt_lin2mu[16384];

u_char __zt_lin2a[16384];
#endif

static u_char defgain[256];

static krwlock_t zone_lock; /* = RW_LOCK_UNLOCKED; */
static krwlock_t chan_lock; /* = RW_LOCK_UNLOCKED; */

static struct zt_zone *tone_zones[ZT_TONE_ZONE_MAX];

#define NUM_SIGS	10	

static void *ztsoftstatep = NULL;

typedef struct zt_soft_state {
  dev_info_t            *dip;
  ddi_acc_handle_t      pci_conf_handle;
} zt_soft_state_t;

static void inline check_pollwakeup(struct zt_chan *chan);
static inline void chan_unlock(struct zt_chan *chan)
{
	mutex_exit(&chan->lock);
	/* check_pollwakeup(chan); */
}

static inline void rotate_sums(void)
{
	/* Rotate where we sum and so forth */
	static int pos = 0;
	conf_sums_prev = sums + (ZT_MAX_CONF + 1) * pos;
	conf_sums = sums + (ZT_MAX_CONF + 1) * ((pos + 1) % 3);
	conf_sums_next = sums + (ZT_MAX_CONF + 1) * ((pos + 2) % 3);
	pos = (pos + 1) % 3;
	bzero(conf_sums_next, maxconfs * sizeof(sumtype));
}

  /* return quiescent (idle) signalling states, for the various signalling types */
static int zt_q_sig(struct zt_chan *chan)
{
int	x;

static unsigned int in_sig[NUM_SIGS][2] = {
	{ ZT_SIG_NONE, 0},
	{ ZT_SIG_EM, 0 | (ZT_ABIT << 8)},
	{ ZT_SIG_FXSLS,ZT_BBIT | (ZT_BBIT << 8)},
	{ ZT_SIG_FXSGS,ZT_ABIT | ZT_BBIT | ((ZT_ABIT | ZT_BBIT) << 8)},
	{ ZT_SIG_FXSKS,ZT_BBIT | ZT_BBIT | ((ZT_ABIT | ZT_BBIT) << 8)},
	{ ZT_SIG_FXOLS,0 | (ZT_ABIT << 8)},
	{ ZT_SIG_FXOGS,ZT_BBIT | ((ZT_ABIT | ZT_BBIT) << 8)},
	{ ZT_SIG_FXOKS,0 | (ZT_ABIT << 8)},
	{ ZT_SIG_SF, 0},
	{ ZT_SIG_EM_E1, ZT_DBIT | ((ZT_ABIT | ZT_DBIT) << 8) },
	} ;

	/* must have span to begin with */
	if (!chan->span) return(-1);
	  /* if RBS does not apply, return error */
	if (!(chan->span->flags & ZT_FLAG_RBS) || 
		!chan->span->rbsbits) return(-1);
	if (chan->sig == ZT_SIG_CAS) {
		static int printed = 0;
		if (printed < 10) {
			printed++;
		}
		return chan->idlebits;
	}
	for (x=0;x<NUM_SIGS;x++) {
		if (in_sig[x][0] == chan->sig) return(in_sig[x][1]);
	}	return(-1); /* not found -- error */
}

static int zt_first_empty_alias(void)
{
	/* Find the first conference which has no alias pointing to it */
	int x;
	for (x=1;x<ZT_MAX_CONF;x++) {
		if (!confrev[x])
			return x;
	}
	return -1;
}

static void recalc_maxconfs(void)
{
	int x;
	for (x=ZT_MAX_CONF-1;x>0;x--) {
		if (confrev[x]) {
			maxconfs = x+1;
			return;
		}
	}
	maxconfs = 0;
}

static void recalc_maxlinks(void)
{
	int x;
	for (x=ZT_MAX_CONF-1;x>0;x--) {
		if (conf_links[x].src || conf_links[x].dst) {
			maxlinks = x+1;
			return;
		}
	}
	maxlinks = 0;
}

static int zt_first_empty_conference(void)
{
	/* Find the first conference which has no alias */
	int x;
	for (x=ZT_MAX_CONF-1;x>0;x--) {
		if (!confalias[x])
			return x;
	}
	return -1;
}

static int zt_get_conf_alias(int x)
{
	int a;
	if (confalias[x]) {
		return confalias[x];
	}

	/* Allocate an alias */
	a = zt_first_empty_alias();
	confalias[x] = a;
	confrev[a] = x;

	/* Highest conference may have changed */
	recalc_maxconfs();
	return a;
}

static void zt_check_conf(int x)
{
	int y;

	/* return if no valid conf number */
	if (x <= 0) return;
	/* Return if there is no alias */
	if (!confalias[x])
		return;
	for (y=0;y<maxchans;y++) {
		if (chans[y] && (chans[y]->confna == x) && (chans[y]->confmode & (ZT_CONF_CONF | ZT_CONF_CONFANN | ZT_CONF_CONFMON | ZT_CONF_CONFANNMON | ZT_CONF_REALANDPSEUDO)))
			return;
	}
	/* If we get here, nobody is in the conference anymore.  Clear it out
	   both forward and reverse */
	confrev[confalias[x]] = 0;
	confalias[x] = 0;

	/* Highest conference may have changed */
	recalc_maxconfs();
}

/* enqueue an event on a channel */
static void __qevent(struct zt_chan *chan, int event, int lock)
{
	if (lock) mutex_enter(&chan->lock);

	  /* if full, ignore */
	if ((chan->eventoutidx == 0) && (chan->eventinidx == (ZT_MAX_EVENTSIZE - 1))) {
		if (lock) chan_unlock(chan);
		return;
	}

	  /* if full, ignore */
	if (chan->eventinidx == (chan->eventoutidx - 1)) {
		if (lock) chan_unlock(chan);
		return;
	}

	  /* save the event */
	chan->eventbuf[chan->eventinidx++] = event;

	  /* wrap the index, if necessary */
	if (chan->eventinidx >= ZT_MAX_EVENTSIZE) 
		chan->eventinidx = 0;

	  /* wake em all up */
	if (chan->iomask & ZT_IOMUX_SIGEVENT) 
		cv_broadcast(&chan->eventbufq);
	cv_broadcast(&chan->readbufq);
	cv_broadcast(&chan->writebufq);
	if (debug) cmn_err(CE_CONT, "__qevent waking %lx, event was 0x%x\n", &chan->sel, event);
	if (lock) chan_unlock(chan);
	pollwakeup(&chan->sel,POLLIN|POLLOUT|POLLPRI);
	return;
}

void zt_qevent_nolock(struct zt_chan *chan, int event)
{
	__qevent(chan, event, 0);
}

void zt_qevent_lock(struct zt_chan *chan, int event)
{
	__qevent(chan, event, 1);
}

#if 0
/* sleep in user space until woken up. Equivilant of tsleep() in BSD */
static int schluffen(wait_queue_head_t *q)
{
	DECLARE_WAITQUEUE(wait, current);
	add_wait_queue(q, &wait);
	current->state = TASK_INTERRUPTIBLE;
	if (!signal_pending(current)) schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(q, &wait);
	if (signal_pending(current)) return ERESTARTSYS;
	return(0);
}
#endif

static inline void calc_fcs(struct zt_chan *ss)
{
	int x;
	unsigned int fcs=PPP_INITFCS;
	unsigned char *data = ss->writebuf[ss->inwritebuf];
	int len = ss->writen[ss->inwritebuf];
	/* Not enough space to do FCS calculation */
	if (len < 2)
		return;
	for (x=0;x<len-2;x++)
		fcs = PPP_FCS(fcs, data[x]);
	fcs ^= 0xffff;
	/* Send out the FCS */
	data[len-2] = (fcs & 0xff);
	data[len-1] = (fcs >> 8) & 0xff;
}

static int zt_reallocbufs(struct zt_chan *ss, int j, int numbufs)
{
	unsigned char *newbuf, *oldbuf;
	unsigned long flags;
	size_t	oldbufsize;
	int x;
	/* Check numbufs */
	if (numbufs < 2)
		numbufs = 2;
	if (numbufs > ZT_MAX_NUM_BUFS)
		numbufs = ZT_MAX_NUM_BUFS;
	/* We need to allocate our buffers now */
	if (j) {
		newbuf = kmem_alloc(j * 2 * numbufs, KM_NOSLEEP);
		if (!newbuf) 
			return (ENOMEM);
	} else
		newbuf = NULL;
	  /* Now that we've allocated our new buffer, we can safely
	     move things around... */
	mutex_enter(&ss->lock);
	oldbufsize = ss->blocksize * 2 * ss->numbufs;
	ss->blocksize = j; /* set the blocksize */
	oldbuf = ss->readbuf[0]; /* Keep track of the old buffer */
	ss->readbuf[0] = NULL;
	if (newbuf) {
		for (x=0;x<numbufs;x++) {
			ss->readbuf[x] = newbuf + x * j;
			ss->writebuf[x] = newbuf + (numbufs + x) * j;
		}
	} else {
		for (x=0;x<numbufs;x++) {
			ss->readbuf[x] = NULL;
			ss->writebuf[x] = NULL;
		}
	}
	/* Mark all buffers as empty */
	for (x=0;x<numbufs;x++) 
		ss->writen[x] = 
		ss->writeidx[x]=
		ss->readn[x]=
		ss->readidx[x] = 0;
	
	/* Keep track of where our data goes (if it goes
	   anywhere at all) */
	if (newbuf) {
		ss->inreadbuf = 0;
		ss->inwritebuf = 0;
	} else {
		ss->inreadbuf = -1;
		ss->inwritebuf = -1;
	}
	ss->outreadbuf = -1;
	ss->outwritebuf = -1;
	ss->numbufs = numbufs;
	if (ss->txbufpolicy == ZT_POLICY_WHEN_FULL)
		ss->txdisable = 1;
	else
		ss->txdisable = 0;

	if (ss->rxbufpolicy == ZT_POLICY_WHEN_FULL)
		ss->rxdisable = 1;
	else
		ss->rxdisable = 0;

	chan_unlock(ss);
	if (oldbuf)
		kmem_free(oldbuf, oldbufsize);
	return 0;
}

static int zt_hangup(struct zt_chan *chan);
static void zt_set_law(struct zt_chan *chan, int law);

/* Pull a ZT_CHUNKSIZE piece off the queue.  Returns
   0 on success or -1 on failure.  If failed, provides
   silence */
static int __buf_pull(struct confq *q, u_char *data, struct zt_chan *c, char *label)
{
	int oldoutbuf = q->outbuf;
	int x;
	/* Ain't nuffin to read */
	if (q->outbuf < 0) {
		if (data)
			for (x = 0; x < ZT_CHUNKSIZE; x++)
				data[x] = ZT_LIN2X(0,c);
		return -1;
	}
	if (data)
		bcopy(q->buf[q->outbuf], data, ZT_CHUNKSIZE);
	q->outbuf = (q->outbuf + 1) % ZT_CB_SIZE;

	/* Won't be nuffin next time */
	if (q->outbuf == q->inbuf) {
		q->outbuf = -1;
	}

	/* If they thought there was no space then
	   there is now where we just read */
	if (q->inbuf < 0) 
		q->inbuf = oldoutbuf;
	return 0;
}

/* Returns a place to put stuff, or NULL if there is
   no room */

static u_char *__buf_pushpeek(struct confq *q)
{
	if (q->inbuf < 0)
		return NULL;
	return q->buf[q->inbuf];
}

static u_char *__buf_peek(struct confq *q)
{
	if (q->outbuf < 0)
		return NULL;
	return q->buf[q->outbuf];
}

#ifdef BUF_MUNGE
static u_char *__buf_cpush(struct confq *q)
{
	int pos;
	/* If we have no space, return where the
	   last space that we *did* have was */
	if (q->inbuf > -1)
		return NULL;
	pos = q->outbuf - 1;
	if (pos < 0)
		pos += ZT_CB_SIZE;
	return q->buf[pos];
}

static void __buf_munge(struct zt_chan *chan, u_char *old, u_char *new)
{
	/* Run a weighted average of the old and new, in order to
	   mask a missing sample */
	int x;
	int val;
	for (x=0;x<ZT_CHUNKSIZE;x++) {
		val = x * ZT_XLAW(new[x], chan) + (ZT_CHUNKSIZE - x - 1) * ZT_XLAW(old[x], chan);
		val = val / (ZT_CHUNKSIZE - 1);
		old[x] = ZT_LIN2X(val, chan);
	}
}
#endif
/* Push something onto the queue, or assume what
   is there is valid if data is NULL */
static int __buf_push(struct confq *q, u_char *data, char *label)
{
	int oldinbuf = q->inbuf;
	if (q->inbuf < 0) {
		return -1;
	}
	if (data)
		/* Copy in the data */
		bcopy(data, q->buf[q->inbuf], ZT_CHUNKSIZE);

	/* Advance the inbuf pointer */
	q->inbuf = (q->inbuf + 1) % ZT_CB_SIZE;

	if (q->inbuf == q->outbuf) {
		/* No space anymore... */	
		q->inbuf = -1;
	}
	/* If they don't think data is ready, let
	   them know it is now */
	if (q->outbuf < 0) {
		q->outbuf = oldinbuf;
	}
	return 0;
}

static void reset_conf(struct zt_chan *chan)
{
	int x;
	/* Empty out buffers and reset to initialization */
	for (x=0;x<ZT_CB_SIZE;x++)
		chan->confin.buf[x] = chan->confin.buffer + ZT_CHUNKSIZE * x;
	chan->confin.inbuf = 0;
	chan->confin.outbuf = -1;

	for (x=0;x<ZT_CB_SIZE;x++)
		chan->confout.buf[x] = chan->confout.buffer + ZT_CHUNKSIZE * x;
	chan->confout.inbuf = 0;
	chan->confout.outbuf = -1;
}


static void close_channel(struct zt_chan *chan)
{
	unsigned long flags;
	void *rxgain = NULL;
	echo_can_state_t *ec = NULL;
	int oldconf;

	zt_reallocbufs(chan, 0, 0); 
	mutex_enter(&chan->lock);
	ec = chan->ec;
	chan->ec = NULL;
	chan->curtone = NULL;
	chan->curzone = NULL;
	chan->cadencepos = 0;
	chan->pdialcount = 0;
	zt_hangup(chan); 
	chan->itimerset = chan->itimer = 0;
	chan->pulsecount = 0;
	chan->pulsetimer = 0;
	chan->ringdebtimer = 0;

	cv_init(&chan->readbufq, NULL, CV_DRIVER, NULL);
	cv_init(&chan->writebufq, NULL, CV_DRIVER, NULL);
	cv_init(&chan->eventbufq, NULL, CV_DRIVER, NULL);
	cv_init(&chan->txstateq, NULL, CV_DRIVER, NULL);

	chan->txdialbuf[0] = '\0';
	chan->digitmode = DIGIT_MODE_DTMF;
	chan->dialing = 0;
	chan->afterdialingtimer = 0;
	  /* initialize IO MUX mask */
	chan->iomask = 0;
	/* save old conf number, if any */
	oldconf = chan->confna;
	  /* initialize conference variables */
	chan->_confn = 0;
	if ((chan->sig & __ZT_SIG_DACS) != __ZT_SIG_DACS) {
		chan->confna = 0;
		chan->confmode = 0;
	}
	chan->confmute = 0;
	/* release conference resource, if any to release */
	if (oldconf) zt_check_conf(oldconf);
	chan->gotgs = 0;
	reset_conf(chan);
	
	if (chan->gainalloc && chan->rxgain)
		rxgain = chan->rxgain;

	chan->rxgain = defgain;
	chan->txgain = defgain;
	chan->gainalloc = 0;
	chan->eventinidx = chan->eventoutidx = 0;
	chan->flags &= ~(ZT_FLAG_LINEAR | ZT_FLAG_PPP | ZT_FLAG_SIGFREEZE);

	zt_set_law(chan,0);

	bzero(chan->conflast, sizeof(chan->conflast));
	bzero(chan->conflast1, sizeof(chan->conflast1));
	bzero(chan->conflast2, sizeof(chan->conflast2));

	chan_unlock(chan);

	if (rxgain)
		kmem_free(rxgain, 512);
	if (ec)
		echo_can_free(ec);

}

static int tone_zone_init(void)
{
	int x;
	for (x=0;x<ZT_TONE_ZONE_MAX;x++)
		tone_zones[x] = NULL;
	return 0;
}

static int free_tone_zone(int num)
{
	struct zt_zone *z;
	if ((num < 0) || (num >= ZT_TONE_ZONE_MAX))
		return EINVAL;
	rw_enter(&zone_lock, RW_WRITER);
	z = tone_zones[num];
	tone_zones[num] = NULL;
	rw_exit(&zone_lock);
	if (z && z->allocsize)
		kmem_free(z, z->allocsize);
	return 0;
}

static int zt_register_tone_zone(int num, struct zt_zone *zone)
{
	int res=0;
	if ((num >= ZT_TONE_ZONE_MAX) || (num < 0))
		return EINVAL;
	rw_enter(&zone_lock, RW_WRITER);
	if (tone_zones[num]) {
		res = EINVAL;
	} else {
		res = 0;
		tone_zones[num] = zone;
	}
	rw_exit(&zone_lock);
	if (!res)
		cmn_err(CE_CONT, "Registered tone zone %d (%s)\n", num, zone->name);
	return res;
}

static int start_tone(struct zt_chan *chan, int tone)
{
	int res = EINVAL;
	/* Stop the current tone, no matter what */
	chan->tonep = 0;
	chan->curtone = NULL;
	chan->pdialcount = 0;
	chan->txdialbuf[0] = '\0';
	chan->dialing =  0;
	if ((tone >= ZT_TONE_MAX) || (tone < -1)) 
		return EINVAL;
	/* Just wanted to stop the tone anyway */
	if (tone < 0)
		return 0;
	if (chan->curzone) {
		/* Have a tone zone */
		if (chan->curzone->tones[tone]) {
			chan->curtone = chan->curzone->tones[tone];
			res = 0;
		} else	/* Indicate that zone is loaded but no such tone exists */
			res = ENOSYS;
	} else	/* Note that no tone zone exists at the moment */
		res = ENODATA;
	if (chan->curtone)
		zt_init_tone_state(&chan->ts, chan->curtone);
	return res;
}

static int set_tone_zone(struct zt_chan *chan, int zone)
{
	int res=0;
	/* Assumes channel is already locked */
	if ((zone >= ZT_TONE_ZONE_MAX) || (zone < -1))
		return EINVAL;
	
	rw_enter(&zone_lock, RW_READER);
	if (zone == -1) {
		zone = default_zone;
	}
	if (tone_zones[zone]) {
		chan->curzone = tone_zones[zone];
		chan->tonezone = zone;
		bcopy(chan->curzone->ringcadence, chan->ringcadence, sizeof(chan->ringcadence));
	} else {
		res = ENODATA;
	}
	
	rw_exit(&zone_lock);
	return res;
}

static void zt_set_law(struct zt_chan *chan, int law)
{
	if (!law) {
		if (chan->deflaw)
			law = chan->deflaw;
		else
			if (chan->span) law = chan->span->deflaw;
			else law = ZT_LAW_MULAW;
	}
	if (law == ZT_LAW_ALAW) {
		chan->xlaw = __zt_alaw;
#ifdef CONFIG_CALC_XLAW
		chan->lineartoxlaw = __zt_lineartoalaw;
#else
		chan->lin2x = __zt_lin2a;
#endif
	} else {
		chan->xlaw = __zt_mulaw;
#ifdef CONFIG_CALC_XLAW
		chan->lineartoxlaw = __zt_lineartoulaw;
#else
		chan->lin2x = __zt_lin2mu;
#endif
	}
}

static int zt_chan_reg(struct zt_chan *chan)
{
	int x;
	int res=0;
	unsigned long flags;
	
	rw_enter(&chan_lock, RW_WRITER);
	for (x=1;x<ZT_MAX_CHANNELS;x++) {
		if (!chans[x]) {
			mutex_init(&chan->lock, NULL, MUTEX_DRIVER, NULL);
			chans[x] = chan;
			if (maxchans < x + 1)
				maxchans = x + 1;
			chan->channo = x;
			if (!chan->master)
				chan->master = chan;
			if (!chan->readchunk)
				chan->readchunk = chan->sreadchunk;
			if (!chan->writechunk)
				chan->writechunk = chan->swritechunk;
			zt_set_law(chan, 0);
			close_channel(chan); 
			/* set this AFTER running close_channel() so that
				HDLC channels wont cause hangage */
			chan->flags |= ZT_FLAG_REGISTERED;
			res = 0;
			break;
		}
	}
	rw_exit(&chan_lock);	
	if (x >= ZT_MAX_CHANNELS)
		cmn_err(CE_CONT, "No more channels available\n");
	return res;
}

char *zt_lboname(int x)
{
	if ((x < 0) || ( x > 7))
		return "Unknown";
	return zt_txlevelnames[x];
}

static void zt_chan_unreg(struct zt_chan *chan)
{
	int x;
	unsigned long flags;

	if (chan == NULL) {
		cmn_err(CE_CONT, "zt_chan_unreg: chan is null\n");
		return;
	}

	rw_enter(&chan_lock, RW_WRITER);
	if (chan->flags & ZT_FLAG_REGISTERED) {
		chans[chan->channo] = NULL;
		chan->flags &= ~ZT_FLAG_REGISTERED;
	}
	maxchans = 0;
	for (x=1;x<ZT_MAX_CHANNELS;x++) 
		if (chans[x]) {
			maxchans = x + 1;
			/* Remove anyone pointing to us as master
			   and make them their own thing */
			if (chans[x]->master == chan) {
				chans[x]->master = chans[x];
			}
			if ((chans[x]->confna == chan->channo) &&
				(((chans[x]->confmode >= ZT_CONF_MONITOR) &&
				(chans[x]->confmode <= ZT_CONF_MONITORBOTH)) ||
				(chans[x]->confmode == ZT_CONF_DIGITALMON))) {
				/* Take them out of conference with us */
				/* release conference resource if any */
				if (chans[x]->confna)
					zt_check_conf(chans[x]->confna);
				chans[x]->confna = 0;
				chans[x]->_confn = 0;
				chans[x]->confmode = 0;
			}
		}
	chan->channo = -1;
	rw_exit(&chan_lock);
}

static ssize_t zt_chan_read(dev_t dev, struct uio *uiop, cred_t *credp)
{
	struct zt_chan *chan;
	int amnt;
	int res, rv;
	int oldbuf,x;
	unsigned long flags;
	int unit;
	int count;

	unit = getminor(dev);
	if (unit >= ZT_DEV_CHAN_BASE && unit < ZT_DEV_CHAN_BASE+ZT_DEV_CHAN_COUNT)
	{
		if (chan_map[unit - ZT_DEV_CHAN_BASE] < 0)
			return -1;
		chan = chans[chan_map[unit - ZT_DEV_CHAN_BASE]];
	}
	else
 		chan = chans[unit];
	count = uiop->uio_resid;

	// cmn_err(CE_CONT, "zaptel: zt_chan_read(unit = %d, count = %d)\n", unit, count);

	/* Make sure count never exceeds 65k, and make sure it's unsigned */
	count &= 0xffff;
	if (!chan) 
		return EINVAL;
	if (count < 1)
		return EINVAL;

	mutex_enter(&chan->lock);
	for(;;) {
		if (chan->eventinidx != chan->eventoutidx) {
			chan_unlock(chan);
			return ELAST;
		}
		res = chan->outreadbuf;
		if (chan->rxdisable)
			res = -1;
		if (res >= 0) break;
		if (uiop->uio_fmode & O_NONBLOCK) {
			chan_unlock(chan);
			return EAGAIN;
		}
		// rv = schluffen(&chan->readbufq);
		// if (rv) return (rv);
		cv_wait(&chan->readbufq, &chan->lock);
	}
	chan_unlock(chan);
	amnt = count;
	if (chan->flags & ZT_FLAG_LINEAR) {
		if (amnt > (chan->readn[chan->outreadbuf] << 1)) 
			amnt = chan->readn[chan->outreadbuf] << 1;
		if (amnt) {
			/* There seems to be a max stack size, so we have
			   to do this in smaller pieces */
			short lindata[128];
			int left = amnt >> 1; /* amnt is in bytes */
			int pos = 0;
			int pass;
			while(left) {
				pass = left;
				if (pass > 128)
					pass = 128;
				for (x=0;x<pass;x++)
					lindata[x] = ZT_XLAW(chan->readbuf[chan->outreadbuf][x + pos], chan);
				if (uiomove(lindata, pass << 1, UIO_READ, uiop))
					return EFAULT;
				left -= pass;
				pos += pass;
			}
		}
	} else {
		if (amnt > chan->readn[chan->outreadbuf]) 
			amnt = chan->readn[chan->outreadbuf];
		if (amnt) {
			if (uiomove(chan->readbuf[chan->outreadbuf], amnt, UIO_READ, uiop))
				return EFAULT;
		}
	}
	mutex_enter(&chan->lock);
	chan->readidx[chan->outreadbuf] = 0;
	chan->readn[chan->outreadbuf] = 0;
	oldbuf = chan->outreadbuf;
	chan->outreadbuf = (chan->outreadbuf + 1) % chan->numbufs;
	if (chan->outreadbuf == chan->inreadbuf) {
		/* Out of stuff */
		chan->outreadbuf = -1;
		if (chan->rxbufpolicy == ZT_POLICY_WHEN_FULL)
			chan->rxdisable = 1;
	}
	if (chan->inreadbuf < 0) {
		/* Notify interrupt handler that we have some space now */
		chan->inreadbuf = oldbuf;
	}
	chan_unlock(chan);
	
	return 0;
}

static int zt_chan_write(dev_t dev, struct uio *uiop, cred_t *credp)
{
	unsigned long flags;
	struct zt_chan *chan;
	int res, amnt, oldbuf, rv,x, unit, count;

	unit = getminor(dev);
	if (unit >= ZT_DEV_CHAN_BASE && unit < ZT_DEV_CHAN_BASE+ZT_DEV_CHAN_COUNT)
	{
		if (chan_map[unit - ZT_DEV_CHAN_BASE] < 0)
			return ENXIO;
		chan = chans[chan_map[unit - ZT_DEV_CHAN_BASE]];
	}
	else
 		chan = chans[unit];

	count = uiop->uio_resid;

// cmn_err(CE_CONT, "zt_chan_write: unit=%d count=%d\n", unit, count);

	/* Make sure count never exceeds 65k, and make sure it's unsigned */
	count &= 0xffff;
	if (!chan) 
		return EINVAL;
	if (count < 1)
		return EINVAL;
	mutex_enter(&chan->lock);
	for(;;) {
		if ((chan->curtone || chan->pdialcount) && !(chan->flags & ZT_FLAG_PSEUDO)) {
			chan->curtone = NULL;
			chan->tonep = 0;
			chan->dialing = 0;
			chan->txdialbuf[0] = '\0';
			chan->pdialcount = 0;
		}
		if (chan->eventinidx != chan->eventoutidx) {
			chan_unlock(chan);
			return ELAST;
		}
		res = chan->inwritebuf;
		if (res >= 0) 
			break;
		if (uiop->uio_fmode & O_NONBLOCK) {
			chan_unlock(chan);
			return EAGAIN;
		}
		/* Wait for something to be available */
		// rv = schluffen(&chan->writebufq);
		// if (rv) return rv;
		cv_wait(&chan->writebufq, &chan->lock);
	}
	chan_unlock(chan);

	amnt = count;
	if (chan->flags & ZT_FLAG_LINEAR) {
		if (amnt > (chan->blocksize << 1))
			amnt = chan->blocksize << 1;
	} else {
		if (amnt > chan->blocksize)
			amnt = chan->blocksize;
	}

#if CONFIG_ZAPATA_DEBUG
	cmn_err(CE_CONT, "zt_chan_write(unit: %d, inwritebuf: %d, outwritebuf: %d amnt: %d\n", 
		unit, chan->inwritebuf, chan->outwritebuf, amnt);
#endif

	if (amnt) {
		if (chan->flags & ZT_FLAG_LINEAR) {
			/* There seems to be a max stack size, so we have
			   to do this in smaller pieces */
			short lindata[128];
			int left = amnt >> 1; /* amnt is in bytes */
			int pos = 0;
			int pass;
			while(left) {
				pass = left;
				if (pass > 128)
					pass = 128;
				if (uiomove(lindata, pass << 1, UIO_WRITE, uiop))
					return EFAULT;
				left -= pass;
				for (x=0;x<pass;x++)
					chan->writebuf[chan->inwritebuf][x + pos] = ZT_LIN2X(lindata[x], chan);
				pos += pass;
			}
			chan->writen[chan->inwritebuf] = amnt >> 1;
		} else {
			uiomove(chan->writebuf[chan->inwritebuf], amnt, UIO_WRITE, uiop);
			chan->writen[chan->inwritebuf] = amnt;
		}
		chan->writeidx[chan->inwritebuf] = 0;
		if (chan->flags & ZT_FLAG_FCS)
			calc_fcs(chan);
		oldbuf = chan->inwritebuf;
		mutex_enter(&chan->lock);
		chan->inwritebuf = (chan->inwritebuf + 1) % chan->numbufs;
		if (chan->inwritebuf == chan->outwritebuf) {
			/* Don't stomp on the transmitter, just wait for them to 
			   wake us up */
			chan->inwritebuf = -1;
			/* Make sure the transmitter is transmitting in case of POLICY_WHEN_FULL */
			chan->txdisable = 0;
		}
		if (chan->outwritebuf < 0) {
			/* Okay, the interrupt handler has been waiting for us.  Give them a buffer */
			chan->outwritebuf = oldbuf;
		}
		chan_unlock(chan);
	}
	return 0;
}

static int zt_ctl_open(dev_t *inode, int flag, int otyp, cred_t *credp)
{
	/* Nothing to do, really */
	return 0;
}

static int zt_chan_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int newdev, x;
	
	/* Allocate a new device */
	newdev = -1;

	for (x = 0; x < ZT_DEV_CHAN_COUNT; x++)
		if (chan_map[x] == -1)
		{
			newdev = x;
			break;
		}

	if (newdev == -1)
		return ENOMEM;

	*devp=makedevice(getmajor(*devp), newdev + ZT_DEV_CHAN_BASE);

	chan_map[newdev] = -2;

	// cmn_err(CE_CONT, "zaptel: channel open, allocated minor %d\n", newdev);
	
	return 0;
}

static int zt_ctl_release(dev_t inode, int flag, int otyp, cred_t *credp)
{
	/* Nothing to do */
	return 0;
}

static int zt_chan_release(dev_t dev, int flag, int otyp, cred_t *credp)
{
	chan_map[getminor(dev) - ZT_DEV_CHAN_BASE] = -1;

	return 0;
}

static void set_txtone(struct zt_chan *ss,int fac, int init_v2, int init_v3)
{
	if (fac == 0)
	{
		ss->v2_1 = 0;
		ss->v3_1 = 0;
		return;
	}
	ss->txtone = fac;
	ss->v1_1 = 0;
	ss->v2_1 = init_v2;
	ss->v3_1 = init_v3;
	return;
}

static void zt_rbs_sethook(struct zt_chan *chan, int txsig, int txstate, int timeout)
{
static int outs[NUM_SIGS][5] = {
/* We set the idle case of the ZT_SIG_NONE to this pattern to make idle E1 CAS
channels happy. Should not matter with T1, since on an un-configured channel, 
who cares what the sig bits are as long as they are stable */
	{ ZT_SIG_NONE, 		ZT_ABIT | ZT_CBIT | ZT_DBIT, 0, 0, 0 },  /* no signalling */
	{ ZT_SIG_EM, 		0, ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT,
		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, 0 },  /* E and M */
	{ ZT_SIG_FXSLS, 	ZT_BBIT | ZT_DBIT, 
		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT,
			ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, 0 }, /* FXS Loopstart */
	{ ZT_SIG_FXSGS, 	ZT_BBIT | ZT_DBIT, 
#ifdef CONFIG_CAC_GROUNDSTART
		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, 0, 0 }, /* FXS Groundstart (CAC-style) */
#else
		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, ZT_ABIT | ZT_CBIT, 0 }, /* FXS Groundstart (normal) */
#endif
	{ ZT_SIG_FXSKS,		ZT_BBIT | ZT_DBIT, 
		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT,
			ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, 0 }, /* FXS Kewlstart */
	{ ZT_SIG_FXOLS,		ZT_BBIT | ZT_DBIT, ZT_BBIT | ZT_DBIT, 0, 0 }, /* FXO Loopstart */
	{ ZT_SIG_FXOGS,		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT,
		 ZT_BBIT | ZT_DBIT, 0, 0 }, /* FXO Groundstart */
	{ ZT_SIG_FXOKS,		ZT_BBIT | ZT_DBIT, ZT_BBIT | ZT_DBIT, 0, 
		ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT }, /* FXO Kewlstart */
	{ ZT_SIG_SF,	ZT_BBIT | ZT_CBIT | ZT_DBIT, 
			ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, 
			ZT_ABIT | ZT_BBIT | ZT_CBIT | ZT_DBIT, 
			ZT_BBIT | ZT_CBIT | ZT_DBIT },  /* no signalling */
	{ ZT_SIG_EM_E1, 	ZT_DBIT, ZT_ABIT | ZT_BBIT | ZT_DBIT,
		ZT_ABIT | ZT_BBIT | ZT_DBIT, ZT_DBIT },  /* E and M  E1 */
	} ;
	int x;

	/* if no span, return doing nothing */
	if (!chan->span) return;
	if (!chan->span->flags & ZT_FLAG_RBS) {
		cmn_err(CE_CONT, "zt_rbs: Tried to set RBS hook state on non-RBS channel %s\n", chan->name);
		return;
	}
	if ((txsig > 3) || (txsig < 0)) {
		cmn_err(CE_CONT, "zt_rbs: Tried to set RBS hook state %d (> 3) on  channel %s\n", txsig, chan->name);
		return;
	}
	if (!chan->span->rbsbits && !chan->span->hooksig) {
		cmn_err(CE_CONT, "zt_rbs: Tried to set RBS hook state %d on channel %s while span %s lacks rbsbits or hooksig function\n",
			txsig, chan->name, chan->span->name);
		return;
	}
	/* Don't do anything for RBS */
	if (chan->sig == ZT_SIG_DACS_RBS)
		return;
	chan->txstate = txstate;
	
	/* if tone signalling */
	if (chan->sig == ZT_SIG_SF)
	{
		chan->txhooksig = txsig;
		if (chan->txtone) /* if set to make tone for tx */
		{
			if ((txsig && !(chan->toneflags & ZT_REVERSE_TXTONE)) ||
			 ((!txsig) && (chan->toneflags & ZT_REVERSE_TXTONE))) 
			{
				set_txtone(chan,chan->txtone,chan->tx_v2,chan->tx_v3);
			}
			else
			{
				set_txtone(chan,0,0,0);
			}
		}
		chan->otimer = timeout * 8;			/* Otimer is timer in samples */
		return;
	}
	if (chan->span->hooksig) {
		if (chan->txhooksig != txsig) {
			chan->txhooksig = txsig;
			chan->span->hooksig(chan, txsig);
		}
		chan->otimer = timeout * 8;			/* Otimer is timer in samples */
		return;
	} else {
		for (x=0;x<NUM_SIGS;x++) {
			if (outs[x][0] == chan->sig) {
#if CONFIG_ZAPATA_DEBUG
				cmn_err(CE_CONT, "Setting bits to %d for channel %s state %d in %d signalling\n", outs[x][txsig + 1], chan->name, txsig, chan->sig);
#endif
				chan->txhooksig = txsig;
				chan->txsig = outs[x][txsig+1];
				chan->span->rbsbits(chan, chan->txsig);
				chan->otimer = timeout * 8;	/* Otimer is timer in samples */
				return;
			}
		}
	}
	cmn_err(CE_CONT, "zt_rbs: Don't know RBS signalling type %d on channel %s\n", chan->sig, chan->name);
}

static int zt_cas_setbits(struct zt_chan *chan, int bits)
{
	/* if no span, return as error */
	if (!chan->span) return -1;
	if (chan->span->rbsbits) {
		chan->txsig = bits;
		chan->span->rbsbits(chan, bits);
	} else {
		cmn_err(CE_CONT, "Huh?  CAS setbits, but no RBS bits function\n");
	}
	return 0;
}

static int zt_hangup(struct zt_chan *chan)
{
	int x,res=0;

	/* Can't hangup pseudo channels */
	if (!chan->span)
		return 0;
	/* Can't hang up a clear channel */
	if (chan->flags & ZT_FLAG_CLEAR)
		return EINVAL;

	chan->kewlonhook = 0;


	if ((chan->sig == ZT_SIG_FXSLS) || (chan->sig == ZT_SIG_FXSKS) ||
		(chan->sig == ZT_SIG_FXSGS)) chan->ringdebtimer = RING_DEBOUNCE_TIME;

	if (chan->span->flags & ZT_FLAG_RBS) {
		if (chan->sig == ZT_SIG_CAS) {
			zt_cas_setbits(chan, chan->idlebits);
		} else if ((chan->sig == ZT_SIG_FXOKS) && (chan->txstate != ZT_TXSTATE_ONHOOK)) {
			/* Do RBS signalling on the channel's behalf */
			zt_rbs_sethook(chan, ZT_TXSIG_KEWL, ZT_TXSTATE_KEWL, ZT_KEWLTIME);
		} else
			zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, ZT_TXSTATE_ONHOOK, 0);
	} else {
		/* Let the driver hang up the line if it wants to  */
		if (chan->span->sethook) {
			if (chan->txhooksig != ZT_ONHOOK) {
				chan->txhooksig = ZT_ONHOOK;
				res = chan->span->sethook(chan, ZT_ONHOOK);
			} else
				res = 0;
		}
	}
	/* if not registered yet, just return here */
	if (!(chan->flags & ZT_FLAG_REGISTERED)) return res;
	/* Mark all buffers as empty */
	for (x = 0;x < chan->numbufs;x++) {
		chan->writen[x] = 
		chan->writeidx[x]=
		chan->readn[x]=
		chan->readidx[x] = 0;
	}	
	if (chan->readbuf[0]) {
		chan->inreadbuf = 0;
		chan->inwritebuf = 0;
	} else {
		chan->inreadbuf = -1;
		chan->inwritebuf = -1;
	}
	chan->outreadbuf = -1;
	chan->outwritebuf = -1;
	chan->dialing = 0;
	chan->afterdialingtimer = 0;
	chan->curtone = NULL;
	chan->pdialcount = 0;
	chan->cadencepos = 0;
	chan->txdialbuf[0] = 0;
	return res;
}

static int initialize_channel(struct zt_chan *chan)
{
	int res;
	unsigned long flags;
	void *rxgain=NULL;
	echo_can_state_t *ec=NULL;
	if ((res = zt_reallocbufs(chan, ZT_DEFAULT_BLOCKSIZE, ZT_DEFAULT_NUM_BUFS)))
		return res;

	mutex_enter(&chan->lock);

	chan->rxbufpolicy = ZT_POLICY_IMMEDIATE;
	chan->txbufpolicy = ZT_POLICY_IMMEDIATE;

	/* Free up the echo canceller if there is one */
	ec = chan->ec;
	chan->ec = NULL;
	chan->echocancel = 0;
	chan->echostate = ECHO_STATE_IDLE;
	chan->echolastupdate = 0;
	chan->echotimer = 0;

	chan->txdisable = 0;
	chan->rxdisable = 0;

	chan->digitmode = DIGIT_MODE_DTMF;
	chan->dialing = 0;
	chan->afterdialingtimer = 0;

	chan->cadencepos = 0;
	chan->firstcadencepos = 0; /* By default loop back to first cadence position */

	/* HDLC & FCS stuff */
	fasthdlc_init(&chan->rxhdlc);
	fasthdlc_init(&chan->txhdlc);
	chan->infcs = PPP_INITFCS;
	
	/* Timings for RBS */
	chan->prewinktime = ZT_DEFAULT_PREWINKTIME;
	chan->preflashtime = ZT_DEFAULT_PREFLASHTIME;
	chan->winktime = ZT_DEFAULT_WINKTIME;
	chan->flashtime = ZT_DEFAULT_FLASHTIME;
	
	if (chan->sig & __ZT_SIG_FXO)
		chan->starttime = ZT_DEFAULT_RINGTIME;
	else
		chan->starttime = ZT_DEFAULT_STARTTIME;
	chan->rxwinktime = ZT_DEFAULT_RXWINKTIME;
	chan->rxflashtime = ZT_DEFAULT_RXFLASHTIME;
	chan->debouncetime = ZT_DEFAULT_DEBOUNCETIME;
	chan->pulsemaketime = ZT_DEFAULT_PULSEMAKETIME;
	chan->pulsebreaktime = ZT_DEFAULT_PULSEBREAKTIME;
	chan->pulseaftertime = ZT_DEFAULT_PULSEAFTERTIME;
	
	/* Initialize RBS timers */
	chan->itimerset = chan->itimer = chan->otimer = 0;
	chan->ringdebtimer = 0;		

	cv_init(&chan->readbufq, NULL, CV_DRIVER, NULL);
	cv_init(&chan->writebufq, NULL, CV_DRIVER, NULL);
	cv_init(&chan->eventbufq, NULL, CV_DRIVER, NULL);
	cv_init(&chan->txstateq, NULL, CV_DRIVER, NULL);

	/* Reset conferences */
	reset_conf(chan);
	
	/* I/O Mask, etc */
	chan->iomask = 0;
	/* release conference resource if any */
	if (chan->confna) zt_check_conf(chan->confna);
	if ((chan->sig & __ZT_SIG_DACS) != __ZT_SIG_DACS) {
		chan->confna = 0;
		chan->confmode = 0;
	}
	chan->_confn = 0;
	bzero(chan->conflast, sizeof(chan->conflast));
	bzero(chan->conflast1, sizeof(chan->conflast1));
	bzero(chan->conflast2, sizeof(chan->conflast2));
	chan->confmute = 0;
	chan->gotgs = 0;
	chan->curtone = NULL;
	chan->tonep = 0;
	chan->pdialcount = 0;
	set_tone_zone(chan, -1);
	if (chan->gainalloc && chan->rxgain)
		rxgain = chan->rxgain;
	chan->rxgain = defgain;
	chan->txgain = defgain;
	chan->gainalloc = 0;
	chan->eventinidx = chan->eventoutidx = 0;
	zt_set_law(chan,0);
	zt_hangup(chan);

	/* Make sure that the audio flag is cleared on a clear channel */	
	if (chan->sig & ZT_SIG_CLEAR) 
		chan->flags &= ~ZT_FLAG_AUDIO;

	if (chan->sig == ZT_SIG_CLEAR)
		chan->flags &= ~(ZT_FLAG_PPP | ZT_FLAG_FCS | ZT_FLAG_HDLC);

	chan->flags &= ~ZT_FLAG_LINEAR;
	if (chan->curzone) {
		/* Take cadence from tone zone */
		bcopy(chan->curzone->ringcadence, chan->ringcadence, sizeof(chan->ringcadence));
	} else {
		/* Do a default */
		bzero(chan->ringcadence, sizeof(chan->ringcadence));
		chan->ringcadence[0] = chan->starttime;
		chan->ringcadence[1] = ZT_RINGOFFTIME;
	}
	chan_unlock(chan);

	if (rxgain)
		kmem_free(rxgain, 512);
	if (ec)
		echo_can_free(ec);
	return 0;
}

static int zt_timing_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	struct zt_timer *t;
	unsigned long flags;
	int newdev, x;

	t = kmem_alloc(sizeof(struct zt_timer), KM_NOSLEEP);
	if (!t)
		return ENOMEM;

	/* Allocate a new device */
	newdev = -1;

	for (x = 0; x < ZT_DEV_TIMER_COUNT; x++)
		if (chan_timer_map[x] == NULL)
		{
			newdev = x;
			break;
		}

	if (newdev == -1)
		return ENOMEM;

	*devp=makedevice(getmajor(*devp), newdev + ZT_DEV_TIMER_BASE);

	chan_timer_map[newdev] = t;

	/* Allocate a new timer */
	bzero(t, sizeof(struct zt_timer));
	t->dev = newdev;

	mutex_enter(&zaptimerlock);
	t->next = zaptimers;
	zaptimers = t;
	mutex_exit(&zaptimerlock);
	return 0;
}

static int zt_timer_release(dev_t dev, int flag, int otyp, cred_t *credp)
{
	struct zt_timer *t, *cur, *prev;
	unsigned long flags;

	t = chan_timer_map[getminor(dev) - ZT_DEV_TIMER_BASE];
	chan_timer_map[getminor(dev) - ZT_DEV_TIMER_BASE] = NULL;

	if (t) {
		mutex_enter(&zaptimerlock);
		prev = NULL;
		cur = zaptimers;
		while(cur) {
			if (t == cur)
				break;
			prev = cur;
			cur = cur->next;
		}
		if (cur) {
			if (prev)
				prev->next = cur->next;
			else
				zaptimers = cur->next;
		}
		mutex_exit(&zaptimerlock);
		if (!cur) {
			cmn_err(CE_CONT, "Zap Timer: Not on list??\n");
			return 0;
		}
		kmem_free(t, sizeof(struct zt_timer));
	}
	return 0;
}

static int zt_specchan_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int res = 0;
	int unit = getminor(*devp);

	// cmn_err(CE_CONT, "zt_specchan_open: unit=%d\n", unit);

	if (chans[unit] && chans[unit]->sig) {
		/* Make sure we're not already open, a net device, or a slave device */
		if (chans[unit]->flags & ZT_FLAG_OPEN) 
			res = EBUSY;
		else if (chans[unit]->flags & ZT_FLAG_NETDEV)
			res = EBUSY;
		else if (chans[unit]->master != chans[unit])
			res = EBUSY;
		else if ((chans[unit]->sig & __ZT_SIG_DACS) == __ZT_SIG_DACS)
			res = EBUSY;
		else {
			/* Assume everything is going to be okay */
			res = initialize_channel(chans[unit]);
			if (chans[unit]->flags & ZT_FLAG_PSEUDO) 
				chans[unit]->flags |= ZT_FLAG_AUDIO;
			if (chans[unit]->span && chans[unit]->span->open)
				res = chans[unit]->span->open(chans[unit]);
			if (!res) {
				// chans[unit]->file = file;
				chans[unit]->flags |= ZT_FLAG_OPEN;
			} else {
				close_channel(chans[unit]);
			}
		}
	} else
		res = ENXIO;
	return res;
}

static int zt_specchan_release(dev_t dev, int flag, int otyp, cred_t *credp)
{
	int res=0;
	int unit, unit1 = getminor(dev) - ZT_DEV_CHAN_BASE;

	if (unit1 < 0 || unit1 > ZT_DEV_CHAN_COUNT)
		return ENXIO;

	unit = chan_map[unit1];

	if (chans[unit]) {
		chans[unit]->flags &= ~ZT_FLAG_OPEN;
		// chans[unit]->file = NULL;
		close_channel(chans[unit]);
		if (chans[unit]->span && chans[unit]->span->close)
			res = chans[unit]->span->close(chans[unit]);
	} else
		res = ENXIO;
	chan_map[unit1] = -1;
	return res;
}

static struct zt_chan *zt_alloc_pseudo(void)
{
	struct zt_chan *pseudo;
	unsigned long flags;
	/* Don't allow /dev/zap/pseudo to open if there are no spans */
	if (maxspans < 1)
		return NULL;
	pseudo = kmem_alloc(sizeof(struct zt_chan), KM_NOSLEEP);
	if (!pseudo)
		return NULL;
	bzero(pseudo, sizeof(struct zt_chan));
	pseudo->sig = ZT_SIG_CLEAR;
	pseudo->sigcap = ZT_SIG_CLEAR;
	pseudo->flags = ZT_FLAG_PSEUDO | ZT_FLAG_AUDIO;
	mutex_enter(&bigzaplock);
	if (zt_chan_reg(pseudo)) {
		kmem_free(pseudo, sizeof(struct zt_chan));
		pseudo = NULL;
	} else
		sprintf(pseudo->name, "Pseudo/%d", pseudo->channo);
	mutex_exit(&bigzaplock);
	return pseudo;	
}

static void zt_free_pseudo(struct zt_chan *pseudo)
{
	unsigned long flags;
	if (pseudo) {
		mutex_enter(&bigzaplock);
		zt_chan_unreg(pseudo);
		mutex_exit(&bigzaplock);
		kmem_free(pseudo, sizeof(struct zt_chan));
	}
}

static int zt_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int unit = getminor(*devp);
	struct zt_chan *chan;
	/* Minor 0: Special "control" descriptor */
	if (!unit) 
		return zt_ctl_open(devp, flag, otyp, credp);
	if (unit == 253) {
		if (maxspans) {
			return zt_timing_open(devp, flag, otyp, credp);
		} else {
			return ENXIO;
		}
	}
	if (unit == 254)
		return zt_chan_open(devp, flag, otyp, credp);
	if (unit == 255) {
		if (maxspans) {
			chan = zt_alloc_pseudo();
			if (chan) {
				*devp = makedevice(getmajor(*devp), chan->channo);
				if (debug) cmn_err(CE_CONT, "zaptel: zt_open(pseudo), dev now = %d\n", *devp);
				return zt_specchan_open(devp, flag, otyp, credp);
			} else {
				return ENXIO;
			}
		} else
			return ENXIO;
	}
	return zt_specchan_open(devp, flag, otyp, credp);
}

static int zt_read(dev_t dev, struct uio *uiop, cred_t *credp)
{
	int unit = getminor(dev);
	struct zt_chan *chan;

	// cmn_err(CE_CONT, "zaptel: zt_read(unit = %d)\n", unit);

	/* Can't read from control */
	if (!unit) {
		return EINVAL;
	}
	
	if (unit == 253) 
		return EINVAL;
	
	if (unit == 254) {
#if 0
// SL FIXME
		chan = file->private_data;
		if (!chan)
			return EINVAL;
		return zt_chan_read(file, usrbuf, count, chan->channo);
#endif
	}
	
	if (unit == 255) {
#if 0
// SL Not needed?
		chan = file->private_data;
		if (!chan) {
			cmn_err(CE_CONT, "No pseudo channel structure to read?\n");
			return EINVAL;
		}
		return zt_chan_read(file, usrbuf, count, chan->channo);
#else
		return EINVAL;
#endif
	}
	if (uiop->uio_resid < 0)
		return EINVAL;

	return zt_chan_read(dev, uiop, credp);
}

static int zt_write(dev_t dev, struct uio *uiop, cred_t *credp)
{
	int unit = getminor(dev);
	struct zt_chan *chan;
	/* Can't read from control */
	if (!unit)
		return EINVAL;
	if (uiop->uio_resid < 0)
		return EINVAL;
	if (unit == 253)
		return EINVAL;
	if (unit == 254) {
#if 0
// SL FIXME
		chan = file->private_data;
		if (!chan)
			return EINVAL;
		return zt_chan_write(file, usrbuf, count, chan->channo);
#endif
		return EINVAL;
	}
	if (unit == 255) {
		return EINVAL;
#if 0
		chan = file->private_data;
		if (!chan) {
			cmn_err(CE_CONT, "No pseudo channel structure to read?\n");
			return EINVAL;
		}
		return zt_chan_write(file, usrbuf, count, chan->channo);
#endif
	}
	return zt_chan_write(dev, uiop, credp);
	
}

/* No bigger than 32k for everything per tone zone */
#define MAX_SIZE 32768
/* No more than 64 subtones */
#define MAX_TONES 64

static int
ioctl_load_zone(unsigned long data, int mode)
{
	struct zt_tone *samples[MAX_TONES];
	short next[MAX_TONES];
	struct zt_tone_def_header th;
	void *slab, *ptr;
	long size;
	struct zt_zone *z;
	struct zt_tone_def td;
	struct zt_tone *t;
	int x;
	int space;
	int res;
	
	/* XXX Unnecessary XXX */
	bzero(samples, sizeof(samples));
	/* XXX Unnecessary XXX */
	bzero(next, sizeof(next));
	ddi_copyin((void *)data, &th, sizeof(th), mode);
	if ((th.count < 0) || (th.count > MAX_TONES)) {
		cmn_err(CE_CONT, "Too many tones included\n");
		return EINVAL;
	}
	space = size = sizeof(struct zt_zone) +
			th.count * sizeof(struct zt_tone);
	if ((size > MAX_SIZE) || (size < 0))
		return E2BIG;
	ptr = slab = (char *)kmem_alloc(size, KM_NOSLEEP);
	if (!slab)
		return ENOMEM;
	/* Zero it out for simplicity */
	bzero(slab, size);
	/* Grab the zone */
	z = (struct zt_zone *)slab;
	z->allocsize = size;
	strncpy(z->name, th.name, sizeof(z->name) - 1);
	for (x=0;x<ZT_MAX_CADENCE;x++)
		z->ringcadence[x] = th.ringcadence[x];
	data += sizeof(struct zt_tone_def_header);
	ptr += sizeof(struct zt_zone);
	space -= sizeof(struct zt_zone);
	for (x=0;x<th.count;x++) {
		if (space < sizeof(struct zt_tone)) {
			/* Check space for zt_tone struct */
			kmem_free(slab,size);
			cmn_err(CE_CONT, "Insufficient tone zone space\n");
			return EINVAL;
		}
		if (ddi_copyin((void *)data, &td, sizeof(struct zt_tone_def), mode)) {
			kmem_free(slab, size);
			return EIO;
		}
		/* Index the current sample */
		samples[x] = t = (struct zt_tone *)ptr;
		/* Remember which sample is next */
		next[x] = td.next;
		/* Make sure the "next" one is sane */
		if ((next[x] >= th.count) || (next[x] < 0)) {
			cmn_err(CE_CONT, "Invalid 'next' pointer\n");
			kmem_free(slab, size);
			return EINVAL;
		}
		if (td.tone >= ZT_TONE_MAX) {
			cmn_err(CE_CONT, "Too many tones defined\n");
			/* Make sure it's sane */
			kmem_free(slab, size);
			return EINVAL;
		}
		/* Update pointers to account for zt_tone header */
		space -= sizeof(struct zt_tone);
		ptr += sizeof(struct zt_tone);
		data += sizeof(struct zt_tone_def);
		/* Fill in tonedata, datalen, and tonesamples fields */
		t->tonesamples = td.samples;
		t->fac1 = td.fac1;
		t->init_v2_1 = td.init_v2_1;
		t->init_v3_1 = td.init_v3_1;
		t->fac2 = td.fac2;
		t->init_v2_2 = td.init_v2_2;
		t->init_v3_2 = td.init_v3_2;
		t->modulate = td.modulate;
		t->next = NULL;					/* XXX Unnecessary XXX */
		if (!z->tones[td.tone])
			z->tones[td.tone] = t;
	}
	for (x=0;x<th.count;x++) 
		/* Set "next" pointers */
		samples[x]->next = samples[next[x]];

	/* Actually register zone */
	res = zt_register_tone_zone(th.zone, z);
	if (res)
		kmem_free(slab, size);
	return res;
}

void zt_init_tone_state(struct zt_tone_state *ts, struct zt_tone *zt)
{
	ts->v1_1 = 0;
	ts->v2_1 = zt->init_v2_1;
	ts->v3_1 = zt->init_v3_1;
	ts->v1_2 = 0;
	ts->v2_2 = zt->init_v2_2;
	ts->v3_2 = zt->init_v3_2;
	ts->modulate = zt->modulate;
}

struct zt_tone *zt_dtmf_tone(char digit, int mf)
{
	struct zt_tone *z;

	if (!mf)
		z = dtmf_tones;
	else
		z = mfv1_tones;
	switch(digit) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return z + (int)(digit - '0');
	case '*':
		return z + 10;
	case '#':
		return z + 11;
	case 'A':
	case 'B':
	case 'C':
		return z + (digit + 12 - 'A');
	case 'D':
		if (!mf)
			return z + ( digit + 12 - 'A');
		return NULL;
	case 'a':
	case 'b':
	case 'c':
		return z + (digit + 12 - 'a');
	case 'd':
		if (!mf)
			return z + ( digit + 12 - 'a');
		return NULL;
	case 'W':
	case 'w':
		return &tone_pause;
	}
	return NULL;
}

static void __do_dtmf(struct zt_chan *chan)
{
	char c;
	/* Called with chan->lock held */
	while (strlen(chan->txdialbuf)) {
		c = chan->txdialbuf[0];
		/* Skooch */
		bcopy(chan->txdialbuf + 1, chan->txdialbuf, sizeof(chan->txdialbuf) - 1);
		switch(c) {
		case 'T':
		case 't':
			chan->digitmode = DIGIT_MODE_DTMF;
			chan->tonep = 0;
			break;
		case 'M':
		case 'm':
			chan->digitmode = DIGIT_MODE_MFV1;
			chan->tonep = 0;
			break;
		case 'P':
		case 'p':
			chan->digitmode = DIGIT_MODE_PULSE;
			chan->tonep = 0;
			break;
		default:
			if (debug) cmn_err(CE_CONT, "Dialing %c\n", c);
			if (chan->digitmode == DIGIT_MODE_PULSE)
			{
				if ((c >= '0') && (c <= '9') && (chan->txhooksig == ZT_TXSIG_OFFHOOK))
				{
					chan->pdialcount = c - '0';
					/* a '0' is ten pulses */
					if (!chan->pdialcount) chan->pdialcount = 10;
					zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, 
						ZT_TXSTATE_PULSEBREAK, chan->pulsebreaktime);
					return;
				}
			} else {
				chan->curtone = zt_dtmf_tone(c, (chan->digitmode == DIGIT_MODE_MFV1)); 
				chan->tonep = 0;
				/* All done */
				if (chan->curtone) {
					zt_init_tone_state(&chan->ts, chan->curtone);
					return;
				}
			}
		}
	}
	/* Notify userspace process if there is nothing left */
	chan->dialing = 0;
	zt_qevent_nolock(chan, ZT_EVENT_DIALCOMPLETE);
}

static int zt_release(dev_t dev, int flag, int otyp, cred_t *credp)
{
	int unit = getminor(dev);
	int res;
	struct zt_chan *chan;

	if (!unit) 
		return zt_ctl_release(dev, flag, otyp, credp);
	if (unit >= ZT_DEV_TIMER_BASE && unit <= ZT_DEV_TIMER_BASE + ZT_DEV_TIMER_COUNT) {
		return zt_timer_release(dev, flag, otyp, credp);
	}
	if (unit >= ZT_DEV_CHAN_BASE && unit <= ZT_DEV_CHAN_BASE + ZT_DEV_CHAN_COUNT) {
		if (chan_map[unit - ZT_DEV_CHAN_BASE] < 0)
			return zt_chan_release(dev, flag, otyp, credp);
		else
			return zt_specchan_release(dev, flag, otyp, credp);
	}
	if (unit == 253 || unit == 254 || unit == 255) {
		/* Shouldn't happen, but just in case... */
		return 0;
	}

	res = zt_specchan_release(dev, flag, otyp, credp);
	if (chans[unit]->flags & ZT_FLAG_PSEUDO)
		zt_free_pseudo(chans[unit]);

	return res;
}

void zt_alarm_notify(struct zt_span *span)
{
	int j;
	int x;

	span->alarms &= ~ZT_ALARM_LOOPBACK;
	/* Determine maint status */
	if (span->maintstat || span->mainttimer)
		span->alarms |= ZT_ALARM_LOOPBACK;
	/* DON'T CHANGE THIS AGAIN. THIS WAS DONE FOR A REASON.
 	   The expression (a != b) does *NOT* do the same thing
	   as ((!a) != (!b)) */
	/* if change in general state */
	if ((!span->alarms) != (!span->lastalarms)) {
		if (span->alarms)
			j = ZT_EVENT_ALARM;
		else
			j = ZT_EVENT_NOALARM;
		span->lastalarms = span->alarms;
		for (x=0;x < span->channels;x++)
			zt_qevent_lock(&span->chans[x], j);
		/* Switch to other master if current master in alarm */
		for (x=1; x<maxspans; x++) {
			if (spans[x] && !spans[x]->alarms && (spans[x]->flags & ZT_FLAG_RUNNING)) {
				if(master != spans[x])
					cmn_err(CE_CONT, "Zaptel: Master changed to %s\n", spans[x]->name);
				master = spans[x];
				break;
			}
		}
	}
}

#define VALID_SPAN(j) do { \
	if ((j >= ZT_MAX_SPANS) || (j < 1)) \
		return EINVAL; \
	if (!spans[j]) \
		return ENXIO; \
} while(0)

#define CHECK_VALID_SPAN(j) do { \
	/* Start a given span */ \
	if (ddi_copyin((void *)data, &j, sizeof(int), mode)) \
		return EFAULT; \
	VALID_SPAN(j); \
} while(0)

#define VALID_CHANNEL(j) do { \
	if ((j >= ZT_MAX_CHANNELS) || (j < 1)) \
		return EINVAL; \
	if (!chans[j]) \
		return ENXIO; \
} while(0)

static int zt_timer_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	int j;
	unsigned long flags;
	struct zt_timer	*timer;

	timer = chan_timer_map[getminor(dev) - ZT_DEV_TIMER_BASE];
	
	if (!timer)
		return EINVAL;
	
	switch(cmd) {
	case ZT_TIMERCONFIG:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if (j < 0)
			j = 0;
		mutex_enter(&zaptimerlock);
		timer->ms = timer->pos = j;
		mutex_exit(&zaptimerlock);
		break;
	case ZT_TIMERACK:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		mutex_enter(&zaptimerlock);
		if ((j < 1) || (j > timer->tripped))
			j = timer->tripped;
		timer->tripped -= j;
		mutex_exit(&zaptimerlock);
		break;
	case ZT_GETEVENT:  /* Get event on queue */
		j = ZT_EVENT_NONE;
		mutex_enter(&zaptimerlock);
		  /* set up for no event */
		if (timer->tripped)
			j = ZT_EVENT_TIMER_EXPIRED;
		if (timer->ping)
			j = ZT_EVENT_TIMER_PING;
		mutex_exit(&zaptimerlock);
		ddi_copyout(&j, (void *)data, sizeof(int), mode);
		break;
	case ZT_TIMERPING:
		mutex_enter(&zaptimerlock);
		timer->ping = 1;
		mutex_exit(&zaptimerlock);
		pollwakeup(&timer->sel, POLLPRI|POLLERR);
		break;
	case ZT_TIMERPONG:
		mutex_enter(&zaptimerlock);
		timer->ping = 0;
		mutex_exit(&zaptimerlock);
		break;
	default:
		return ENOTTY;
	}
	return 0;
}

static int zt_common_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	union {
		struct zt_gains gain;
		struct zt_spaninfo span;
		struct zt_params param;
	} stack;
	struct zt_chan *chan;
#ifdef ALLOW_CHAN_DIAG
	/* This structure is huge and will bork a 4k stack */
	struct zt_chan mychan;
	unsigned long flags;
#endif	
	int i,j;
	int unit = getminor(dev);

	switch(cmd) {
	case ZT_GET_PARAMS: /* get channel timing parameters */
		ddi_copyin((void *)data, &stack.param, sizeof(stack.param), mode);
		/* Pick the right channo's */
		if (!stack.param.channo || unit) {
			stack.param.channo = unit;
		}
		/* Check validity of channel */
		VALID_CHANNEL(stack.param.channo);
		chan = chans[stack.param.channo];

		  /* point to relevant structure */
		stack.param.sigtype = chan->sig;  /* get signalling type */
		/* return non-zero if rx not in idle state */
		if (chan->span) {
			j = zt_q_sig(chan); 
			if (j >= 0) { /* if returned with success */
				stack.param.rxisoffhook = ((chan->rxsig & (j >> 8)) != (j & 0xff));
			}
			else {
				stack.param.rxisoffhook = ((chan->rxhooksig != ZT_RXSIG_ONHOOK) &&
					(chan->rxhooksig != ZT_RXSIG_INITIAL));
			}
		} else stack.param.rxisoffhook = 0;
		if (chan->span && chan->span->rbsbits && !(chan->sig & ZT_SIG_CLEAR)) {
			stack.param.rxbits = chan->rxsig;
			stack.param.txbits = chan->txsig;
			stack.param.idlebits = chan->idlebits;
		} else {
			stack.param.rxbits = -1;
			stack.param.txbits = -1;
			stack.param.idlebits = 0;
		}
		if (chan->span && (chan->span->rbsbits || chan->span->hooksig) && 
			!(chan->sig & ZT_SIG_CLEAR)) {
			stack.param.rxhooksig = chan->rxhooksig;
			stack.param.txhooksig = chan->txhooksig;
		} else {
			stack.param.rxhooksig = -1;
			stack.param.txhooksig = -1;
		}
		stack.param.prewinktime = chan->prewinktime; 
		stack.param.preflashtime = chan->preflashtime;		
		stack.param.winktime = chan->winktime;
		stack.param.flashtime = chan->flashtime;
		stack.param.starttime = chan->starttime;
		stack.param.rxwinktime = chan->rxwinktime;
		stack.param.rxflashtime = chan->rxflashtime;
		stack.param.debouncetime = chan->debouncetime;
		stack.param.channo = chan->channo;
		stack.param.pulsemaketime = chan->pulsemaketime;
		stack.param.pulsebreaktime = chan->pulsebreaktime;
		stack.param.pulseaftertime = chan->pulseaftertime;
		if (chan->span) stack.param.spanno = chan->span->spanno;
			else stack.param.spanno = 0;
		strncpy(stack.param.name, chan->name, sizeof(stack.param.name) - 1);
		stack.param.chanpos = chan->chanpos;
		/* Return current law */
		if (chan->xlaw == __zt_alaw)
			stack.param.curlaw = ZT_LAW_ALAW;
		else
			stack.param.curlaw = ZT_LAW_MULAW;
		ddi_copyout(&stack.param, (void *)data, sizeof(stack.param), mode);
		break;
	case ZT_SET_PARAMS: /* set channel timing stack.paramters */
		ddi_copyin((void *)data, &stack.param, sizeof(stack.param), mode);
		/* Pick the right channo's */
		if (!stack.param.channo || unit) {
			stack.param.channo = unit;
		}
		/* Check validity of channel */
		VALID_CHANNEL(stack.param.channo);
		chan = chans[stack.param.channo];
		  /* point to relevant structure */
		/* NOTE: sigtype is *not* included in this */
		  /* get timing stack.paramters */
		chan->prewinktime = stack.param.prewinktime;
		chan->preflashtime = stack.param.preflashtime;
		chan->winktime = stack.param.winktime;
		chan->flashtime = stack.param.flashtime;
		chan->starttime = stack.param.starttime;
		/* Update ringtime if not using a tone zone */
		if (!chan->curzone)
			chan->ringcadence[0] = chan->starttime;
		chan->rxwinktime = stack.param.rxwinktime;
		chan->rxflashtime = stack.param.rxflashtime;
		chan->debouncetime = stack.param.debouncetime;
		chan->pulsemaketime = stack.param.pulsemaketime;
		chan->pulsebreaktime = stack.param.pulsebreaktime;
		chan->pulseaftertime = stack.param.pulseaftertime;
		break;
	case ZT_GETGAINS:  /* get gain stuff */
		if (ddi_copyin((void *)data, &stack.gain, sizeof(stack.gain), mode))
			return EIO;
		i = stack.gain.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = unit;
		  /* make sure channel number makes sense */
		if ((i < 0) || (i > ZT_MAX_CHANNELS) || !chans[i]) return(EINVAL);
		
		if (!(chans[i]->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		stack.gain.chan = i; /* put the span # in here */
		for (j=0;j<256;j++)  {
			stack.gain.txgain[j] = chans[i]->txgain[j];
			stack.gain.rxgain[j] = chans[i]->rxgain[j];
		}
		if (ddi_copyout(&stack.gain, (void *)data, sizeof(stack.gain), mode))
			return EIO;
		break;
	case ZT_SETGAINS:  /* set gain stuff */
		if (ddi_copyin((void *)data, &stack.gain, sizeof(stack.gain), mode))
			return EIO;
		i = stack.gain.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = unit;
		  /* make sure channel number makes sense */
		if ((i < 0) || (i > ZT_MAX_CHANNELS) || !chans[i]) return(EINVAL);
		if (!(chans[i]->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		if (!chans[i]->gainalloc) {
			chans[i]->rxgain = kmem_alloc(512, KM_NOSLEEP);
			if (!chans[i]->rxgain) {
				chans[i]->rxgain = defgain;
				return ENOMEM;
			} else {
				chans[i]->gainalloc = 1;
				chans[i]->txgain = chans[i]->rxgain + 256;
			}
		}
		stack.gain.chan = i; /* put the span # in here */
		for (j=0;j<256;j++) {
			chans[i]->rxgain[j] = stack.gain.rxgain[j];
			chans[i]->txgain[j] = stack.gain.txgain[j];
		}
		if (!bcmp(chans[i]->rxgain, defgain, 256) && 
		    !bcmp(chans[i]->txgain, defgain, 256)) {
			/* This is really just a normal gain, so 
			   deallocate the memory and go back to defaults */
			if (chans[i]->gainalloc)
				kmem_free(chans[i]->rxgain, 512);
			chans[i]->rxgain = defgain;
			chans[i]->txgain = defgain;
			chans[i]->gainalloc = 0;
		}
		if (ddi_copyout(&stack.gain, (void *)data, sizeof(stack.gain), mode))
			return EIO;
		break;
	case ZT_SPANSTAT:
		ddi_copyin((void *)data, &stack.span, sizeof(stack.span), mode);
		i = stack.span.spanno; /* get specified span number */
		if ((i < 0) || (i >= maxspans)) return(EINVAL);  /* if bad span no */
		if (i == 0) /* if to figure it out for this chan */
		   {
		   	if (!chans[unit])
				return EINVAL;
			i = chans[unit]->span->spanno;
		   }
		if (!spans[i])
			return EINVAL;
		stack.span.spanno = i; /* put the span # in here */
		stack.span.totalspans = 0;
		if (maxspans) stack.span.totalspans = maxspans - 1; /* put total number of spans here */
		strncpy(stack.span.desc, spans[i]->desc, sizeof(stack.span.desc) - 1);
		strncpy(stack.span.name, spans[i]->name, sizeof(stack.span.name) - 1);
		stack.span.alarms = spans[i]->alarms;		/* get alarm status */
		stack.span.bpvcount = spans[i]->bpvcount;	/* get BPV count */
		stack.span.rxlevel = spans[i]->rxlevel;	/* get rx level */
		stack.span.txlevel = spans[i]->txlevel;	/* get tx level */
		stack.span.crc4count = spans[i]->crc4count;	/* get CRC4 error count */
		stack.span.ebitcount = spans[i]->ebitcount;	/* get E-bit error count */
		stack.span.fascount = spans[i]->fascount;	/* get FAS error count */
		stack.span.irqmisses = spans[i]->irqmisses;	/* get IRQ miss count */
		stack.span.syncsrc = spans[i]->syncsrc;	/* get active sync source */
		stack.span.totalchans = spans[i]->channels;
		stack.span.numchans = 0;
		for (j=0; j < spans[i]->channels; j++)
			if (spans[i]->chans[j].sig)
				stack.span.numchans++;
		ddi_copyout(&stack.span, (void *)data, sizeof(stack.span), mode);
		break;
#ifdef ALLOW_CHAN_DIAG
	case ZT_CHANDIAG:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		/* make sure its a valid channel number */
		if ((j < 1) || (j >= maxchans))
			return EINVAL;
		/* if channel not mapped, not there */
		if (!chans[j]) return EINVAL;
		/* lock irq state */
		mutex_enter(&chans[j]->lock);
		/* make static copy of channel */
		bcopy(chans[j],&mychan,sizeof(struct zt_chan));
		/* let irq's go */
		chan_unlock(chans[j]);
		cmn_err(CE_CONT, "Dump of Zaptel Channel %d (%s,%d,%d):\n\n",j,
			mychan.name,mychan.channo,mychan.chanpos);
		cmn_err(CE_CONT, "flags: %x hex, writechunk: %08lx, readchunk: %08lx\n",
			mychan.flags, (long) mychan.writechunk, (long) mychan.readchunk);
		cmn_err(CE_CONT, "rxgain: %08lx, txgain: %08lx, gainalloc: %d\n",
			(long) mychan.rxgain, (long)mychan.txgain, mychan.gainalloc);
		cmn_err(CE_CONT, "span: %08lx, sig: %x hex, sigcap: %x hex\n",
			(long)mychan.span, mychan.sig, mychan.sigcap);
		cmn_err(CE_CONT, "inreadbuf: %d, outreadbuf: %d, inwritebuf: %d, outwritebuf: %d\n",
			mychan.inreadbuf, mychan.outreadbuf, mychan.inwritebuf, mychan.outwritebuf);
		cmn_err(CE_CONT, "blocksize: %d, numbufs: %d, txbufpolicy: %d, txbufpolicy: %d\n",
			mychan.blocksize, mychan.numbufs, mychan.txbufpolicy, mychan.rxbufpolicy);
		cmn_err(CE_CONT, "txdisable: %d, rxdisable: %d, iomask: %d\n",
			mychan.txdisable, mychan.rxdisable, mychan.iomask);
		cmn_err(CE_CONT, "curzone: %08lx, tonezone: %d, curtone: %08lx, tonep: %d\n",
			(long) mychan.curzone, mychan.tonezone, (long) mychan.curtone, mychan.tonep);
		cmn_err(CE_CONT, "digitmode: %d, txdialbuf: %s, dialing: %d, aftdialtimer: %d, cadpos. %d\n",
			mychan.digitmode, mychan.txdialbuf, mychan.dialing,
				mychan.afterdialingtimer, mychan.cadencepos);
		cmn_err(CE_CONT, "confna: %d, confn: %d, confmode: %d, confmute: %d\n",
			mychan.confna, mychan._confn, mychan.confmode, mychan.confmute);
		cmn_err(CE_CONT, "ec: %08lx, echocancel: %d, deflaw: %d, xlaw: %08lx\n",
			(long) mychan.ec, mychan.echocancel, mychan.deflaw, (long) mychan.xlaw);
		cmn_err(CE_CONT, "echostate: %02x, echotimer: %d, echolastupdate: %d\n",
			(int) mychan.echostate, mychan.echotimer, mychan.echolastupdate);
		cmn_err(CE_CONT, "itimer: %d, otimer: %d, ringdebtimer: %d\n\n",
			mychan.itimer,mychan.otimer,mychan.ringdebtimer);
#if 0
		if (mychan.ec) {
			int x;
			/* Dump the echo canceller parameters */
			for (x=0;x<mychan.ec->taps;x++) {
				cmn_err(CE_CONT, "tap %d: %d\n", x, mychan.ec->fir_taps[x]);
			}
		}
#endif
#endif /* ALLOW_CHAN_DIAG */
		break;
	default:
		return ENOTTY;
	}
	return 0;
}

static int (*zt_dynamic_ioctl)(int cmd, intptr_t data, int mode);

void zt_set_dynamic_ioctl(int (*func)(int cmd, intptr_t data, int mode)) 
{
	zt_dynamic_ioctl = func;
}

static void recalc_slaves(struct zt_chan *chan)
{
	int x;
	struct zt_chan *last = chan;

	/* Makes no sense if you don't have a span */
	if (!chan->span)
		return;

#if CONFIG_ZAPATA_DEBUG
	cmn_err(CE_CONT, "Recalculating slaves on %s\n", chan->name);
#endif

	/* Link all slaves appropriately */
	for (x=chan->chanpos;x<chan->span->channels;x++)
		if (chan->span->chans[x].master == chan) {
#if CONFIG_ZAPATA_DEBUG
			cmn_err(CE_CONT, "Channel %s, slave to %s, last is %s, its next will be %d\n", 
			       chan->span->chans[x].name, chan->name, last->name, x);
#endif
			last->nextslave = x;
			last = &chan->span->chans[x];
		}
	/* Terminate list */
	last->nextslave = 0;
#if CONFIG_ZAPATA_DEBUG
	cmn_err(CE_CONT, "Done Recalculating slaves on %s (last is %s)\n", chan->name, last->name);
#endif
}

static void inline check_pollwakeup(struct zt_chan *chan)
{
	if (chan->pollwake) {
		pollwakeup(&chan->sel, chan->pollwake);
		chan->pollwake = 0;
	}
	if (chan->master->pollwake) {
		pollwakeup(&chan->master->sel, chan->master->pollwake);
		chan->master->pollwake = 0;
	}
}

static int zt_ctl_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	/* I/O CTL's for control interface */
	int i,j;
	struct zt_lineconfig lc;
	struct zt_chanconfig ch;
	struct zt_sfconfig sf;
	int sigcap;
	int res = 0;
	int x,y;
	struct zt_chan *newmaster;
	struct zt_dialparams tdp;
	struct zt_maintinfo maint;
	struct zt_indirect_data ind;
	unsigned long flags;
	int rv;

	switch(cmd) {
	case ZT_INDIRECT:
		if (ddi_copyin((void *)data, &ind, sizeof(ind), mode))
			return EFAULT;
		VALID_CHANNEL(ind.chan);
		return zt_chan_ioctl(makedevice(getmajor(dev),ind.chan+ZT_DEV_CHAN_BASE), ind.op, (unsigned long) ind.data, mode, credp, rvalp);
	case ZT_SPANCONFIG:
		if (ddi_copyin((void *)data, &lc, sizeof(lc), mode))
			return EFAULT;
		VALID_SPAN(lc.span);
		if ((lc.lineconfig & 0xf0 & spans[lc.span]->linecompat) != (lc.lineconfig & 0xf0))
			return EINVAL;
		if (spans[lc.span]->spanconfig)
			return spans[lc.span]->spanconfig(spans[lc.span], &lc);
		return 0;
	case ZT_STARTUP:
		CHECK_VALID_SPAN(j);
		if (spans[j]->flags & ZT_FLAG_RUNNING)
			return 0;
		if (spans[j]->startup)
			res = spans[j]->startup(spans[j]);
		if (!res) {
			/* Mark as running and hangup any channels */
			spans[j]->flags |= ZT_FLAG_RUNNING;
			for (x=0;x<spans[j]->channels;x++) {
				y = zt_q_sig(&spans[j]->chans[x]) & 0xff;
				if (y >= 0) spans[j]->chans[x].rxsig = (unsigned char)y;
				mutex_enter(&spans[j]->chans[x].lock);
				zt_hangup(&spans[j]->chans[x]);
				chan_unlock(&spans[j]->chans[x]);
				spans[j]->chans[x].rxhooksig = ZT_RXSIG_INITIAL;
			}
		}
		return 0;
	case ZT_SHUTDOWN:
		CHECK_VALID_SPAN(j);
		if (spans[j]->shutdown)
			res =  spans[j]->shutdown(spans[j]);
		spans[j]->flags &= ~ZT_FLAG_RUNNING;
		return 0;
	case ZT_CHANCONFIG:
		if (ddi_copyin((void *)data, &ch, sizeof(ch), mode))
			return EFAULT;
		VALID_CHANNEL(ch.chan);
		if (ch.sigtype == ZT_SIG_SLAVE) {
			/* We have to use the master's sigtype */
			if ((ch.master < 1) || (ch.master >= ZT_MAX_CHANNELS))
				return EINVAL;
			if (!chans[ch.master])
				return EINVAL;
			ch.sigtype = chans[ch.master]->sig;
			newmaster = chans[ch.master];
		} else if ((ch.sigtype & __ZT_SIG_DACS) == __ZT_SIG_DACS) {
			newmaster = chans[ch.chan];
			if ((ch.idlebits < 1) || (ch.idlebits >= ZT_MAX_CHANNELS))
				return EINVAL;
			if (!chans[ch.idlebits])
				return EINVAL;
		} else {
			newmaster = chans[ch.chan];
		}
		mutex_enter(&chans[ch.chan]->lock);
		if (ch.sigtype == ZT_SIG_HDLCNET) {
				chan_unlock(chans[ch.chan]);
				cmn_err(CE_CONT, "Zaptel networking not supported by this build.\n");
				return ENOSYS;
		}
		sigcap = chans[ch.chan]->sigcap;
		/* If they support clear channel, then they support the HDLC and such through
		   us.  */
		if (sigcap & ZT_SIG_CLEAR) 
			sigcap |= (ZT_SIG_HDLCRAW | ZT_SIG_HDLCFCS | ZT_SIG_HDLCNET | ZT_SIG_DACS);
		
		if ((sigcap & ch.sigtype) != ch.sigtype)
			res =  EINVAL;	
		
		if (!res && chans[ch.chan]->span->chanconfig)
			res = chans[ch.chan]->span->chanconfig(chans[ch.chan], ch.sigtype);
		if (chans[ch.chan]->master) {
			/* Clear the master channel */
			recalc_slaves(chans[ch.chan]->master);
			chans[ch.chan]->nextslave = 0;
		}
		if (!res) {
			chans[ch.chan]->sig = ch.sigtype;
			if (chans[ch.chan]->sig == ZT_SIG_CAS)
				chans[ch.chan]->idlebits = ch.idlebits;
			else
				chans[ch.chan]->idlebits = 0;
			if ((ch.sigtype & ZT_SIG_CLEAR) == ZT_SIG_CLEAR) {
				/* Set clear channel flag if appropriate */
				chans[ch.chan]->flags &= ~ZT_FLAG_AUDIO;
				chans[ch.chan]->flags |= ZT_FLAG_CLEAR;
			} else {
				/* Set audio flag and not clear channel otherwise */
				chans[ch.chan]->flags |= ZT_FLAG_AUDIO;
				chans[ch.chan]->flags &= ~ZT_FLAG_CLEAR;
			}
			if ((ch.sigtype & ZT_SIG_HDLCRAW) == ZT_SIG_HDLCRAW) {
				/* Set the HDLC flag */
				chans[ch.chan]->flags |= ZT_FLAG_HDLC;
			} else {
				/* Clear the HDLC flag */
				chans[ch.chan]->flags &= ~ZT_FLAG_HDLC;
			}
			if ((ch.sigtype & ZT_SIG_HDLCFCS) == ZT_SIG_HDLCFCS) {
				/* Set FCS to be calculated if appropriate */
				chans[ch.chan]->flags |= ZT_FLAG_FCS;
			} else {
				/* Clear FCS flag */
				chans[ch.chan]->flags &= ~ZT_FLAG_FCS;
			}
			if ((ch.sigtype & __ZT_SIG_DACS) == __ZT_SIG_DACS) {
				/* Setup conference properly */
				chans[ch.chan]->confmode = ZT_CONF_DIGITALMON;
				chans[ch.chan]->confna = ch.idlebits;
			}
			chans[ch.chan]->master = newmaster;
			/* Note new slave if we are not our own master */
			if (newmaster != chans[ch.chan]) {
				recalc_slaves(chans[ch.chan]->master);
			}
		}
		if ((chans[ch.chan]->sig == ZT_SIG_HDLCNET) && 
		    (chans[ch.chan] == newmaster) &&
		    !(chans[ch.chan]->flags & ZT_FLAG_NETDEV))
			cmn_err(CE_CONT, "Unable to register HDLC device for channel %s\n", chans[ch.chan]->name);
		if (!res) {
			/* Setup default law */
			chans[ch.chan]->deflaw = ch.deflaw;
			/* Copy back any modified settings */
			chan_unlock(chans[ch.chan]);
			if (ddi_copyout(&ch, (void *)data, sizeof(ch), mode))
				return EFAULT;
			mutex_enter(&chans[ch.chan]->lock);
			/* And hangup */
			zt_hangup(chans[ch.chan]);
			y = zt_q_sig(chans[ch.chan]) & 0xff;
			if (y >= 0) chans[ch.chan]->rxsig = (unsigned char)y;
			chans[ch.chan]->rxhooksig = ZT_RXSIG_INITIAL;
		}
#if CONFIG_ZAPATA_DEBUG
		cmn_err(CE_CONT, "Configured channel %s, flags %04x, sig %04x\n", chans[ch.chan]->name, chans[ch.chan]->flags, chans[ch.chan]->sig);
#endif		
		chan_unlock(chans[ch.chan]);
		return res;
	case ZT_SFCONFIG:
		if (ddi_copyin((void *)data, &sf, sizeof(sf), mode))
			return EFAULT;
		VALID_CHANNEL(sf.chan);
		if (chans[sf.chan]->sig != ZT_SIG_SF) return EINVAL;
		mutex_enter(&chans[sf.chan]->lock);
		chans[sf.chan]->rxp1 = sf.rxp1;
		chans[sf.chan]->rxp2 = sf.rxp2;
		chans[sf.chan]->rxp3 = sf.rxp3;
		chans[sf.chan]->txtone = sf.txtone;
		chans[sf.chan]->tx_v2 = sf.tx_v2;
		chans[sf.chan]->tx_v3 = sf.tx_v3;
		chans[sf.chan]->toneflags = sf.toneflag;
		if (sf.txtone) /* if set to make tone for tx */
		{
			if ((chans[sf.chan]->txhooksig && !(sf.toneflag & ZT_REVERSE_TXTONE)) ||
			 ((!chans[sf.chan]->txhooksig) && (sf.toneflag & ZT_REVERSE_TXTONE))) 
			{
				set_txtone(chans[sf.chan],sf.txtone,sf.tx_v2,sf.tx_v3);
			}
			else
			{
				set_txtone(chans[sf.chan],0,0,0);
			}
		}
		chan_unlock(chans[sf.chan]);
		return res;
	case ZT_DEFAULTZONE:
		if (ddi_copyin((void *)data, &j, sizeof(int), mode))
			return EFAULT;  /* get conf # */
		if ((j < 0) || (j >= ZT_TONE_ZONE_MAX)) return (EINVAL);
		rw_enter(&zone_lock, RW_WRITER);
		default_zone = j;
		rw_exit(&zone_lock);
		return 0;
	case ZT_LOADZONE:
		return ioctl_load_zone(data, mode);
	case ZT_FREEZONE:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if ((j < 0) || (j >= ZT_TONE_ZONE_MAX)) return (EINVAL);
		free_tone_zone(j);
		return 0;
	case ZT_SET_DIALPARAMS:
		if (ddi_copyin((void *)data, &tdp, sizeof(tdp), mode))
			return EIO;
		if ((tdp.dtmf_tonelen > 4000) || (tdp.dtmf_tonelen < 10))
			return EINVAL;
		if ((tdp.mfv1_tonelen > 4000) || (tdp.mfv1_tonelen < 10))
			return EINVAL;
		for (i=0;i<16;i++)
			dtmf_tones[i].tonesamples = tdp.dtmf_tonelen * 8;
		dtmf_silence.tonesamples = tdp.dtmf_tonelen * 8;
		for (i=0;i<15;i++)
			mfv1_tones[i].tonesamples = tdp.mfv1_tonelen * 8;
		mfv1_silence.tonesamples = tdp.mfv1_tonelen * 8;
		/* Special case for K/P tone */
		mfv1_tones[10].tonesamples = tdp.mfv1_tonelen * 8 * 5 / 3;
		break;
	case ZT_GET_DIALPARAMS:
		tdp.dtmf_tonelen = dtmf_tones[0].tonesamples / 8;
		tdp.mfv1_tonelen = mfv1_tones[0].tonesamples / 8;
		tdp.reserved[0] = 0;
		tdp.reserved[1] = 0;
		tdp.reserved[2] = 0;
		tdp.reserved[3] = 0;
		if (ddi_copyin(&tdp, (void *)data, sizeof(tdp), mode))
			return EIO;
		break;
	case ZT_MAINT:  /* do maintence stuff */
		  /* get struct from user */
		if (ddi_copyin((void *)data, &maint, sizeof(maint), mode))
			return EIO;
		/* must be valid span number */
		if ((maint.spanno < 1) || (maint.spanno > ZT_MAX_SPANS) || (!spans[maint.spanno]))
			return EINVAL;
		if (!spans[maint.spanno]->maint)
			return ENOSYS;
		mutex_enter(&spans[maint.spanno]->lock);
		  /* save current maint state */
		i = spans[maint.spanno]->maintstat;
		  /* set maint mode */
		spans[maint.spanno]->maintstat = maint.command;
		switch(maint.command) {
		case ZT_MAINT_NONE:
		case ZT_MAINT_LOCALLOOP:
		case ZT_MAINT_REMOTELOOP:
			/* if same, ignore it */
			if (i == maint.command) break;
			rv = spans[maint.spanno]->maint(spans[maint.spanno], maint.command);
			mutex_exit(&spans[maint.spanno]->lock);
			if (rv) return rv;
			mutex_enter(&spans[maint.spanno]->lock);
			break;
		case ZT_MAINT_LOOPUP:
		case ZT_MAINT_LOOPDOWN:
			spans[maint.spanno]->mainttimer = ZT_LOOPCODE_TIME * 8;
			rv = spans[maint.spanno]->maint(spans[maint.spanno], maint.command);
			cv_wait(&spans[maint.spanno]->maintq, &spans[maint.spanno]->lock);
			break;
		default:
			cmn_err(CE_CONT, "zaptel: Unknown maintenance event: %d\n", maint.command);
		}
		zt_alarm_notify(spans[maint.spanno]);  /* process alarm-related events */
		mutex_exit(&spans[maint.spanno]->lock);
		break;
	case ZT_DYNAMIC_CREATE:
	case ZT_DYNAMIC_DESTROY:
		if (zt_dynamic_ioctl)
			return zt_dynamic_ioctl(cmd, data, mode);
		else {
			// request_module("ztdynamic");
			if (zt_dynamic_ioctl)
				return zt_dynamic_ioctl(cmd, data, mode);
		}
		return ENOSYS;
	default:
		return zt_common_ioctl(dev, cmd, data, mode, credp, rvalp);
	}
	return 0;
}

static int zt_chanandpseudo_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	struct zt_chan *chan;
	union {
		struct zt_dialoperation tdo;
		struct zt_bufferinfo bi;
		struct zt_confinfo conf;
		struct zt_ring_cadence cad;
	} stack;
	unsigned long flags, flagso;
	int i, j, k, rv;
	int ret, c, unit;
	
	unit = getminor(dev);
 	chan = chans[unit];
	if (!chan)
		return EINVAL;
	
	switch(cmd) {
	case ZT_DIALING:
		mutex_enter(&chan->lock);
		j = chan->dialing;
		chan_unlock(chan);
		if (ddi_copyout(&j, (void *)data, sizeof(int), mode))
			return EIO;
		return 0;
	case ZT_DIAL:
		if (ddi_copyin((void *)data, &stack.tdo, sizeof(stack.tdo), mode))
			return EIO;
		rv = 0;
		/* Force proper NULL termination */
		stack.tdo.dialstr[ZT_MAX_DTMF_BUF - 1] = '\0';
		mutex_enter(&chan->lock);
		switch(stack.tdo.op) {
		case ZT_DIAL_OP_CANCEL:
			chan->curtone = NULL;
			chan->dialing = 0;
			chan->txdialbuf[0] = '\0';
			chan->tonep = 0;
			chan->pdialcount = 0;
			break;
		case ZT_DIAL_OP_REPLACE:
			strcpy(chan->txdialbuf, stack.tdo.dialstr);
			chan->dialing = 1;
			__do_dtmf(chan);
			break;
		case ZT_DIAL_OP_APPEND:
			if (strlen(stack.tdo.dialstr) + strlen(chan->txdialbuf) >= ZT_MAX_DTMF_BUF)
			   {
				rv = EBUSY;
				break;
			   }
			strncpy(chan->txdialbuf + strlen(chan->txdialbuf), stack.tdo.dialstr, ZT_MAX_DTMF_BUF - strlen(chan->txdialbuf));
			if (!chan->dialing)
			   {
				chan->dialing = 1;
				__do_dtmf(chan);
			   }
			break;
		default:
			rv = EINVAL;
		}
		chan_unlock(chan);
		return rv;
	case ZT_GET_BUFINFO:
		stack.bi.rxbufpolicy = chan->rxbufpolicy;
		stack.bi.txbufpolicy = chan->txbufpolicy;
		stack.bi.numbufs = chan->numbufs;
		stack.bi.bufsize = chan->blocksize;
		/* XXX FIXME! XXX */
		stack.bi.readbufs = -1;
		stack.bi.writebufs = -1;
		if (ddi_copyout(&stack.bi, (void *)data, sizeof(stack.bi), mode))
			return EIO;
		break;
	case ZT_SET_BUFINFO:
		if (ddi_copyin((void *)data, &stack.bi, sizeof(stack.bi), mode))
			return EIO;
		if (stack.bi.bufsize > ZT_MAX_BLOCKSIZE)
			return EINVAL;
		if (stack.bi.bufsize < 16)
			return EINVAL;
		if (stack.bi.bufsize * stack.bi.numbufs > ZT_MAX_BUF_SPACE)
			return EINVAL;
		chan->rxbufpolicy = stack.bi.rxbufpolicy & 0x1;
		chan->txbufpolicy = stack.bi.txbufpolicy & 0x1;
		if ((rv = zt_reallocbufs(chan,  stack.bi.bufsize, stack.bi.numbufs)))
			return (rv);
		break;
	case ZT_GET_BLOCKSIZE:  /* get blocksize */
		ddi_copyout(&chan->blocksize, (void *)data, sizeof(int), mode);
		break;
	case ZT_SET_BLOCKSIZE:  /* set blocksize */
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		  /* cannot be larger than max amount */
		if (j > ZT_MAX_BLOCKSIZE) return(EINVAL);
		  /* cannot be less then 16 */
		if (j < 16) return(EINVAL);
		  /* allocate a single kernel buffer which we then
		     sub divide into four pieces */
		if ((rv = zt_reallocbufs(chan, j, chan->numbufs)))
			return (rv);
		break;
	case ZT_FLUSH:  /* flush input buffer, output buffer, and/or event queue */
		ddi_copyin((void *)data, &i, sizeof(int), mode);
		mutex_enter(&chan->lock);
		if (i & ZT_FLUSH_READ)  /* if for read (input) */
		   {
			  /* initialize read buffers and pointers */
			chan->inreadbuf = 0;
			chan->outreadbuf = -1;
			for (j=0;j<chan->numbufs;j++) {
				/* Do we need this? */
				chan->readn[j] = 0;
				chan->readidx[j] = 0;
			}
			if (debug) cmn_err(CE_CONT, "ZT_FLUSH waking %lx\n", &chan->sel);
			cv_broadcast(&chan->readbufq);  /* wake_up_interruptible waiting on read */
			pollwakeup(&chan->sel, POLLIN);
		   }
		if (i & ZT_FLUSH_WRITE) /* if for write (output) */
		   {
			  /* initialize write buffers and pointers */
			chan->outwritebuf = -1;
			chan->inwritebuf = 0;
			for (j=0;j<chan->numbufs;j++) {
				/* Do we need this? */
				chan->writen[j] = 0;
				chan->writeidx[j] = 0;
			}
			cv_broadcast(&chan->writebufq); /* wake_up_interruptible waiting on write */
			pollwakeup(&chan->sel, POLLOUT);
			   /* if IO MUX wait on write empty, well, this
				certainly *did* empty the write */
			if (chan->iomask & ZT_IOMUX_WRITEEMPTY)
				cv_broadcast(&chan->eventbufq); /* wake_up_interruptible waiting on IOMUX */
		   }
		if (i & ZT_FLUSH_EVENT) /* if for events */
		   {
			   /* initialize the event pointers */
			chan->eventinidx = chan->eventoutidx = 0;
		   }
		chan_unlock(chan);
		break;
	case ZT_SYNC:  /* wait for no tx */
		mutex_enter(&chan->lock);
		for(;;)  /* loop forever */
		   {
			  /* Know if there is a write pending */
			i = (chan->outwritebuf > -1);
			if (!i) break; /* skip if none */
			cv_wait(&chan->writebufq, &chan->lock);
			// rv = schluffen(&chan->writebufq);
			// if (rv) return(rv);
		   }
		chan_unlock(chan);
		break;
	case ZT_IOMUX: /* wait for something to happen */
		ddi_copyin((void *)data, &chan->iomask, sizeof(int), mode);
		if (!chan->iomask) return(EINVAL);  /* cant wait for nothing */
		mutex_enter(&chan->lock);
		for(;;)  /* loop forever */
		   {
			  /* has to have SOME mask */
			ret = 0;  /* start with empty return value */
			  /* if looking for read */
			if (chan->iomask & ZT_IOMUX_READ)
			   {
				/* if read available */
				if ((chan->outreadbuf > -1)  && !chan->rxdisable)
					ret |= ZT_IOMUX_READ;
			   }
			  /* if looking for write avail */
			if (chan->iomask & ZT_IOMUX_WRITE)
			   {
				if (chan->inwritebuf > -1)
					ret |= ZT_IOMUX_WRITE;
			   }
			  /* if looking for write empty */
			if (chan->iomask & ZT_IOMUX_WRITEEMPTY)
			   {
				  /* if everything empty -- be sure the transmitter is enabled */
				chan->txdisable = 0;
				if (chan->outwritebuf < 0)
					ret |= ZT_IOMUX_WRITEEMPTY;
			   }
			  /* if looking for signalling event */
			if (chan->iomask & ZT_IOMUX_SIGEVENT)
			   {
				  /* if event */
				if (chan->eventinidx != chan->eventoutidx)
					ret |= ZT_IOMUX_SIGEVENT;
			   }
			  /* if something to return, or not to wait */
			if (ret || (chan->iomask & ZT_IOMUX_NOWAIT))
			   {
				  /* set return value */
				ddi_copyout(&ret, (void *)data, sizeof(int), mode);
				break; /* get out of loop */
			   }
			// rv = schluffen(&chan->eventbufq);
			// if (rv) return(rv);
			cv_wait(&chan->eventbufq, &chan->lock);
		   }
		  /* clear IO MUX mask */
		chan_unlock(chan);
		chan->iomask = 0;
		break;
	case ZT_GETEVENT:  /* Get event on queue */
		  /* set up for no event */
		j = ZT_EVENT_NONE;
		mutex_enter(&chan->lock);
		  /* if some event in queue */
		if (chan->eventinidx != chan->eventoutidx)
		   {
			j = chan->eventbuf[chan->eventoutidx++];
			  /* get the data, bump index */
			  /* if index overflow, set to beginning */
			if (chan->eventoutidx >= ZT_MAX_EVENTSIZE)
				chan->eventoutidx = 0;
		   }		
		chan_unlock(chan);
		ddi_copyout(&j, (void *)data, sizeof(int), mode);
		break;
	case ZT_CONFMUTE:  /* set confmute flag */
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if (!(chan->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		mutex_enter(&bigzaplock);
		chan->confmute = j;
		mutex_exit(&bigzaplock);
		break;
	case ZT_GETCONFMUTE:  /* get confmute flag */
		if (!(chan->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		j = chan->confmute;
		ddi_copyout(&j, (void *)data, sizeof(int), mode);
		rv = 0;
		break;
	case ZT_SETTONEZONE:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		mutex_enter(&chan->lock);
		rv =  set_tone_zone(chan, j);
		chan_unlock(chan);
		return rv;
	case ZT_GETTONEZONE:
		mutex_enter(&chan->lock);
		if (chan->curzone)
			rv = chan->tonezone;
		else
			rv = default_zone;
		chan_unlock(chan);
		ddi_copyout(&rv, (void *)data, sizeof(int), mode);
		break;
	case ZT_SENDTONE:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		mutex_enter(&chan->lock);
		rv = start_tone(chan, j);	
		chan_unlock(chan);
		return rv;
	case ZT_GETCONF:  /* get conf stuff */
		ddi_copyin((void *)data, &stack.conf, sizeof(stack.conf), mode);
		i = stack.conf.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = chan->channo;
		  /* make sure channel number makes sense */
		if ((i < 0) || (i > ZT_MAX_CONF) || (!chans[i])) return(EINVAL);
		if (!(chans[i]->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		stack.conf.chan = i;  /* get channel number */
		stack.conf.confno = chans[i]->confna;  /* get conference number */
		stack.conf.confmode = chans[i]->confmode; /* get conference mode */
		ddi_copyout(&stack.conf, (void *)data, sizeof(stack.conf), mode);
		break;
	case ZT_SETCONF:  /* set conf stuff */
		ddi_copyin((void *)data, &stack.conf, sizeof(stack.conf), mode);
		i = stack.conf.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = chan->channo;
		  /* make sure channel number makes sense */
		if ((i < 1) || (i > ZT_MAX_CHANNELS) || (!chans[i])) return(EINVAL);
		if (!(chans[i]->flags & ZT_FLAG_AUDIO)) return (EINVAL); 
		if (stack.conf.confmode && ((stack.conf.confmode & ZT_CONF_MODE_MASK) < 4)) {
			/* Monitor mode -- it's a channel */
			if ((stack.conf.confno < 0) || (stack.conf.confno >= ZT_MAX_CHANNELS) || !chans[stack.conf.confno]) return(EINVAL);
		} else {
			  /* make sure conf number makes sense, too */
			if ((stack.conf.confno < -1) || (stack.conf.confno > ZT_MAX_CONF)) return(EINVAL);
		}
			
		  /* if taking off of any conf, must have 0 mode */
		if ((!stack.conf.confno) && stack.conf.confmode) return(EINVAL);
		  /* likewise if 0 mode must have no conf */
		if ((!stack.conf.confmode) && stack.conf.confno) return (EINVAL);
		stack.conf.chan = i;  /* return with real channel # */
		mutex_enter(&bigzaplock);
		mutex_enter(&chan->lock);
		if (stack.conf.confno == -1) 
			stack.conf.confno = zt_first_empty_conference();
		if ((stack.conf.confno < 1) && (stack.conf.confmode)) {
			/* No more empty conferences */
			chan_unlock(chan);
			mutex_exit(&bigzaplock);
			return EBUSY;
		}
		  /* if changing confs, clear last added info */
		if (stack.conf.confno != chans[i]->confna) {
			bzero(chans[i]->conflast,  ZT_MAX_CHUNKSIZE);
			bzero(chans[i]->conflast1, ZT_MAX_CHUNKSIZE);
			bzero(chans[i]->conflast2, ZT_MAX_CHUNKSIZE);
		}
		j = chans[i]->confna;  /* save old conference number */
		chans[i]->confna = stack.conf.confno;   /* set conference number */
		chans[i]->confmode = stack.conf.confmode;  /* set conference mode */
		chans[i]->_confn = 0;		     /* Clear confn */
		zt_check_conf(j);
		zt_check_conf(stack.conf.confno);
		/* k will be non-zero if in a real conf */
		k = stack.conf.confmode & (ZT_CONF_CONF | ZT_CONF_CONFANN | ZT_CONF_CONFMON | ZT_CONF_CONFANNMON | ZT_CONF_REALANDPSEUDO);
		/* if we are going onto a conf */
		if (stack.conf.confno && k) {
			/* Get alias */
			chans[i]->_confn = zt_get_conf_alias(stack.conf.confno);
		}
		chan_unlock(chan);
		mutex_exit(&bigzaplock);
		ddi_copyout(&stack.conf, (void *)data, sizeof(stack.conf), mode);
		break;
	case ZT_CONFLINK:  /* do conf link stuff */
		if (!(chan->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		ddi_copyin((void *)data, &stack.conf, sizeof(stack.conf), mode);
		  /* check sanity of arguments */
		if ((stack.conf.chan < 0) || (stack.conf.chan > ZT_MAX_CONF)) return(EINVAL);
		if ((stack.conf.confno < 0) || (stack.conf.confno > ZT_MAX_CONF)) return(EINVAL);
		  /* cant listen to self!! */
		if (stack.conf.chan && (stack.conf.chan == stack.conf.confno)) return(EINVAL);
		mutex_enter(&bigzaplock);
		mutex_enter(&chan->lock);
		  /* if to clear all links */
		if ((!stack.conf.chan) && (!stack.conf.confno))
		   {
			   /* clear all the links */
			bzero(conf_links,sizeof(conf_links));
			recalc_maxlinks();
			chan_unlock(chan);
			mutex_exit(&bigzaplock);
			break;
		   }
		rv = 0;  /* clear return value */
		/* look for already existant specified combination */
		for(i = 1; i <= ZT_MAX_CONF; i++)
		   {
			  /* if found, exit */
			if ((conf_links[i].src == stack.conf.chan) &&
				(conf_links[i].dst == stack.conf.confno)) break;
		   }
		if (i <= ZT_MAX_CONF) /* if found */
		   {
			if (!stack.conf.confmode) /* if to remove link */
			   {
				conf_links[i].src = conf_links[i].dst = 0;
			   }
			else /* if to add and already there, error */
			   {
				rv = EEXIST;
			   }
		   }
		else  /* if not found */
		   {
			if (stack.conf.confmode) /* if to add link */
			   {
				/* look for empty location */
				for(i = 1; i <= ZT_MAX_CONF; i++)
				   {
					  /* if empty, exit loop */
					if ((!conf_links[i].src) &&
						 (!conf_links[i].dst)) break;
				   }
				   /* if empty spot found */
				if (i <= ZT_MAX_CONF)
				   {
					conf_links[i].src = stack.conf.chan;
					conf_links[i].dst = stack.conf.confno;
				   }
				else /* if no empties -- error */
				   {
					rv = ENOSPC;
				   }
			   }
			else /* if to remove, and not found -- error */
			   {
				rv = ENOENT;
			   }
		   }
		recalc_maxlinks();
		chan_unlock(chan);
		mutex_exit(&bigzaplock);
		return(rv);
	case ZT_CONFDIAG:  /* output diagnostic info to console */
		if (!(chan->flags & ZT_FLAG_AUDIO)) return (EINVAL);
		ddi_copyin((void *)data, &j, sizeof(int), mode);
 		  /* loop thru the interesting ones */
		for(i = ((j) ? j : 1); i <= ((j) ? j : ZT_MAX_CONF); i++)
		   {
			c = 0;
			for(k = 1; k < ZT_MAX_CHANNELS; k++)
			   {
				  /* skip if no pointer */
				if (!chans[k]) continue;
				  /* skip if not in this conf */
				if (chans[k]->confna != i) continue;
				if (!c) cmn_err(CE_CONT, "Conf #%d:\n",i);
				c = 1;
				cmn_err(CE_CONT, "chan %d, mode %x\n",
					k,chans[k]->confmode);
			   }
			rv = 0;
			for(k = 1; k <= ZT_MAX_CONF; k++)
			   {
				if (conf_links[k].dst == i)
				   {
					if (!c) cmn_err(CE_CONT, "Conf #%d:\n",i);
					c = 1;
					if (!rv) cmn_err(CE_CONT, "Snooping on:\n");
					rv = 1;
					cmn_err(CE_CONT, "conf %d\n",conf_links[k].src);
				   }
			   }
			if (c) cmn_err(CE_CONT, "\n");
		   }
		break;
	case ZT_CHANNO:  /* get channel number of stream */
		ddi_copyout(&unit, (void *)data, sizeof(int), mode);
		break;
	case ZT_SETLAW:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if ((j < 0) || (j > ZT_LAW_ALAW))
			return EINVAL;
		zt_set_law(chan, j);
		break;
	case ZT_SETLINEAR:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		/* Makes no sense on non-audio channels */
		if (!(chan->flags & ZT_FLAG_AUDIO))
			return EINVAL;

		if (j)
			chan->flags |= ZT_FLAG_LINEAR;
		else
			chan->flags &= ~ZT_FLAG_LINEAR;
		break;
	case ZT_SETCADENCE:
		if (data) {
			/* Use specific ring cadence */
			if (ddi_copyin((void *)data, &stack.cad, sizeof(stack.cad), mode))
				return EIO;
			bcopy(&stack.cad, chan->ringcadence, sizeof(chan->ringcadence));
			chan->firstcadencepos = 0;
			/* Looking for negative ringing time indicating where to loop back into ringcadence */
			for (i=0; i<ZT_MAX_CADENCE; i+=2 ) {
				if (chan->ringcadence[i]<0) {
					chan->ringcadence[i] *= -1;
					chan->firstcadencepos = i;
					break;
				}
			}
		} else {
			/* Reset to default */
			chan->firstcadencepos = 0;
			if (chan->curzone) {
				bcopy(chan->curzone->ringcadence, chan->ringcadence, sizeof(chan->ringcadence));
				/* Looking for negative ringing time indicating where to loop back into ringcadence */
				for (i=0; i<ZT_MAX_CADENCE; i+=2 ) {
					if (chan->ringcadence[i]<0) {
						chan->ringcadence[i] *= -1;
						chan->firstcadencepos = i;
						break;
					}
				}
			} else {
				bzero(chan->ringcadence, sizeof(chan->ringcadence));
				chan->ringcadence[0] = chan->starttime;
				chan->ringcadence[1] = ZT_RINGOFFTIME;
			}
		}
		break;
	default:
		/* Check for common ioctl's and private ones */
		rv = zt_common_ioctl(dev, cmd, data, mode, credp, rvalp);
		/* if no span, just return with value */
		if (!chan->span) return rv;
		if ((rv == ENOTTY) && chan->span->ioctl) 
			rv = chan->span->ioctl(chan, cmd, data, 0);
		return rv;
		
	}
	return 0;
}

static int zt_chan_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	struct zt_chan *chan;
	unsigned long flags;
	int j, rv;
	int ret;
	int oldconf;
	void *rxgain=NULL;
	echo_can_state_t *ec, *tec;
	int unit = getminor(dev) - ZT_DEV_CHAN_BASE;

	if (unit < 0 || unit > ZT_DEV_CHAN_COUNT)
		return ENOSYS;

	if (chan_map[unit] < 0)
		return ENOSYS;

 	chan = chans[chan_map[unit]];

	if (!chan)
		return ENOSYS;

	switch(cmd) {
	case ZT_SIGFREEZE:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		mutex_enter(&chan->lock);
		if (j) {
			chan->flags |= ZT_FLAG_SIGFREEZE;
		} else {
			chan->flags &= ~ZT_FLAG_SIGFREEZE;
		}
		chan_unlock(chan);
		break;
	case ZT_GETSIGFREEZE:
		mutex_enter(&chan->lock);
		if (chan->flags & ZT_FLAG_SIGFREEZE)
			j = 1;
		else
			j = 0;
		chan_unlock(chan);
		ddi_copyout(&j, (void *)data, sizeof(int), mode);
		break;
	case ZT_AUDIOMODE:
		/* Only literal clear channels can be put in  */
		if (chan->sig != ZT_SIG_CLEAR) return (EINVAL);
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if (j) {
			mutex_enter(&chan->lock);
			chan->flags |= ZT_FLAG_AUDIO;
			chan->flags &= ~(ZT_FLAG_HDLC | ZT_FLAG_FCS);
			chan_unlock(chan);
		} else {
			/* Coming out of audio mode, also clear all 
			   conferencing and gain related info as well
			   as echo canceller */
			mutex_enter(&chan->lock);
			chan->flags &= ~ZT_FLAG_AUDIO;
			/* save old conf number, if any */
			oldconf = chan->confna;
			  /* initialize conference variables */
			chan->_confn = 0;
			chan->confna = 0;
			chan->confmode = 0;
			chan->confmute = 0;
			bzero(chan->conflast, sizeof(chan->conflast));
			bzero(chan->conflast1, sizeof(chan->conflast1));
			bzero(chan->conflast2, sizeof(chan->conflast2));
			ec = chan->ec;
			chan->ec = NULL;
			/* release conference resource, if any to release */
			reset_conf(chan);
			if (chan->gainalloc && chan->rxgain)
				rxgain = chan->rxgain;
			else
				rxgain = NULL;

			chan->rxgain = defgain;
			chan->txgain = defgain;
			chan->gainalloc = 0;
			chan_unlock(chan);

			if (rxgain)
				kmem_free(rxgain,512);
			if (ec)
				echo_can_free(ec);
			if (oldconf) zt_check_conf(oldconf);
		}
		break;
	case ZT_HDLCPPP:
		cmn_err(CE_CONT, "Zaptel: Zaptel PPP support not compiled in\n");
		return ENOSYS;
		break;
	case ZT_HDLCRAWMODE:
		if (chan->sig != ZT_SIG_CLEAR)	return (EINVAL);
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		chan->flags &= ~(ZT_FLAG_AUDIO | ZT_FLAG_HDLC | ZT_FLAG_FCS);
		if (j) {
			chan->flags |= ZT_FLAG_HDLC;
			fasthdlc_init(&chan->rxhdlc);
			fasthdlc_init(&chan->txhdlc);
		}
		break;
	case ZT_HDLCFCSMODE:
		if (chan->sig != ZT_SIG_CLEAR)	return (EINVAL);
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		chan->flags &= ~(ZT_FLAG_AUDIO | ZT_FLAG_HDLC | ZT_FLAG_FCS);
		if (j) {
			chan->flags |= ZT_FLAG_HDLC | ZT_FLAG_FCS;
			fasthdlc_init(&chan->rxhdlc);
			fasthdlc_init(&chan->txhdlc);
		}
		break;
	case ZT_ECHOCANCEL:
		if (!(chan->flags & ZT_FLAG_AUDIO))
			return EINVAL;
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if (j) {
			if ((j == 32) ||
			    (j == 64) ||
			    (j == 128) ||
			    (j == 256)) {
				/* Okay */
			} else {
				j = deftaps;
			}
			ec = echo_can_create(j, 0);
			if (!ec)
				return ENOMEM;
			mutex_enter(&chan->lock);
			/* If we had an old echo can, zap it now */
			tec = chan->ec;
			chan->echocancel = j;
			chan->ec = ec;
			chan->echostate = ECHO_STATE_IDLE;
			chan->echolastupdate = 0;
			chan->echotimer = 0;
			echo_can_disable_detector_init(&chan->txecdis);
			echo_can_disable_detector_init(&chan->rxecdis);
			chan_unlock(chan);
			if (tec)
				echo_can_free(tec);
		} else {
			mutex_enter(&chan->lock);
			tec = chan->ec;
			chan->echocancel = 0;
			chan->ec = NULL;
			chan->echostate = ECHO_STATE_IDLE;
			chan->echolastupdate = 0;
			chan->echotimer = 0;
			chan_unlock(chan);
			if (tec)
				echo_can_free(tec);
		}
		break;
	case ZT_ECHOTRAIN:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if ((j < 0) || (j >= ZT_MAX_PRETRAINING))
			return EINVAL;
		j <<= 3;
		if (chan->ec) {
			/* Start pretraining stage */
			chan->echostate = ECHO_STATE_PRETRAINING;
			chan->echotimer = j;
		} else
			return EINVAL;
		break;
	case ZT_SETTXBITS:
		if (chan->sig != ZT_SIG_CAS)
			return EINVAL;
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		zt_cas_setbits(chan, j);
		rv = 0;
		break;
	case ZT_GETRXBITS:
		ddi_copyin(&chan->rxsig, (void *)data, sizeof(int), mode);
		rv = 0;
		break;
	case ZT_HOOK:
		ddi_copyin((void *)data, &j, sizeof(int), mode);
		if (chan->flags & ZT_FLAG_CLEAR)
			return EINVAL;
		if (chan->sig == ZT_SIG_CAS) 
			return EINVAL;
		/* if no span, just do nothing */
		if (!chan->span) return(0);
		mutex_enter(&chan->lock);
		/* if dialing, stop it */
		chan->curtone = NULL;
		chan->dialing = 0;
		chan->txdialbuf[0] = '\0';
		chan->tonep = 0;
		chan->pdialcount = 0;
		chan_unlock(chan);
		if (chan->span->flags & ZT_FLAG_RBS) {
			switch (j) {
			case ZT_ONHOOK:
				mutex_enter(&chan->lock);
				zt_hangup(chan);
				chan_unlock(chan);
				break;
			case ZT_OFFHOOK:
				mutex_enter(&chan->lock);
				if ((chan->txstate == ZT_TXSTATE_KEWL) ||
				  (chan->txstate == ZT_TXSTATE_AFTERKEWL)) {
					chan_unlock(chan);
					return EBUSY;
				}
				zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_DEBOUNCE, chan->debouncetime);
				chan_unlock(chan);
				break;
			case ZT_RING:
			case ZT_START:
				mutex_enter(&chan->lock);
				if (chan->txstate != ZT_TXSTATE_ONHOOK) {
					chan_unlock(chan);
					return EBUSY;
				}
				if (chan->sig & __ZT_SIG_FXO) {
					ret = 0;
					chan->cadencepos = 0;
					ret = chan->ringcadence[0];
					zt_rbs_sethook(chan, ZT_TXSIG_START, ZT_TXSTATE_RINGON, ret);
				} else {
					zt_rbs_sethook(chan, ZT_TXSIG_START, ZT_TXSTATE_START, chan->starttime);
				}
				chan_unlock(chan);
				/*
				if (mode & O_NONBLOCK) {
					cmn_err(CE_CONT, "Returning because of non-block.\n");
					cv_broadcast(&chan->txstateq);
					return -EINPROGRESS;
				}
				*/
				rv = 0;
				cv_broadcast(&chan->txstateq);
				break;
			case ZT_WINK:
				mutex_enter(&chan->lock);
				if (chan->txstate != ZT_TXSTATE_ONHOOK) {
					chan_unlock(chan);
					return EBUSY;
				}
				zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, ZT_TXSTATE_PREWINK, chan->prewinktime);
				chan_unlock(chan);
				if (mode & O_NONBLOCK)
					return EINPROGRESS;
				//rv = schluffen(&chan->txstateq);
				//if (rv) return rv;
				cv_broadcast(&chan->txstateq);
				break;
			case ZT_FLASH:
				mutex_enter(&chan->lock);
				if (chan->txstate != ZT_TXSTATE_OFFHOOK) {
					chan_unlock(chan);
					return EBUSY;
				}
				zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_PREFLASH, chan->preflashtime);
				chan_unlock(chan);
				if (mode & O_NONBLOCK)
					return EINPROGRESS;
				// rv = schluffen(&chan->txstateq);
				// if (rv) return rv;
				cv_broadcast(&chan->txstateq);
				break;
			case ZT_RINGOFF:
				mutex_enter(&chan->lock);
				zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, ZT_TXSTATE_ONHOOK, 0);
				chan_unlock(chan);
				break;
			default:
				return EINVAL;
			}
		} else if (chan->span->sethook) {
			if (chan->txhooksig != j) {
				chan->txhooksig = j;
				chan->span->sethook(chan, j);
			}
		} else
			return ENOSYS;
		break;
	default:
		return zt_chanandpseudo_ioctl(makedevice(getmajor(dev),chan_map[unit]), cmd, data, mode, credp, rvalp);
	}
	return 0;
}

static int zt_prechan_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	struct zt_chan *chan;
	int channo;
	int res;
	dev_t newdev;

	if (chan_map[getminor(dev) - ZT_DEV_CHAN_BASE] >= 0) {
		cmn_err(CE_CONT, "Huh?  Prechan is already specified??\n");
	}
	switch(cmd) {
	case ZT_SPECIFY:
		ddi_copyin((void *)data, &channo, sizeof(int), mode);
		// cmn_err(CE_CONT, "ZT_SPECIFY channo = %d\n", channo);
		if (channo < 1)
			return EINVAL;
		if (channo > ZT_MAX_CHANNELS)
			return EINVAL;
		newdev = makedevice(getmajor(dev), channo);
		res = zt_specchan_open(&newdev, 0, 0, NULL);
		if (!res) {
			chan_map[getminor(dev) - ZT_DEV_CHAN_BASE] = channo;
			/* Return success */
			return 0;
		}
		return res;
	default:
		return ENOSYS;
	}
	return 0;
}

static int zt_ioctl(dev_t dev, int cmd, intptr_t data, int mode, cred_t *credp, int *rvalp)
{
	int unit = getminor(dev);
	struct zt_chan *chan;
	struct zt_timer *timer;

	if (unit == 0)
		return zt_ctl_ioctl(dev, cmd, data, mode, credp, rvalp);
	
	/*
	if (cmd == ZT_DIAL) {
		cmn_err(CE_CONT, "got ZT_DIAL on unit %d\n", unit);
	} else if (cmd == ZT_HOOK) {
		cmn_err(CE_CONT, "got ZT_HOOK on unit %d\n", unit);
	} else if (cmd == ZT_GETEVENT) {
		cmn_err(CE_CONT, "got ZT_GETEVENT on unit %d\n", unit);
	} else if (cmd == ZT_IOMUX) {
		cmn_err(CE_CONT, "got ZT_IOMUX on unit %d\n", unit);
	} else if (cmd == ZT_FLUSH) {
		cmn_err(CE_CONT, "got ZT_FLUSH on unit %d\n", unit);
	} else {
		cmn_err(CE_CONT, "zt_ioctl unit=%d cmd=%d\n", unit, cmd);
	}
	*/
	
	if (unit == 253) {
		/* Shouldn't happen - unit will have been replaced in open */
		return EINVAL;
	}
	if (unit >= ZT_DEV_TIMER_BASE && unit <= (ZT_DEV_TIMER_BASE + ZT_DEV_TIMER_COUNT)) {
		return zt_timer_ioctl(dev, cmd, data, mode, credp, rvalp);
	}
	if (unit == 254) {
		/* Shouldn't happen - unit will have been replaced in open */
		return EINVAL;
	}
	if (unit >= ZT_DEV_CHAN_BASE && unit <= (ZT_DEV_CHAN_BASE + ZT_DEV_CHAN_COUNT)) {
		if (chan_map[unit - ZT_DEV_CHAN_BASE] > 0)
			return zt_chan_ioctl(dev, cmd, data, mode, credp, rvalp);
		else
			return zt_prechan_ioctl(dev, cmd, data, mode, credp, rvalp);
	}
	if (unit == 255) {
		/* Shouldn't happen - unit will have been replaced in open */
		return EINVAL;
	}

	if (chans[unit]->flags & ZT_FLAG_PSEUDO) 
		return zt_chanandpseudo_ioctl(dev, cmd, data, mode, credp, rvalp);
	else
		return zt_chan_ioctl(dev, cmd, data, mode, credp, rvalp);
}

int zt_register(struct zt_span *span, int prefmaster)
{
	int x;

	if (!span)
		return EINVAL;
	if (span->flags & ZT_FLAG_REGISTERED) {
		cmn_err(CE_CONT, "Span %s already appears to be registered\n", span->name);
		return EBUSY;
	}
	for (x=1;x<maxspans;x++)
		if (spans[x] == span) {
			cmn_err(CE_CONT, "Span %s already in list\n", span->name);
			return EBUSY;
		}
	for (x=1;x<ZT_MAX_SPANS;x++)
		if (!spans[x])
			break;
	if (x < ZT_MAX_SPANS) {
		spans[x] = span;
		if (maxspans < x + 1)
			maxspans = x + 1;
	} else {
		cmn_err(CE_CONT, "Too many zapata spans registered\n");
		return EBUSY;
	}
	span->flags |= ZT_FLAG_REGISTERED;
	span->spanno = x;
	mutex_init(&span->lock, NULL, MUTEX_DRIVER, NULL);
	if (!span->deflaw) {
		cmn_err(CE_CONT, "zaptel: Span %s didn't specify default law.  Assuming mulaw, please fix driver!\n", span->name);
		span->deflaw = ZT_LAW_MULAW;
	}

	for (x=0;x<span->channels;x++) {
		span->chans[x].span = span;
		zt_chan_reg(&span->chans[x]); 
	}

	cmn_err(CE_CONT, "Registered Span %d ('%s') with %d channels\n", span->spanno, span->name, span->channels);
	if (!master || prefmaster) {
		master = span;
		cmn_err(CE_CONT, "Span ('%s') is new master\n", span->name);
	}
	return 0;
}

int zt_unregister(struct zt_span *span)
{
	int x;

	if (!(span->flags & ZT_FLAG_REGISTERED)) {
		cmn_err(CE_CONT, "Span %s does not appear to be registered\n", span->name);
		return -1;
	}
	/* Shutdown the span if it's running */
	if (span->flags & ZT_FLAG_RUNNING)
		if (span->shutdown)
			span->shutdown(span);
			
	if (spans[span->spanno] != span) {
		cmn_err(CE_CONT, "Span %s has spanno %d which is something else\n", span->name, span->spanno);
		return -1;
	}
	cmn_err(CE_CONT, "Unregistering Span '%s' with %d channels\n", span->name, span->channels);

	spans[span->spanno] = NULL;
	span->spanno = 0;
	span->flags &= ~ZT_FLAG_REGISTERED;
	for (x=0;x<span->channels;x++)
		zt_chan_unreg(&span->chans[x]);
	maxspans = 0;
	if (master == span)
		master = NULL;
	for (x=1;x<ZT_MAX_SPANS;x++) {
		if (spans[x]) {
			maxspans = x+1;
			if (!master)
				master = spans[x];
		}
	}

	return 0;
}

/*
** This routine converts from linear to ulaw
**
** Craig Reese: IDA/Supercomputing Research Center
** Joe Campbell: Department of Defense
** 29 September 1989
**
** References:
** 1) CCITT Recommendation G.711  (very difficult to follow)
** 2) "A New Digital Technique for Implementation of Any
**     Continuous PCM Companding Law," Villeret, Michel,
**     et al. 1973 IEEE Int. Conf. on Communications, Vol 1,
**     1973, pg. 11.12-11.17
** 3) MIL-STD-188-113,"Interoperability and Performance Standards
**     for Analog-to_Digital Conversion Techniques,"
**     17 February 1987
**
** Input: Signed 16 bit linear sample
** Output: 8 bit ulaw sample
*/

#define ZEROTRAP    /* turn on the trap as per the MIL-STD */
#define BIAS 0x84   /* define the add-in bias for 16 bit samples */
#define CLIP 32635

#ifdef CONFIG_CALC_XLAW
unsigned char
#else
static unsigned char 
#endif
__zt_lineartoulaw(short sample)
{
  static int exp_lut[256] = {0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
                             4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};
  int sign, exponent, mantissa;
  unsigned char ulawbyte;

  /* Get the sample into sign-magnitude. */
  sign = (sample >> 8) & 0x80;          /* set aside the sign */
  if (sign != 0) sample = -sample;              /* get magnitude */
  if (sample > CLIP) sample = CLIP;             /* clip the magnitude */

  /* Convert from 16 bit linear to ulaw. */
  sample = sample + BIAS;
  exponent = exp_lut[(sample >> 7) & 0xFF];
  mantissa = (sample >> (exponent + 3)) & 0x0F;
  ulawbyte = ~(sign | (exponent << 4) | mantissa);
#ifdef ZEROTRAP
  if (ulawbyte == 0) ulawbyte = 0x02;   /* optional CCITT trap */
#endif
  if (ulawbyte == 0xff) ulawbyte = 0x7f;   /* never return 0xff */
  return(ulawbyte);
}

#define AMI_MASK 0x55

#ifdef CONFIG_CALC_XLAW
unsigned char
#else
static inline unsigned char 
#endif
__zt_lineartoalaw (short linear)
{
    int mask;
    int seg;
    int pcm_val;
    static int seg_end[8] =
    {
         0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF
    };
    
    pcm_val = linear;
    if (pcm_val >= 0)
    {
        /* Sign (7th) bit = 1 */
        mask = AMI_MASK | 0x80;
    }
    else
    {
        /* Sign bit = 0 */
        mask = AMI_MASK;
        pcm_val = -pcm_val;
    }

    /* Convert the scaled magnitude to segment number. */
    for (seg = 0;  seg < 8;  seg++)
    {
        if (pcm_val <= seg_end[seg])
	    break;
    }
    /* Combine the sign, segment, and quantization bits. */
    return  ((seg << 4) | ((pcm_val >> ((seg)  ?  (seg + 3)  :  4)) & 0x0F)) ^ mask;
}
/*- End of function --------------------------------------------------------*/

static inline short int alaw2linear (uint8_t alaw)
{
    int i;
    int seg;

    alaw ^= AMI_MASK;
    i = ((alaw & 0x0F) << 4);
    seg = (((int) alaw & 0x70) >> 4);
    if (seg)
        i = (i + 0x100) << (seg - 1);
    return (short int) ((alaw & 0x80)  ?  i  :  -i);
}
/*- End of function --------------------------------------------------------*/
static void  zt_conv_init(void)
{
	int i;

	/* 
	 *  Set up mu-law conversion table
	 */
	for(i = 0;i < 256;i++)
	   {
		short mu,e,f,y;
		static short etab[]={0,132,396,924,1980,4092,8316,16764};

		mu = 255-i;
		e = (mu & 0x70)/16;
		f = mu & 0x0f;
		y = f * (1 << (e + 3));
		y += etab[e];
		if (mu & 0x80) y = -y;
	        __zt_mulaw[i] = y;
		__zt_alaw[i] = alaw2linear(i);
		/* Default (0.0 db) gain table */
		defgain[i] = i;
	   }
#ifndef CONFIG_CALC_XLAW
	  /* set up the reverse (mu-law) conversion table */
	for(i = -32768; i < 32768; i += 4)
	   {
		__zt_lin2mu[((unsigned short)(short)i) >> 2] = __zt_lineartoulaw(i);
		__zt_lin2a[((unsigned short)(short)i) >> 2] = __zt_lineartoalaw(i);
	   }
#endif
}

static inline void __zt_process_getaudio_chunk(struct zt_chan *ss, unsigned char *txb)
{
	/* We transmit data from our master channel */
	/* Called with ss->lock held */
	struct zt_chan *ms = ss->master;
	/* Linear representation */
	short getlin[ZT_CHUNKSIZE], k[ZT_CHUNKSIZE];
	int x;

	/* Okay, now we've got something to transmit */
	for (x=0;x<ZT_CHUNKSIZE;x++)
		getlin[x] = ZT_XLAW(txb[x], ms);
#ifndef NO_ECHOCAN_DISABLE
	if (ms->ec) {
		for (x=0;x<ZT_CHUNKSIZE;x++) {
			/* Check for echo cancel disabling tone */
			if (echo_can_disable_detector_update(&ms->txecdis, getlin[x])) {
				cmn_err(CE_CONT, "zaptel Disabled echo canceller because of tone (tx) on channel %d\n", ss->channo);
				ms->echocancel = 0;
				ms->echostate = ECHO_STATE_IDLE;
				ms->echolastupdate = 0;
				ms->echotimer = 0;
				kmem_free(ms->ec, ms->ec->allocsize);
				ms->ec = NULL;
				break;
			}
		}
	}
#endif
	if ((!ms->confmute && !ms->dialing) || (ms->flags & ZT_FLAG_PSEUDO)) {
		/* Handle conferencing on non-clear channel and non-HDLC channels */
		switch(ms->confmode & ZT_CONF_MODE_MASK) {
		case ZT_CONF_NORMAL:
			/* Do nuffin */
			break;
		case ZT_CONF_MONITOR:	/* Monitor a channel's rx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & ZT_FLAG_PSEUDO) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & ZT_FLAG_PSEUDO) {
				ACSS(getlin, chans[ms->confna]->getlin);
			} else {
				ACSS(getlin, chans[ms->confna]->putlin);
			}
			for (x=0;x<ZT_CHUNKSIZE;x++)
				txb[x] = ZT_LIN2X(getlin[x], ms);
			break;
		case ZT_CONF_MONITORTX: /* Monitor a channel's tx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & ZT_FLAG_PSEUDO) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & ZT_FLAG_PSEUDO) {
				ACSS(getlin, chans[ms->confna]->putlin);
			} else {
				ACSS(getlin, chans[ms->confna]->getlin);
			}

			for (x=0;x<ZT_CHUNKSIZE;x++)
				txb[x] = ZT_LIN2X(getlin[x], ms);
			break;
		case ZT_CONF_MONITORBOTH: /* monitor a channel's rx and tx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & ZT_FLAG_PSEUDO) break;
			ACSS(getlin, chans[ms->confna]->putlin);
			ACSS(getlin, chans[ms->confna]->getlin);
			for (x=0;x<ZT_CHUNKSIZE;x++)
				txb[x] = ZT_LIN2X(getlin[x], ms);
			break;
		case ZT_CONF_REALANDPSEUDO:
			/* This strange mode takes the transmit buffer and
				puts it on the conference, minus its last sample,
				then outputs from the conference minus the 
				real channel's last sample. */
			  /* if to talk on conf */
			if (ms->confmode & ZT_CONF_PSEUDO_TALKER) {
				/* Store temp value */
				bcopy(getlin, k, ZT_CHUNKSIZE * sizeof(short));
				/* Add conf value */
				ACSS(k, conf_sums_next[ms->_confn]);
				/* save last one */
				bcopy(ms->conflast1, ms->conflast2, ZT_CHUNKSIZE * sizeof(short));
				bcopy(k, ms->conflast1, ZT_CHUNKSIZE * sizeof(short));
				/*  get amount actually added */
				SCSS(ms->conflast1, conf_sums_next[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums_next[ms->_confn], ms->conflast1);
			} else {
				bzero(ms->conflast1, ZT_CHUNKSIZE * sizeof(short));
				bzero(ms->conflast2, ZT_CHUNKSIZE * sizeof(short));
			}
			bzero(getlin, ZT_CHUNKSIZE * sizeof(short));
			txb[0] = ZT_LIN2X(0, ms);
			for (x=0; x< ZT_CHUNKSIZE - 1; x++)
				txb[x+1] = txb[0];
			/* fall through to normal conf mode */
		case ZT_CONF_CONF:	/* Normal conference mode */
			if (ms->flags & ZT_FLAG_PSEUDO) /* if pseudo-channel */
			   {
				  /* if to talk on conf */
				if (ms->confmode & ZT_CONF_TALKER) {
					/* Store temp value */
					bcopy(getlin, k, ZT_CHUNKSIZE * sizeof(short));
					/* Add conf value */
					ACSS(k, conf_sums[ms->_confn]);
					/*  get amount actually added */
					bcopy(k, ms->conflast, ZT_CHUNKSIZE * sizeof(short));
					SCSS(ms->conflast, conf_sums[ms->_confn]);
					/* Really add in new value */
					ACSS(conf_sums[ms->_confn], ms->conflast);
				} else bzero(ms->conflast, ZT_CHUNKSIZE * sizeof(short));
				bcopy(ms->getlin, getlin, ZT_CHUNKSIZE * sizeof(short));
				txb[0] = ZT_LIN2X(0, ms);
				for (x=0; x< ZT_CHUNKSIZE - 1; x++)
					txb[x+1] = txb[0];
				break;
		 	   }
			/* fall through */
		case ZT_CONF_CONFMON:	/* Conference monitor mode */
			if (ms->confmode & ZT_CONF_LISTENER) {
				/* Subtract out last sample written to conf */
				SCSS(getlin, ms->conflast);
				/* Add in conference */
				ACSS(getlin, conf_sums[ms->_confn]);
			}
			for (x=0;x<ZT_CHUNKSIZE;x++)
				txb[x] = ZT_LIN2X(getlin[x], ms);
			break;
		case ZT_CONF_CONFANN:
		case ZT_CONF_CONFANNMON:
			/* First, add tx buffer to conf */
			ACSS(conf_sums_next[ms->_confn], getlin);
			/* Start with silence */
			bzero(getlin, ZT_CHUNKSIZE * sizeof(short));
			/* If a listener on the conf... */
			if (ms->confmode & ZT_CONF_LISTENER) {
				/* Subtract last value written */
				SCSS(getlin, ms->conflast);
				/* Add in conf */
				ACSS(getlin, conf_sums[ms->_confn]);
			}
			for (x=0;x<ZT_CHUNKSIZE;x++)
				txb[x] = ZT_LIN2X(getlin[x], ms);
			break;
		case ZT_CONF_DIGITALMON:
			/* Real digital monitoring, but still echo cancel if desired */
			if (!chans[ms->confna])
				break;
			if (chans[ms->confna]->flags & ZT_FLAG_PSEUDO) {
				if (ms->ec) {
					for (x=0;x<ZT_CHUNKSIZE;x++)
						txb[x] = ZT_LIN2X(chans[ms->confna]->getlin[x], ms);
				} else {
					bcopy(chans[ms->confna]->getraw, txb, ZT_CHUNKSIZE);
				}
			} else {
				if (ms->ec) {
					for (x=0;x<ZT_CHUNKSIZE;x++)
						txb[x] = ZT_LIN2X(chans[ms->confna]->putlin[x], ms);
				} else {
					bcopy(chans[ms->confna]->putraw, txb, ZT_CHUNKSIZE);
				}
			}
			for (x=0;x<ZT_CHUNKSIZE;x++)
				getlin[x] = ZT_XLAW(txb[x], ms);
			break;
		}
	}
	if (ms->confmute || (ms->echostate & __ECHO_STATE_MUTE)) {
		txb[0] = ZT_LIN2X(0, ms);
		for (x=1;x<ZT_CHUNKSIZE;x++)
			txb[x] = txb[0];
		if (ms->echostate == ECHO_STATE_STARTTRAINING) {
			/* Transmit impulse now */
			txb[0] = ZT_LIN2X(16384, ms);
			ms->echostate = ECHO_STATE_AWAITINGECHO;
		}
	}
	/* save value from last chunk */
	bcopy(ms->getlin, ms->getlin_lastchunk, ZT_CHUNKSIZE * sizeof(short));
	/* save value from current */
	bcopy(getlin, ms->getlin, ZT_CHUNKSIZE * sizeof(short));
	/* save value from current */
	bcopy(txb, ms->getraw, ZT_CHUNKSIZE);
	/* if to make tx tone */
	if (ms->v1_1 || ms->v2_1 || ms->v3_1)
	{
		for (x=0;x<ZT_CHUNKSIZE;x++)
		{
			getlin[x] += zt_txtone_nextsample(ms);
			txb[x] = ZT_LIN2X(getlin[x], ms);
		}
	}
	/* This is what to send (after having applied gain) */
	for (x=0;x<ZT_CHUNKSIZE;x++)
		txb[x] = ms->txgain[txb[x]];
}

static inline void __zt_getbuf_chunk(struct zt_chan *ss, unsigned char *txb)
{
	/* Called with ss->lock held */
	/* We transmit data from our master channel */
	struct zt_chan *ms = ss->master;
	/* Buffer we're using */
	unsigned char *buf;
	/* Old buffer number */
	int oldbuf;
	/* Linear representation */
	int getlin;
	/* How many bytes we need to process */
	int bytes = ZT_CHUNKSIZE, left;
	int x;

	/* Let's pick something to transmit.  First source to
	   try is our write-out buffer.  Always check it first because
	   its our 'fast path' for whatever that's worth. */
	while(bytes) {
		if ((ms->outwritebuf > -1) && !ms->txdisable) {
			buf= ms->writebuf[ms->outwritebuf];
			left = ms->writen[ms->outwritebuf] - ms->writeidx[ms->outwritebuf];
			if (left > bytes)
				left = bytes;
			if (ms->flags & ZT_FLAG_HDLC) {
				/* If this is an HDLC channel we only send a byte of
				   HDLC. */
				for(x=0;x<left;x++) {
					if (ms->txhdlc.bits < 8)
						/* Load a byte of data only if needed */
						fasthdlc_tx_load_nocheck(&ms->txhdlc, buf[ms->writeidx[ms->outwritebuf]++]);
					*(txb++) = fasthdlc_tx_run_nocheck(&ms->txhdlc);
				}
				bytes -= left;
			} else {
				bcopy(buf + ms->writeidx[ms->outwritebuf], txb, left);
				ms->writeidx[ms->outwritebuf]+=left;
				txb += left;
				bytes -= left;
			}
			/* Check buffer status */
			if (ms->writeidx[ms->outwritebuf] >= ms->writen[ms->outwritebuf]) {
				/* We've reached the end of our buffer.  Go to the next. */
				oldbuf = ms->outwritebuf;
				/* Clear out write index and such */
				ms->writeidx[oldbuf] = 0;
				ms->writen[oldbuf] = 0;
				ms->outwritebuf = (ms->outwritebuf + 1) % ms->numbufs;
				if (ms->outwritebuf == ms->inwritebuf) {
					/* Whoopsies, we're run out of buffers.  Mark ours
					as -1 and wait for the filler to notify us that
					there is something to write */
					ms->outwritebuf = -1;
					if (ms->iomask & (ZT_IOMUX_WRITE | ZT_IOMUX_WRITEEMPTY))
						cv_broadcast(&ms->eventbufq);
					/* If we're only supposed to start when full, disable the transmitter */
					if (ms->txbufpolicy == ZT_POLICY_WHEN_FULL)
						ms->txdisable = 1;
				}
				if (ms->inwritebuf < 0) {
					/* The filler doesn't have a place to put data.  Now
					that we're done with this buffer, notify them. */
					ms->inwritebuf = oldbuf;
				}
/* In the very orignal driver, it was quite well known to me (Jim) that there
was a possibility that a channel sleeping on a write block needed to
be potentially woken up EVERY time a buffer was emptied, not just on the first
one, because if only done on the first one there is a slight timing potential
of missing the wakeup (between where it senses the (lack of) active condition
(with interrupts disabled) and where it does the sleep (interrupts enabled)
in the read or iomux call, etc). That is why the write and iomux calls start
with an infinite loop that gets broken out of upon an active condition,
otherwise keeps sleeping and looking. The part in this code got "optimized"
out in the later versions, and is put back now. */
				if (!(ms->flags & (ZT_FLAG_NETDEV | ZT_FLAG_PPP))) {
					cv_broadcast(&ms->writebufq);
					pollwakeup(&ms->sel, POLLOUT);
					if (ms->iomask & ZT_IOMUX_WRITE)
						cv_broadcast(&ms->eventbufq);
				}
				/* Transmit a flag if this is an HDLC channel */
				if (ms->flags & ZT_FLAG_HDLC)
					fasthdlc_tx_frame_nocheck(&ms->txhdlc);
			}
		} else if (ms->curtone && !(ms->flags & ZT_FLAG_PSEUDO)) {
			left = ms->curtone->tonesamples - ms->tonep;
			if (left > bytes)
				left = bytes;
			for (x=0;x<left;x++) {
				/* Pick our default value from the next sample of the current tone */
				getlin = zt_tone_nextsample(&ms->ts, ms->curtone);
				*(txb++) = ZT_LIN2X(getlin, ms);
			}
			ms->tonep+=left;
			bytes -= left;
			if (ms->tonep >= ms->curtone->tonesamples) {
				struct zt_tone *last;
				/* Go to the next sample of the tone */
				ms->tonep = 0;
				last = ms->curtone;
				ms->curtone = ms->curtone->next;
				if (!ms->curtone) {
					/* No more tones...  Is this dtmf or mf?  If so, go to the next digit */
					if (ms->dialing)
						__do_dtmf(ms);
				} else {
					if (last != ms->curtone)
						zt_init_tone_state(&ms->ts, ms->curtone);
				}
			}
		} else if (ms->flags & ZT_FLAG_HDLC) {
			for (x=0;x<bytes;x++) {
				/* Okay, if we're HDLC, then transmit a flag by default */
				if (ms->txhdlc.bits < 8) 
					fasthdlc_tx_frame_nocheck(&ms->txhdlc);
				*(txb++) = fasthdlc_tx_run_nocheck(&ms->txhdlc);
			}
			bytes = 0;
		} else if (ms->flags & ZT_FLAG_CLEAR) {
			/* Clear channels should idle with 0xff for the sake
			of silly PRI's that care about idle B channels */
			if (ms->flags & ZT_FLAG_AUDIO) {
				for (x=0; x<bytes; x++)
					txb[x] = ZT_LIN2X(0, ms);
				bytes = 0;
			} else {
				for (x=0; x<bytes; x++)
					txb[x] = 0xff;
				bytes = 0;
			}
		} else {
			for (x=0; x<bytes; x++)
				txb[x] = ZT_LIN2X(0, ms);
			bytes = 0;
		}
	}	
}

static inline void rbs_itimer_expire(struct zt_chan *chan)
{
	/* the only way this could have gotten here, is if a channel
	    went off hook longer then the wink or flash detect timeout */
	/* Called with chan->lock held */
	switch(chan->sig)
	   {
	    case ZT_SIG_FXOLS:  /* if FXO, its definitely on hook */
	    case ZT_SIG_FXOGS:
	    case ZT_SIG_FXOKS:
		zt_qevent_nolock(chan,ZT_EVENT_ONHOOK);
		chan->gotgs = 0; 
		break;
	    default:  /* otherwise, its definitely off hook */
		zt_qevent_nolock(chan,ZT_EVENT_RINGOFFHOOK); 
		break;
	   }
	
}

static inline void __rbs_otimer_expire(struct zt_chan *chan)
{
	int len = 0;
	/* Called with chan->lock held */

	chan->otimer = 0;
	/* Move to the next timer state */	
	switch(chan->txstate) {
	case ZT_TXSTATE_RINGOFF:
		/* Turn on the ringer now that the silent time has passed */
		++chan->cadencepos;
		if (chan->cadencepos >= ZT_MAX_CADENCE)
			chan->cadencepos = chan->firstcadencepos;
		len = chan->ringcadence[chan->cadencepos];

		if (!len) {
			chan->cadencepos = chan->firstcadencepos;
			len = chan->ringcadence[chan->cadencepos];
		}

		zt_rbs_sethook(chan, ZT_TXSIG_START, ZT_TXSTATE_RINGON, len);
		zt_qevent_nolock(chan, ZT_EVENT_RINGERON);
		break;
		
	case ZT_TXSTATE_RINGON:
		/* Turn off the ringer now that the loud time has passed */
		++chan->cadencepos;
		if (chan->cadencepos >= ZT_MAX_CADENCE)
			chan->cadencepos = 0;
		len = chan->ringcadence[chan->cadencepos];

		if (!len) {
			chan->cadencepos = 0;
			len = chan->curzone->ringcadence[chan->cadencepos];
		}

		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_RINGOFF, len);
		zt_qevent_nolock(chan, ZT_EVENT_RINGEROFF);
		break;
		
	case ZT_TXSTATE_START:
		/* If we were starting, go off hook now ready to debounce */
		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_AFTERSTART, ZT_AFTERSTART_TIME);
		cv_broadcast(&chan->txstateq);
		break;
		
	case ZT_TXSTATE_PREWINK:
		/* Actually wink */
		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_WINK, chan->winktime);
		break;
		
	case ZT_TXSTATE_WINK:
		/* Wink complete, go on hook and stabalize */
		zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, ZT_TXSTATE_ONHOOK, 0);
		if (chan->filemode & O_NONBLOCK)
			zt_qevent_nolock(chan, ZT_EVENT_HOOKCOMPLETE);
		cv_broadcast(&chan->txstateq);
		break;
		
	case ZT_TXSTATE_PREFLASH:
		/* Actually flash */
		zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, ZT_TXSTATE_FLASH, chan->flashtime);
		break;

	case ZT_TXSTATE_FLASH:
		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_OFFHOOK, 0);
		if (chan->filemode & O_NONBLOCK)
			zt_qevent_nolock(chan, ZT_EVENT_HOOKCOMPLETE);
		cv_broadcast(&chan->txstateq);
		break;
	
	case ZT_TXSTATE_DEBOUNCE:
		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_OFFHOOK, 0);
		/* See if we've gone back on hook */
		if (chan->rxhooksig == ZT_RXSIG_ONHOOK)
			chan->itimerset = chan->itimer = chan->rxflashtime * 8;
		cv_broadcast(&chan->txstateq);
		break;
		
	case ZT_TXSTATE_AFTERSTART:
		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_OFFHOOK, 0);
		if (chan->filemode & O_NONBLOCK)
			zt_qevent_nolock(chan, ZT_EVENT_HOOKCOMPLETE);
		cv_broadcast(&chan->txstateq);
		break;

	case ZT_TXSTATE_KEWL:
		zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, ZT_TXSTATE_AFTERKEWL, ZT_AFTERKEWLTIME);
		if (chan->filemode & O_NONBLOCK)
			zt_qevent_nolock(chan, ZT_EVENT_HOOKCOMPLETE);
		cv_broadcast(&chan->txstateq);
		break;

	case ZT_TXSTATE_AFTERKEWL:
		if (chan->kewlonhook)  {
			zt_qevent_nolock(chan,ZT_EVENT_ONHOOK);
		}
		chan->txstate = ZT_TXSTATE_ONHOOK;
		chan->gotgs = 0;
		break;

	case ZT_TXSTATE_PULSEBREAK:
		zt_rbs_sethook(chan, ZT_TXSIG_OFFHOOK, ZT_TXSTATE_PULSEMAKE, 
			chan->pulsemaketime);
		cv_broadcast(&chan->txstateq);
		break;

	case ZT_TXSTATE_PULSEMAKE:
		if (chan->pdialcount)
			chan->pdialcount--;
		if (chan->pdialcount)
		{
			zt_rbs_sethook(chan, ZT_TXSIG_ONHOOK, 
				ZT_TXSTATE_PULSEBREAK, chan->pulsebreaktime);
			break;
		}
		chan->txstate = ZT_TXSTATE_PULSEAFTER;
		chan->otimer = chan->pulseaftertime * 8;
		cv_broadcast(&chan->txstateq);
		break;

	case ZT_TXSTATE_PULSEAFTER:
		chan->txstate = ZT_TXSTATE_OFFHOOK;
		__do_dtmf(chan);
		cv_broadcast(&chan->txstateq);
		break;

	default:
		break;
	}
}

static void __zt_hooksig_pvt(struct zt_chan *chan, zt_rxsig_t rxsig)
{

	/* State machines for receive hookstate transitions 
		called with chan->lock held */

	if ((chan->rxhooksig) == rxsig) return;
	
	if ((chan->flags & ZT_FLAG_SIGFREEZE)) return;

	chan->rxhooksig = rxsig;
	switch(chan->sig) {
	    case ZT_SIG_EM:  /* E and M */
	    case ZT_SIG_EM_E1:
		switch(rxsig) {
		    case ZT_RXSIG_OFFHOOK: /* went off hook */
			/* The interface is going off hook */
			/* set wink timer */
			chan->itimerset = chan->itimer = chan->rxwinktime * 8;
			break;
		    case ZT_RXSIG_ONHOOK: /* went on hook */
			/* This interface is now going on hook.
			   Check for WINK, etc */
			if (chan->itimer)
				zt_qevent_nolock(chan,ZT_EVENT_WINKFLASH); 
			else {
				zt_qevent_nolock(chan,ZT_EVENT_ONHOOK); 
				chan->gotgs = 0;
			}
			chan->itimerset = chan->itimer = 0;
			break;
		    default:
			break;
		}
		break;
	   case ZT_SIG_FXSKS:  /* FXS Kewlstart */
		  /* ignore a bit poopy if loop not closed and stable */
		if (chan->txstate != ZT_TXSTATE_OFFHOOK) break;
		/* fall through intentionally */
	   case ZT_SIG_FXSGS:  /* FXS Groundstart */
		if (rxsig == ZT_RXSIG_ONHOOK) {
			chan->ringdebtimer = RING_DEBOUNCE_TIME;
			chan->ringtrailer = 0;
			if (chan->txstate != ZT_TXSTATE_DEBOUNCE) {
				chan->gotgs = 0;
				zt_qevent_nolock(chan,ZT_EVENT_ONHOOK);
			}
		}
		break;
	   case ZT_SIG_FXOGS: /* FXO Groundstart */
		if (rxsig == ZT_RXSIG_START) {
			  /* if havent got gs, report it */
			if (!chan->gotgs) {
				zt_qevent_nolock(chan,ZT_EVENT_RINGOFFHOOK);
				chan->gotgs = 1;
			}
		}
		/* fall through intentionally */
	   case ZT_SIG_FXOLS: /* FXO Loopstart */
	   case ZT_SIG_FXOKS: /* FXO Kewlstart */
		switch(rxsig) {
		    case ZT_RXSIG_OFFHOOK: /* went off hook */
			  /* if asserti ng ring, stop it */
			if (chan->txstate == ZT_TXSTATE_START) {
				zt_rbs_sethook(chan,ZT_TXSIG_OFFHOOK, ZT_TXSTATE_AFTERSTART, ZT_AFTERSTART_TIME);
			}
			chan->kewlonhook = 0;
#if CONFIG_ZAPATA_DEBUG
			cmn_err(CE_CONT, "Off hook on channel %d, itimer = %d, gotgs = %d\n", chan->channo, chan->itimer, chan->gotgs);
#endif
			if (chan->itimer) /* if timer still running */
			{
			    int plen = chan->itimerset - chan->itimer;
			    if (plen <= ZT_MAXPULSETIME)
			    {
					if (plen >= ZT_MINPULSETIME)
					{
						chan->pulsecount++;
						chan->pulsetimer = ZT_PULSETIMEOUT;
						chan->itimer = chan->itimerset;
						if (chan->pulsecount == 1)
							zt_qevent_nolock(chan,ZT_EVENT_PULSE_START); 
					} 
			    } else 
					zt_qevent_nolock(chan,ZT_EVENT_WINKFLASH); 
			} else {
				  /* if havent got GS detect */
				if (!chan->gotgs) {
					zt_qevent_nolock(chan,ZT_EVENT_RINGOFFHOOK); 
					chan->gotgs = 1;
					chan->itimerset = chan->itimer = 0;
				}
			}
			chan->itimerset = chan->itimer = 0;
			break;
		    case ZT_RXSIG_ONHOOK: /* went on hook */
			  /* if not during offhook debounce time */
			if ((chan->txstate != ZT_TXSTATE_DEBOUNCE) &&
			    (chan->txstate != ZT_TXSTATE_KEWL) && 
			    (chan->txstate != ZT_TXSTATE_AFTERKEWL)) {
				chan->itimerset = chan->itimer = chan->rxflashtime * 8;
			}
			if (chan->txstate == ZT_TXSTATE_KEWL)
				chan->kewlonhook = 1;
			break;
		    default:
			break;
		}
	    default:
		break;
	}
}

void zt_hooksig(struct zt_chan *chan, zt_rxsig_t rxsig)
{
	  /* skip if no change */
	unsigned long flags;
	mutex_enter(&chan->lock);
	__zt_hooksig_pvt(chan,rxsig);
	chan_unlock(chan);
}

void zt_rbsbits(struct zt_chan *chan, int cursig)
{
	unsigned long flags;
	if (cursig == chan->rxsig)
		return;

	if ((chan->flags & ZT_FLAG_SIGFREEZE)) return;

	mutex_enter(&chan->lock);
	switch(chan->sig) {
	    case ZT_SIG_FXOGS: /* FXO Groundstart */
		/* B-bit only matters for FXO GS */
		if (!(cursig & ZT_BBIT)) {
			__zt_hooksig_pvt(chan, ZT_RXSIG_START);
			break;
		}
		/* Fall through */
	    case ZT_SIG_EM:  /* E and M */
	    case ZT_SIG_EM_E1:
	    case ZT_SIG_FXOLS: /* FXO Loopstart */
	    case ZT_SIG_FXOKS: /* FXO Kewlstart */
		if (cursig & ZT_ABIT)  /* off hook */
			__zt_hooksig_pvt(chan,ZT_RXSIG_OFFHOOK);
		else /* on hook */
			__zt_hooksig_pvt(chan,ZT_RXSIG_ONHOOK);
		break;

	   case ZT_SIG_FXSKS:  /* FXS Kewlstart */
	   case ZT_SIG_FXSGS:  /* FXS Groundstart */
	   case ZT_SIG_FXSLS:
		if (!(cursig & ZT_BBIT)) {
			/* Check for ringing first */
			__zt_hooksig_pvt(chan, ZT_RXSIG_RING);
			break;
		}
		if ((chan->sig != ZT_SIG_FXSLS) && (cursig & ZT_ABIT)) { 
			    /* if went on hook */
			__zt_hooksig_pvt(chan, ZT_RXSIG_ONHOOK);
		} else {
			__zt_hooksig_pvt(chan, ZT_RXSIG_OFFHOOK);
		}
		break;
	   case ZT_SIG_CAS:
		/* send event that something changed */
		zt_qevent_nolock(chan, ZT_EVENT_BITSCHANGED);
		break;

	   default:
		break;
	}
	/* Keep track of signalling for next time */
	chan->rxsig = cursig;
	chan_unlock(chan);
}

void zt_ec_chunk(struct zt_chan *ss, unsigned char *rxchunk, const unsigned char *txchunk)
{
	short rxlin, txlin;
	int x;
	unsigned long flags;
	mutex_enter(&ss->lock);
	/* Perform echo cancellation on a chunk if necessary */
	if (ss->ec) {
		if (ss->echostate & __ECHO_STATE_MUTE) {
			/* Special stuff for training the echo can */
			for (x=0;x<ZT_CHUNKSIZE;x++) {
				rxlin = ZT_XLAW(rxchunk[x], ss);
				txlin = ZT_XLAW(txchunk[x], ss);
				if (ss->echostate == ECHO_STATE_PRETRAINING) {
					if (--ss->echotimer <= 0) {
						ss->echotimer = 0;
						ss->echostate = ECHO_STATE_STARTTRAINING;
					}
				}
				if ((ss->echostate == ECHO_STATE_AWAITINGECHO) && (txlin > 8000)) {
					ss->echolastupdate = 0;
					ss->echostate = ECHO_STATE_TRAINING;
				}
				if (ss->echostate == ECHO_STATE_TRAINING) {
					if (echo_can_traintap(ss->ec, ss->echolastupdate++, rxlin)) {
						ss->echostate = ECHO_STATE_ACTIVE;
					}
				}
				rxlin = 0;
				rxchunk[x] = ZT_LIN2X((int)rxlin, ss);
			}
		} else {
			for (x=0;x<ZT_CHUNKSIZE;x++) {
				rxlin = ZT_XLAW(rxchunk[x], ss);
				rxlin = echo_can_update(ss->ec, ZT_XLAW(txchunk[x], ss), rxlin);
				rxchunk[x] = ZT_LIN2X((int)rxlin, ss);
			}
		}
	}
	chan_unlock(ss);
}

/* return 0 if nothing detected, 1 if lack of tone, 2 if presence of tone */
/* modifies buffer pointed to by 'amp' with notched-out values */
static inline int sf_detect (sf_detect_state_t *s,
                 short *amp,
                 int samples,long p1, long p2, long p3)
{
int     i,rv = 0;
long x,y;

#define	SF_DETECT_SAMPLES (ZT_CHUNKSIZE * 5)
#define	SF_DETECT_MIN_ENERGY 500
#define	NB 14  /* number of bits to shift left */
         
        /* determine energy level before filtering */
        for(i = 0; i < samples; i++)
        {
                if (amp[i] < 0) s->e1 -= amp[i];
                else s->e1 += amp[i];
        }
	/* do 2nd order IIR notch filter at given freq. and calculate
	    energy */
        for(i = 0; i < samples; i++)
        {
                x = amp[i] << NB;
                y = s->x2 + (p1 * (s->x1 >> NB)) + x;
                y += (p2 * (s->y2 >> NB)) + 
			(p3 * (s->y1 >> NB));
                s->x2 = s->x1;
                s->x1 = x;
                s->y2 = s->y1;
                s->y1 = y;
                amp[i] = y >> NB;
                if (amp[i] < 0) s->e2 -= amp[i];
                else s->e2 += amp[i];
        }
	s->samps += i;
	/* if time to do determination */
	if ((s->samps) >= SF_DETECT_SAMPLES)
	{
		rv = 1; /* default to no tone */
		/* if enough energy, it is determined to be a tone */
		if (((s->e1 - s->e2) / s->samps) > SF_DETECT_MIN_ENERGY) rv = 2;
		/* reset energy processing variables */
		s->samps = 0;
		s->e1 = s->e2 = 0;
	}
	return(rv);		
}

static inline void __zt_process_putaudio_chunk(struct zt_chan *ss, unsigned char *rxb)
{
	/* We transmit data from our master channel */
	/* Called with ss->lock held */
	struct zt_chan *ms = ss->master;
	/* Linear version of received data */
	short putlin[ZT_CHUNKSIZE],k[ZT_CHUNKSIZE];
	int x,r;

	if (ms->dialing) ms->afterdialingtimer = 50;
	else if (ms->afterdialingtimer) ms->afterdialingtimer--;
	if (ms->afterdialingtimer && (!(ms->flags & ZT_FLAG_PSEUDO))) {
		/* Be careful since memset is likely a macro */
		rxb[0] = ZT_LIN2X(0, ms);
		for(x=1; x<ZT_CHUNKSIZE; x++)
			rxb[x] = rxb[0];
	} 
	for (x=0;x<ZT_CHUNKSIZE;x++) {
		rxb[x] = ms->rxgain[rxb[x]];
		putlin[x] = ZT_XLAW(rxb[x], ms);
	}

#ifndef NO_ECHOCAN_DISABLE
	if (ms->ec) {
		for (x=0;x<ZT_CHUNKSIZE;x++) {
			if (echo_can_disable_detector_update(&ms->rxecdis, putlin[x])) {
				cmn_err(CE_CONT, "zaptel Disabled echo canceller because of tone (rx) on channel %d\n", ss->channo);
				ms->echocancel = 0;
				ms->echostate = ECHO_STATE_IDLE;
				ms->echolastupdate = 0;
				ms->echotimer = 0;
				kmem_free(ms->ec, ms->ec->allocsize);
				ms->ec = NULL;
				break;
			}
		}
	}
#endif	
	/* if doing rx tone decoding */
	if (ms->rxp1 && ms->rxp2 && ms->rxp3)
	{
		r = sf_detect(&ms->rd,putlin,ZT_CHUNKSIZE,ms->rxp1,
			ms->rxp2,ms->rxp3);
		/* Convert back */
		for(x=0;x<ZT_CHUNKSIZE;x++)
			rxb[x] = ZT_LIN2X(putlin[x], ms);
		if (r) /* if something happened */
		{
			if (r != ms->rd.lastdetect)
			{
				if (((r == 2) && !(ms->toneflags & ZT_REVERSE_RXTONE)) ||
				    ((r == 1) && (ms->toneflags & ZT_REVERSE_RXTONE)))
				{
					zt_qevent_nolock(ms,ZT_EVENT_RINGOFFHOOK);
				}
				else
				{
					zt_qevent_nolock(ms,ZT_EVENT_ONHOOK);
				}
				ms->rd.lastdetect = r;
			}
		}
	}		

	if (!(ms->flags &  ZT_FLAG_PSEUDO)) {
		bcopy(putlin, ms->putlin, ZT_CHUNKSIZE * sizeof(short));
		bcopy(rxb, ms->putraw, ZT_CHUNKSIZE);
	}
	
	/* Take the rxc, twiddle it for conferencing if appropriate and put it
	   back */
	if ((!ms->confmute && !ms->afterdialingtimer) ||
	    (ms->flags & ZT_FLAG_PSEUDO)) {
		switch(ms->confmode & ZT_CONF_MODE_MASK) {
		case ZT_CONF_NORMAL:		/* Normal mode */
			/* Do nothing.  rx goes output */
			break;
		case ZT_CONF_MONITOR:		/* Monitor a channel's rx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & ZT_FLAG_PSEUDO)) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & ZT_FLAG_PSEUDO) {
				ACSS(putlin, chans[ms->confna]->getlin);
			} else {
				ACSS(putlin, chans[ms->confna]->putlin);
			}
			/* Convert back */
			for(x=0;x<ZT_CHUNKSIZE;x++)
				rxb[x] = ZT_LIN2X(putlin[x], ms);
			break;
		case ZT_CONF_MONITORTX:	/* Monitor a channel's tx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & ZT_FLAG_PSEUDO)) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & ZT_FLAG_PSEUDO) {
				ACSS(putlin, chans[ms->confna]->putlin);
			} else {
				ACSS(putlin, chans[ms->confna]->getlin);
			}
			/* Convert back */
			for(x=0;x<ZT_CHUNKSIZE;x++)
				rxb[x] = ZT_LIN2X(putlin[x], ms);
			break;
		case ZT_CONF_MONITORBOTH:	/* Monitor a channel's tx and rx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & ZT_FLAG_PSEUDO)) break;
			/* Note: Technically, saturation should be done at 
			   the end of the whole addition, but for performance
			   reasons, we don't do that.  Besides, it only matters
			   when you're so loud you're clipping anyway */
			ACSS(putlin, chans[ms->confna]->getlin);
			ACSS(putlin, chans[ms->confna]->putlin);
			/* Convert back */
			for(x=0;x<ZT_CHUNKSIZE;x++)
				rxb[x] = ZT_LIN2X(putlin[x], ms);
			break;
		case ZT_CONF_REALANDPSEUDO:
			  /* do normal conf mode processing */
			if (ms->confmode & ZT_CONF_TALKER) {
				/* Store temp value */
				bcopy(putlin, k, ZT_CHUNKSIZE * sizeof(short));
				/* Add conf value */
				ACSS(k, conf_sums_next[ms->_confn]);
				/*  get amount actually added */
				bcopy(k, ms->conflast, ZT_CHUNKSIZE * sizeof(short));
				SCSS(ms->conflast, conf_sums_next[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums_next[ms->_confn], ms->conflast);
			} else bzero(ms->conflast, ZT_CHUNKSIZE * sizeof(short));
			  /* do the pseudo-channel part processing */
			bzero(putlin, ZT_CHUNKSIZE * sizeof(short));
			if (ms->confmode & ZT_CONF_PSEUDO_LISTENER) {
				/* Subtract out previous last sample written to conf */
				SCSS(putlin, ms->conflast2);
				/* Add in conference */
				ACSS(putlin, conf_sums[ms->_confn]);
			}
			/* Convert back */
			for(x=0;x<ZT_CHUNKSIZE;x++)
				rxb[x] = ZT_LIN2X(putlin[x], ms);
			break;
		case ZT_CONF_CONF:	/* Normal conference mode */
			if (ms->flags & ZT_FLAG_PSEUDO) /* if a pseudo-channel */
			   {
				if (ms->confmode & ZT_CONF_LISTENER) {
					/* Subtract out last sample written to conf */
					SCSS(putlin, ms->conflast);
					/* Add in conference */
					ACSS(putlin, conf_sums[ms->_confn]);
				}
				/* Convert back */
				for(x=0;x<ZT_CHUNKSIZE;x++)
					rxb[x] = ZT_LIN2X(putlin[x], ms);
				bcopy(putlin, ss->getlin, ZT_CHUNKSIZE * sizeof(short));
				break;
			   }
			/* fall through */
		case ZT_CONF_CONFANN:  /* Conference with announce */
			if (ms->confmode & ZT_CONF_TALKER) {
				/* Store temp value */
				bcopy(putlin, k, ZT_CHUNKSIZE * sizeof(short));
				/* Add conf value */
				ACSS(k, conf_sums_next[ms->_confn]);
				/*  get amount actually added */
				bcopy(k, ms->conflast, ZT_CHUNKSIZE * sizeof(short));
				SCSS(ms->conflast, conf_sums_next[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums_next[ms->_confn], ms->conflast);
			} else 
				bzero(ms->conflast, ZT_CHUNKSIZE * sizeof(short));
			  /* rxc unmodified */
			break;
		case ZT_CONF_CONFMON:
		case ZT_CONF_CONFANNMON:
			if (ms->confmode & ZT_CONF_TALKER) {
				/* Store temp value */
				bcopy(k, putlin, ZT_CHUNKSIZE * sizeof(short));
				/* Subtract last value */
				SCSS(conf_sums[ms->_confn], ms->conflast);
				/* Add conf value */
				ACSS(k, conf_sums[ms->_confn]);
				/*  get amount actually added */
				bcopy(k, ms->conflast, ZT_CHUNKSIZE * sizeof(short));
				SCSS(ms->conflast, conf_sums[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums[ms->_confn], ms->conflast);
			} else 
				bzero(ms->conflast, ZT_CHUNKSIZE * sizeof(short));
			for (x=0;x<ZT_CHUNKSIZE;x++)
				rxb[x] = ZT_LIN2X((int)conf_sums_prev[ms->_confn][x], ms);
			break;
		case ZT_CONF_DIGITALMON:
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & ZT_FLAG_PSEUDO)) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & ZT_FLAG_PSEUDO) {
				bcopy(chans[ms->confna]->getraw, rxb, ZT_CHUNKSIZE);
			} else {
				bcopy(chans[ms->confna]->putraw, rxb, ZT_CHUNKSIZE);
			}
			break;			
		}
	}
}

static inline void __zt_putbuf_chunk(struct zt_chan *ss, unsigned char *rxb)
{
	/* We transmit data from our master channel */
	/* Called with ss->lock held */
	struct zt_chan *ms = ss->master;
	/* Our receive buffer */
	unsigned char *buf;
	int oldbuf;
	int eof=0;
	int abort=0;
	int res;
	int left, x;

	int bytes = ZT_CHUNKSIZE;

	while(bytes) {
		abort = 0;
		eof = 0;
		/* Next, figure out if we've got a buffer to receive into */
		if (ms->inreadbuf > -1) {
			/* Read into the current buffer */
			buf = ms->readbuf[ms->inreadbuf];
			left = ms->blocksize - ms->readidx[ms->inreadbuf];
			if (left > bytes)
				left = bytes;
			if (ms->flags & ZT_FLAG_HDLC) {
				for (x=0;x<left;x++) {
					/* Handle HDLC deframing */
					fasthdlc_rx_load_nocheck(&ms->rxhdlc, *(rxb++));
					bytes--;
					res = fasthdlc_rx_run(&ms->rxhdlc);
					/* If there is nothing there, continue */
					if (res & RETURN_EMPTY_FLAG)
						continue;
					else if (res & RETURN_COMPLETE_FLAG) {
						/* Only count this if it's a non-empty frame */
						if (ms->readidx[ms->inreadbuf]) {
							if ((ms->flags & ZT_FLAG_FCS) && (ms->infcs != PPP_GOODFCS)) {
								abort = ZT_EVENT_BADFCS;
							} else
								eof=1;
							break;
						}
						continue;
					} else if (res & RETURN_DISCARD_FLAG) {
						/* This could be someone idling with 
						  "idle" instead of "flag" */
						if (!ms->readidx[ms->inreadbuf])
							continue;
						abort = ZT_EVENT_ABORT;
						break;
					} else {
						unsigned char rxc;
						rxc = res;
						ms->infcs = PPP_FCS(ms->infcs, rxc);
						buf[ms->readidx[ms->inreadbuf]++] = rxc;
						/* Pay attention to the possibility of an overrun */
						if (ms->readidx[ms->inreadbuf] >= ms->blocksize) {
							if (!ss->span->alarms) 
								cmn_err(CE_CONT, "HDLC Receiver overrun on channel %s (master=%s)\n", ss->name, ss->master->name);
							abort=ZT_EVENT_OVERRUN;
							/* Force the HDLC state back to frame-search mode */
							ms->rxhdlc.state = 0;
							ms->rxhdlc.bits = 0;
							ms->readidx[ms->inreadbuf]=0;
							break;
						}
					}
				}
			} else {
				/* Not HDLC */
				bcopy(rxb, buf + ms->readidx[ms->inreadbuf], left);
				rxb += left;
				ms->readidx[ms->inreadbuf] += left;
				bytes -= left;
				/* End of frame is decided by block size of 'N' */
				eof = (ms->readidx[ms->inreadbuf] >= ms->blocksize);
			}
			if (eof)  {
				/* Finished with this buffer, try another. */
				oldbuf = ms->inreadbuf;
				ms->infcs = PPP_INITFCS;
				ms->readn[ms->inreadbuf] = ms->readidx[ms->inreadbuf];
#if CONFIG_ZAPATA_DEBUG
				cmn_err(CE_CONT, "EOF, len is %d\n", ms->readn[ms->inreadbuf]);
#endif
/**** SL Did I delete too much? *****/
				{
					ms->inreadbuf = (ms->inreadbuf + 1) % ms->numbufs;
					if (ms->inreadbuf == ms->outreadbuf) {
						/* Whoops, we're full, and have no where else
						to store into at the moment.  We'll drop it
						until there's a buffer available */
#if CONFIG_ZAPATA_DEBUG
						cmn_err(CE_CONT, "Out of storage space\n");
#endif
						ms->inreadbuf = -1;
						/* Enable the receiver in case they've got POLICY_WHEN_FULL */
						ms->rxdisable = 0;
					}
					if (ms->outreadbuf < 0) { /* start out buffer if not already */
						ms->outreadbuf = oldbuf;
					}
/* In the very orignal driver, it was quite well known to me (Jim) that there
was a possibility that a channel sleeping on a receive block needed to
be potentially woken up EVERY time a buffer was filled, not just on the first
one, because if only done on the first one there is a slight timing potential
of missing the wakeup (between where it senses the (lack of) active condition
(with interrupts disabled) and where it does the sleep (interrupts enabled)
in the read or iomux call, etc). That is why the read and iomux calls start
with an infinite loop that gets broken out of upon an active condition,
otherwise keeps sleeping and looking. The part in this code got "optimized"
out in the later versions, and is put back now. */
					if (!ms->rxdisable) { /* if receiver enabled */
						/* Notify a blocked reader that there is data available
						to be read, unless we're waiting for it to be full */
#if CONFIG_ZAPATA_DEBUG
						cmn_err(CE_CONT, "Notifying reader data in block %d\n", oldbuf);
#endif
						cv_broadcast(&ms->readbufq);
						pollwakeup(&ms->sel, POLLIN);
						if (ms->iomask & ZT_IOMUX_READ)
							cv_broadcast(&ms->eventbufq);
					}
				}
			}
			if (abort) {
				/* Start over reading frame */
				ms->readidx[ms->inreadbuf] = 0;
				ms->infcs = PPP_INITFCS;

				if ((ms->flags & ZT_FLAG_OPEN) && !ss->span->alarms) 
						/* Notify the receiver... */
					zt_qevent_nolock(ss->master, abort);
			}
		} else /* No place to receive -- drop on the floor */
			break;
	}
}

static void process_timers(void)
{
	unsigned long flags;
	struct zt_timer *cur;
	mutex_enter(&zaptimerlock);
	cur = zaptimers;
	while(cur) {
		if (cur->ms) {
			cur->pos -= ZT_CHUNKSIZE;
			if (cur->pos <= 0) {
				cur->tripped++;
				cur->pos = cur->ms;
				pollwakeup(&cur->sel, POLLPRI|POLLERR);
			}
		}
		cur = cur->next;
	}
	mutex_exit(&zaptimerlock);
}

static int zt_timer_poll(dev_t dev, short events, int anyyet, short *reventsp, struct pollhead **phpp)
{
	struct zt_timer *timer;
	unsigned long flags;
	short ret = 0;

 	timer = chan_timer_map[getminor(dev) - ZT_DEV_TIMER_BASE];
	if (timer) {
		// mutex_enter(&zaptimerlock);
		if (timer->tripped || timer->ping) 
			ret |= POLLPRI|POLLERR;
		// mutex_exit(&zaptimerlock);
		
		if (ret == 0) {
			if (!anyyet) {
				*phpp = &timer->sel;
			}
		}
		*reventsp = ret;
		return 0;
	} else
		return EINVAL;
}

/* device poll routine */
static int zt_chan_poll(dev_t dev, short events, int anyyet, short *reventsp, struct pollhead **phpp)
{   
	
	struct zt_chan *chan;
	short	ret;
	unsigned long flags;
	int	unit = getminor(dev);

	if (debug > 1) cmn_err(CE_CONT, "zt_chan_poll on unit = %d\n", unit);

	if (unit >= ZT_DEV_CHAN_BASE && unit<ZT_DEV_CHAN_BASE+ZT_DEV_CHAN_COUNT)
		unit = chan_map[unit - ZT_DEV_CHAN_BASE];

 	chan = chans[unit];

	  /* do the poll wait */
	if (chan) {
		ret = 0; /* start with nothing to return */
		// mutex_enter(&chan->lock);
		   /* if at least 1 write buffer avail */
		if ((events & (POLLOUT|POLLWRNORM)) && chan->inwritebuf > -1) {
			ret |= POLLOUT | POLLWRNORM;
		}
		if ((events & (POLLIN|POLLRDNORM)) && (chan->outreadbuf > -1) && !chan->rxdisable) {
			ret |= POLLIN | POLLRDNORM;
		}
		if ((events & (POLLPRI|POLLERR)) && chan->eventoutidx != chan->eventinidx)
		   {
			/* Indicate an exception */
			ret |= POLLPRI|POLLERR;
		   }
		if (debug > 1) cmn_err(CE_CONT, "zt_chan_poll: unit=%d, events=%x ret=%x, chan=%lx\n", unit, events, ret, &chan);
		// chan_unlock(chan);
		if (ret == 0) {
			if (!anyyet) {
				*phpp = &chan->sel;
			}
		}
		*reventsp = ret;
		return 0;
	} else
		return EINVAL;
}

static int zt_poll(dev_t dev, short events, int anyyet, short *reventsp, struct pollhead **phpp)
{
	int unit = getminor(dev);
	struct zt_chan *chan;

	if (debug > 1) cmn_err(CE_CONT, "zt_poll: unit=%d\n", unit);

	if (unit == 0)
		return EINVAL;

	if (unit == 253 || unit == 254 || unit == 255) 
		return EINVAL;

	if (unit>=ZT_DEV_TIMER_BASE && unit<ZT_DEV_TIMER_BASE+ZT_DEV_TIMER_COUNT)
		return zt_timer_poll(dev, events, anyyet, reventsp, phpp);

	if (unit < 253 || (unit >= ZT_DEV_CHAN_BASE && unit<ZT_DEV_CHAN_BASE+ZT_DEV_CHAN_COUNT))
		return zt_chan_poll(dev, events, anyyet, reventsp, phpp);

	return EINVAL;
}

static void __zt_transmit_chunk(struct zt_chan *chan, unsigned char *buf)
{
	unsigned char silly[ZT_CHUNKSIZE];
	/* Called with chan->lock locked */
	if (!buf)
		buf = silly;
	__zt_getbuf_chunk(chan, buf);

	if ((chan->flags & ZT_FLAG_AUDIO) || (chan->confmode)) {
		__zt_process_getaudio_chunk(chan, buf);
	}
}

static inline void __zt_real_transmit(struct zt_chan *chan)
{
	/* Called with chan->lock held */
	if (chan->confmode) {
		/* Pull queued data off the conference */
		__buf_pull(&chan->confout, chan->writechunk, chan, "zt_real_transmit");
	} else {
		__zt_transmit_chunk(chan, chan->writechunk);
	}
}

static void __zt_getempty(struct zt_chan *ms, unsigned char *buf)
{
	int bytes = ZT_CHUNKSIZE;
	int left;
	unsigned char *txb = buf;
	int x;
	short getlin;
	/* Called with ms->lock held */

	while(bytes) {
		/* Receive silence, or tone */
		if (ms->curtone) {
			left = ms->curtone->tonesamples - ms->tonep;
			if (left > bytes)
				left = bytes;
			for (x=0;x<left;x++) {
				/* Pick our default value from the next sample of the current tone */
				getlin = zt_tone_nextsample(&ms->ts, ms->curtone);
				*(txb++) = ZT_LIN2X(getlin, ms);
			}
			ms->tonep+=left;
			bytes -= left;
			if (ms->tonep >= ms->curtone->tonesamples) {
				struct zt_tone *last;
				/* Go to the next sample of the tone */
				ms->tonep = 0;
				last = ms->curtone;
				ms->curtone = ms->curtone->next;
				if (!ms->curtone) {
					/* No more tones...  Is this dtmf or mf?  If so, go to the next digit */
					if (ms->dialing)
						__do_dtmf(ms);
				} else {
					if (last != ms->curtone)
						zt_init_tone_state(&ms->ts, ms->curtone);
				}
			}
		} else {
			/* Use silence */
			for (x=0; x<bytes; x++)
				txb[x] = ZT_LIN2X(0, ms);
			bytes = 0;
		}
	}
		
}

static void __zt_receive_chunk(struct zt_chan *chan, unsigned char *buf)
{
	/* Receive chunk of audio -- called with chan->lock held */
	char waste[ZT_CHUNKSIZE];
	int x;

	if (!buf) {
		for (x=0; x<sizeof(waste); x++)
			waste[x] = ZT_LIN2X(0, chan);
		buf = waste;
	}
	if ((chan->flags & ZT_FLAG_AUDIO) || (chan->confmode)) {
		__zt_process_putaudio_chunk(chan, buf);
	}
	__zt_putbuf_chunk(chan, buf);
}

static inline void __zt_real_receive(struct zt_chan *chan)
{
	/* Called with chan->lock held */
	if (chan->confmode) {
		/* Load into queue if we have space */
		__buf_push(&chan->confin, chan->readchunk, "zt_real_receive");
	} else {
		__zt_receive_chunk(chan, chan->readchunk);
	}
}

int zt_transmit(struct zt_span *span)
{
	int x,y,z;
	unsigned long flags;

	if (span == NULL) {
		cmn_err(CE_CONT, "zt_transmit: span is null");
		return (0);
	}
#if 1
	for (x=0;x<span->channels;x++) {
		mutex_enter(&span->chans[x].lock);
		if (&span->chans[x] == span->chans[x].master) {
			if (span->chans[x].otimer) {
				span->chans[x].otimer -= ZT_CHUNKSIZE;
				if (span->chans[x].otimer <= 0) {
					__rbs_otimer_expire(&span->chans[x]);
				}
			}
			if (span->chans[x].flags & ZT_FLAG_AUDIO) {
				__zt_real_transmit(&span->chans[x]);
			} else {
				if (span->chans[x].nextslave) {
					u_char data[ZT_CHUNKSIZE];
					int pos=ZT_CHUNKSIZE;
					/* Process master/slaves one way */
					for (y=0;y<ZT_CHUNKSIZE;y++) {
						/* Process slaves for this byte too */
						z = x;
						do {
							if (pos==ZT_CHUNKSIZE) {
								/* Get next chunk */
								__zt_transmit_chunk(&span->chans[x], data);
								pos = 0;
							}
							span->chans[z].writechunk[y] = data[pos++]; 
							z = span->chans[z].nextslave;
						} while(z);
					}
				} else {
					/* Process independents elsewise */
					__zt_real_transmit(&span->chans[x]);
				}
			}
			if (span->chans[x].sig == ZT_SIG_DACS_RBS) {
				if (chans[span->chans[x].confna]) {
				    	/* Just set bits for our destination */
					if (span->chans[x].txsig != chans[span->chans[x].confna]->rxsig) {
						span->chans[x].txsig = chans[span->chans[x].confna]->rxsig;
						span->rbsbits(&span->chans[x], chans[span->chans[x].confna]->rxsig);
					}
				}
			}

		}
		chan_unlock(&span->chans[x]);
	}
	if (span->mainttimer) {
		span->mainttimer -= ZT_CHUNKSIZE;
		if (span->mainttimer <= 0) {
			span->mainttimer = 0;
			if (span->maint)
				span->maint(span, ZT_MAINT_LOOPSTOP);
			span->maintstat = 0;
			cv_broadcast(&span->maintq);
		}
	}
#endif
	return 0;
}

int zt_receive(struct zt_span *span)
{
	int x,y,z;
	unsigned long flags, flagso;

	if (span == NULL) {
		cmn_err(CE_CONT, "zt_receive: span is null");
		return (0);
	}
#if 1
#ifdef CONFIG_ZAPTEL_WATCHDOG
	span->watchcounter--;
#endif	
	for (x=0;x<span->channels;x++) {
		if (span->chans[x].master == &span->chans[x]) {
			mutex_enter(&span->chans[x].lock);
			if (span->chans[x].nextslave) {
				/* Must process each slave at the same time */
				u_char data[ZT_CHUNKSIZE];
				int pos = 0;
				for (y=0;y<ZT_CHUNKSIZE;y++) {
					/* Put all its slaves, too */
					z = x;
					do {
						data[pos++] = span->chans[z].readchunk[y];
						if (pos == ZT_CHUNKSIZE) {
							__zt_receive_chunk(&span->chans[x], data);
							pos = 0;
						}
						z=span->chans[z].nextslave;
					} while(z);
				}
			} else {
				/* Process a normal channel */
				__zt_real_receive(&span->chans[x]);
			}
			if (span->chans[x].itimer) {
				span->chans[x].itimer -= ZT_CHUNKSIZE;
				if (span->chans[x].itimer <= 0) {
					rbs_itimer_expire(&span->chans[x]);
				}
			}
			if (span->chans[x].ringdebtimer)
				span->chans[x].ringdebtimer--;
			if (span->chans[x].sig & __ZT_SIG_FXS) {
				if (span->chans[x].rxhooksig == ZT_RXSIG_RING)
					span->chans[x].ringtrailer = ZT_RINGTRAILER;
				else if (span->chans[x].ringtrailer) {
					span->chans[x].ringtrailer-= ZT_CHUNKSIZE;
					/* See if RING trailer is expired */
					if (!span->chans[x].ringtrailer && !span->chans[x].ringdebtimer) 
						zt_qevent_nolock(&span->chans[x],ZT_EVENT_RINGOFFHOOK);
				}
			}
			if (span->chans[x].pulsetimer)
			{
				span->chans[x].pulsetimer--;
				if (span->chans[x].pulsetimer <= 0)
				{
					if (span->chans[x].pulsecount)
					{
						if (span->chans[x].pulsecount > 12) {
						
							cmn_err(CE_CONT, "Got pulse digit %d on %s???\n",
						    span->chans[x].pulsecount,
							span->chans[x].name);
						} else if (span->chans[x].pulsecount > 11) {
							zt_qevent_nolock(&span->chans[x], ZT_EVENT_PULSEDIGIT | '#');
						} else if (span->chans[x].pulsecount > 10) {
							zt_qevent_nolock(&span->chans[x], ZT_EVENT_PULSEDIGIT | '*');
						} else if (span->chans[x].pulsecount > 9) {
							zt_qevent_nolock(&span->chans[x], ZT_EVENT_PULSEDIGIT | '0');
						} else {
							zt_qevent_nolock(&span->chans[x], ZT_EVENT_PULSEDIGIT | ('0' + 
								span->chans[x].pulsecount));
						}
						span->chans[x].pulsecount = 0;
					}
				}
			}
			chan_unlock(&span->chans[x]);
		}
	}

	if (span == master) {
		/* Hold the big zap lock for the duration of major
		   activities which touch all sorts of channels */
		mutex_enter(&bigzaplock);			
		/* Process any timers */
		process_timers();
		/* If we have dynamic stuff, call the ioctl with 0,0 parameters to
		   make it run */
		if (zt_dynamic_ioctl)
			zt_dynamic_ioctl(0,0,0);
		for (x=1;x<maxchans;x++) {
			if (chans[x] && chans[x]->confmode && !(chans[x]->flags & ZT_FLAG_PSEUDO)) {
				u_char *data;
				mutex_enter(&chans[x]->lock);
				data = __buf_peek(&chans[x]->confin);
				__zt_receive_chunk(chans[x], data);
				if (data)
					__buf_pull(&chans[x]->confin, NULL,chans[x], "confreceive");
				chan_unlock(chans[x]);
			}
		}
		/* This is the master channel, so make things switch over */
		rotate_sums();
		/* do all the pseudo and/or conferenced channel receives (getbuf's) */
		for (x=1;x<maxchans;x++) {
			if (chans[x] && (chans[x]->flags & ZT_FLAG_PSEUDO)) {
				mutex_enter(&chans[x]->lock);
				__zt_transmit_chunk(chans[x], NULL);
				chan_unlock(chans[x]);
			}
		}
		if (maxlinks) {
			  /* process all the conf links */
			for(x = 1; x <= maxlinks; x++) {
				  /* if we have a destination conf */
				if (((z = confalias[conf_links[x].dst]) > 0) &&
				    ((y = confalias[conf_links[x].src]) > 0)) {
					ACSS(conf_sums[z], conf_sums[y]);
				}
			}
		}
		/* do all the pseudo/conferenced channel transmits (putbuf's) */
		for (x=1;x<maxchans;x++) {
			if (chans[x] && (chans[x]->flags & ZT_FLAG_PSEUDO)) {
				unsigned char tmp[ZT_CHUNKSIZE];
				mutex_enter(&chans[x]->lock);
				__zt_getempty(chans[x], tmp);
				__zt_receive_chunk(chans[x], tmp);
				chan_unlock(chans[x]);
			}
		}
		for (x=1;x<maxchans;x++) {
			if (chans[x] && chans[x]->confmode && !(chans[x]->flags & ZT_FLAG_PSEUDO)) {
				u_char *data;
				mutex_enter(&chans[x]->lock);
				data = __buf_pushpeek(&chans[x]->confout);
				__zt_transmit_chunk(chans[x], data);
				if (data)
					__buf_push(&chans[x]->confout, NULL, "conftransmit");
				chan_unlock(chans[x]);
			}
		}
		mutex_exit(&bigzaplock);			
	}
#endif
	return 0;
}

static struct cb_ops    zt_cb_ops = {
    zt_open,                    /* open() */
    zt_release,                 /* close() */
    nodev,                      /* strategy()           */
    nodev,                      /* print routine        */
    nodev,                      /* no dump routine      */
    zt_read,                    /* read() */
    zt_write,                   /* write() */
    zt_ioctl,                   /* generic ioctl */
    nodev,                      /* no devmap routine    */
    nodev,                      /* no mmap routine      */
    nodev,                      /* no segmap routine    */
    zt_poll,                    /* no chpoll routine    */
    ddi_prop_op,
    NULL,                       /* a STREAMS driver     */
    D_NEW | D_MP,               /* safe for multi-thread/multi-processor */
    0,                          /* cb_ops version? */
    nodev,                      /* cb_aread() */
    nodev,                      /* cb_awrite() */
};

static int zt_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result);
static int zt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int zt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

static struct dev_ops zt_ops = {
    DEVO_REV,                   /* devo_rev */
    0,                          /* devo_refcnt */
    zt_getinfo,                 /* devo_getinfo */
    nulldev,                    /* devo_identify */
    nulldev,                    /* devo_probe */
    zt_attach,                  /* devo_attach */
    zt_detach,                  /* devo_detach */
    nodev,                      /* devo_reset */
    &zt_cb_ops,                 /* devo_cb_ops */
    (struct bus_ops *)0,        /* devo_bus_ops */
    NULL,                       /* devo_power */
};

static  struct modldrv modldrv = {
    &mod_driverops,
    "Zapata Telephony Interface",
    &zt_ops,
};

static  struct modlinkage modlinkage = {
    MODREV_1,                   /* MODREV_1 is indicated by manual */
    { &modldrv, NULL, NULL, NULL }
};

int _init(void)
{
  int ret, idx;
  size_t softstateSize = sizeof(zt_soft_state_t);

  for (idx=0; idx<ZT_MAX_CHANNELS; idx++)
	chans[idx] = 0;
  for (idx=0; idx<ZT_MAX_SPANS; idx++)
	spans[idx] = 0;
  for (idx=0; idx<ZT_DEV_CHAN_COUNT; idx++)
	chan_map[idx] = 0;
  for (idx=0; idx<ZT_DEV_TIMER_COUNT; idx++)
	chan_timer_map[idx] = 0;

  ztsoftstatep = NULL;
  ret = ddi_soft_state_init(&ztsoftstatep,
                              softstateSize,
                              4);
  if (ret != 0)
  {
    cmn_err(CE_CONT, "zaptel: ddi_soft_state_init failed: %d - size=%d p=%p", ret, softstateSize, &ztsoftstatep);
    return DDI_FAILURE;
  }

  if ((ret = mod_install(&modlinkage)) != 0) {
    cmn_err(CE_CONT, "zaptel: _init FAILED\n");
    return DDI_FAILURE;
  }

  if (debug) cmn_err(CE_CONT, "zaptel: _init SUCCESS\n");
  return DDI_SUCCESS;
}

int _info(struct modinfo *modinfop)
{
    return mod_info(&modlinkage, modinfop);
}

int _fini(void)
{
    int ret;

    /*
     * If mod_remove() is successful, we destroy our global mutex
     */
    if ((ret = mod_remove(&modlinkage)) == 0) {
        ddi_soft_state_fini(&ztsoftstatep);
    }
    if (debug) cmn_err(CE_CONT, "zaptel: _fini %s\n", ret == DDI_SUCCESS ? "SUCCESS" : "FAILURE");
    return ret;
}

#ifdef CONFIG_ZAPTEL_WATCHDOG
static struct timer_list watchdogtimer;

static void watchdog_check(unsigned long ignored)
{
	int x;
	unsigned long flags;
	static int wdcheck=0;
	
	local_irq_save(flags);
	for (x=0;x<maxspans;x++) {
		if (spans[x] && (spans[x]->flags & ZT_FLAG_RUNNING)) {
			if (spans[x]->watchcounter == ZT_WATCHDOG_INIT) {
				/* Whoops, dead card */
				if ((spans[x]->watchstate == ZT_WATCHSTATE_OK) || 
					(spans[x]->watchstate == ZT_WATCHSTATE_UNKNOWN)) {
					spans[x]->watchstate = ZT_WATCHSTATE_RECOVERING;
					if (spans[x]->watchdog) {
						cmn_err(CE_CONT, "Kicking span %s\n", spans[x]->name);
						spans[x]->watchdog(spans[x], ZT_WATCHDOG_NOINTS);
					} else {
						cmn_err(CE_CONT, "Span %s is dead with no revival\n", spans[x]->name);
						spans[x]->watchstate = ZT_WATCHSTATE_FAILED;
					}
				}
			} else {
				if ((spans[x]->watchstate != ZT_WATCHSTATE_OK) &&
					(spans[x]->watchstate != ZT_WATCHSTATE_UNKNOWN))
						cmn_err(CE_CONT, "Span %s is alive!\n", spans[x]->name);
				spans[x]->watchstate = ZT_WATCHSTATE_OK;
			}
			spans[x]->watchcounter = ZT_WATCHDOG_INIT;
		}
	}
	local_irq_restore(flags);
	if (!wdcheck) {
		cmn_err(CE_CONT, "Zaptel watchdog on duty!\n");
		wdcheck=1;
	}
	mod_timer(&watchdogtimer, jiffies + 2);
}

static int watchdog_init(void)
{
	init_timer(&watchdogtimer);
	watchdogtimer.expires = 0;
	watchdogtimer.data =0;
	watchdogtimer.function = watchdog_check;
	/* Run every couple of jiffy or so */
	mod_timer(&watchdogtimer, jiffies + 2);
	return 0;
}

static void watchdog_cleanup(void)
{
	del_timer(&watchdogtimer);
}

#endif


static int zt_init(dev_info_t *dip) {
	struct zt_soft_state *state;
	int instance, status;
	char *getdev_name;
	int res = DDI_SUCCESS;
	int x;

	instance = ddi_get_instance(dip);
	if (debug) cmn_err(CE_CONT, "zaptel%d: attach\n", instance); 

	if (instance != 0)
	{
		cmn_err(CE_CONT, "zaptel: Only instance 0 supported\n");
		return DDI_FAILURE;
	}

	/* Allocate some memory for this instance */
	if (ddi_soft_state_zalloc(ztsoftstatep, instance) != DDI_SUCCESS)
	{
		cmn_err(CE_CONT, "zaptel%d: Failed to alloc soft state", instance);
		return DDI_FAILURE;
	}

	/* Get pointer to that memory */
	state = ddi_get_soft_state(ztsoftstatep, instance);
	if (state == NULL)
	{
		cmn_err(CE_CONT, "zaptel%d: attach, failed to get soft state", instance);
		ddi_soft_state_free(ztsoftstatep, instance);
		return DDI_FAILURE;
	}

  	state->dip = dip;

	if (ddi_create_minor_node(dip, "timer", S_IFCHR, 253, DDI_NT_ZAP, 0) == DDI_FAILURE ||
	    ddi_create_minor_node(dip, "channel", S_IFCHR, 254, DDI_NT_ZAP, 0) == DDI_FAILURE ||
	    ddi_create_minor_node(dip, "pseudo", S_IFCHR, 255, DDI_NT_ZAP, 0) == DDI_FAILURE ||
	    ddi_create_minor_node(dip, "ctl", S_IFCHR, 0, DDI_NT_ZAP, 0) == DDI_FAILURE)
	{
		ddi_soft_state_free(ztsoftstatep, instance);
		ddi_remove_minor_node(dip, NULL);
		return DDI_FAILURE;
	}

	cmn_err(CE_CONT, "Zapata Telephony Interface Registered\n");
	zt_conv_init();
	if (debug) cmn_err(CE_CONT, "zt_conv_init\n");
	tone_zone_init();
	if (debug) cmn_err(CE_CONT, "tone_zone_init\n");
	fasthdlc_precalc();
	if (debug) cmn_err(CE_CONT, "fasthdlc_precalc\n");
	rotate_sums();
	if (debug) cmn_err(CE_CONT, "rotate_sums\n");
	rw_init(&chan_lock, NULL, RW_DRIVER, NULL);
	if (debug) cmn_err(CE_CONT, "rw_init chan_lock\n");
	rw_init(&zone_lock, NULL, RW_DRIVER, NULL);
	if (debug) cmn_err(CE_CONT, "rw_init zone_lock\n");
#ifdef CONFIG_ZAPTEL_WATCHDOG
	watchdog_init();
#endif	

	for (x=0; x<ZT_DEV_CHAN_COUNT; x++)
		chan_map[x] = -1;

	if (debug) cmn_err(CE_CONT, "leaving zt_init\n");
	return res;
}

static int zt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_RESUME:
		cmn_err(CE_CONT, "zaptel: Ignoring attach_RESUME");
		return DDI_FAILURE;
	case DDI_PM_RESUME:
		cmn_err(CE_CONT, "zaptel: Ignoring attach_PM_RESUME");
		return DDI_FAILURE;
	case DDI_ATTACH:
		return zt_init(dip);
	default:
		cmn_err(CE_CONT, "zaptel: unknown attach command %d", cmd);
		return DDI_FAILURE;
	}
}

static void zt_cleanup(void) {
	int x;

	cmn_err(CE_CONT, "Zapata Telephony Interface Unloaded\n");
	for (x=0;x<ZT_TONE_ZONE_MAX;x++)
		if (tone_zones[x])
			if (tone_zones[x]->allocsize)
				kmem_free(tone_zones[x], tone_zones[x]->allocsize);
#ifdef CONFIG_ZAPTEL_WATCHDOG
	watchdog_cleanup();
#endif
}

static int zt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int instance;
	struct zt_soft_state *state;

	instance = ddi_get_instance(dip);
	/* cmn_err(CE_CONT, "zaptel%d: detach", instance); */

	state = ddi_get_soft_state(ztsoftstatep, instance);
	if (state == NULL) 
	{
		cmn_err(CE_CONT, "zaptel%d: detach, failed to get soft state", instance);
		return DDI_FAILURE;
    	}
	
	zt_cleanup();

	ddi_remove_minor_node(dip, NULL);

	return DDI_SUCCESS;
}

static int zt_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int instance;
        struct zt_soft_state *state;
        int error = DDI_FAILURE;

        switch (infocmd) {
        case DDI_INFO_DEVT2DEVINFO:
                if ((state = ddi_get_soft_state(ztsoftstatep, getminor((dev_t)arg))) != NULL) {
                        *result = state->dip;
                        error = DDI_SUCCESS;
                } else
                        *result = NULL;
                break;

        case DDI_INFO_DEVT2INSTANCE:
		instance = getminor((dev_t) arg);
                *result = (void *)(long)instance;
                error = DDI_SUCCESS;
                break;

        default:
                break;
        }

        return (error);
}


