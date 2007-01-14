/*
 * Dynamic Span Interface for Zaptel
 *
 * Written by Mark Spencer <markster@linux-support.net>
 *
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * All rights reserved.
 *
 * Solaris Port By Joseph Benden and others at SolarisVoip.com
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

#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <netinet/in.h>
#include <stddef.h>

/* Must be after other includes */
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cyclic.h>
#include <sys/modctl.h>

#ifdef STANDALONE_ZAPATA
#include "zaptel.h"
#else
#include <zaptel.h>
#endif

#include "compat.h"

/*
	Redefine these, because they are compatible from the above header file.
*/
#undef spin_lock_init
#define spin_lock_init(a) mutex_init(a, NULL, MUTEX_DRIVER, NULL)

#ifdef BIG_ENDIAN
#define htons(x) (x)
#define ntohs(x) (x)
#else
// fix me
#endif

struct ztdynamic_state {
	dev_info_t *dip;
	timeout_id_t timerid;
	cyclic_id_t cyclic;
};

char _depends_on[] = "drv/zaptel";
static void *ztdynamic_statep;

/*
 *  Dynamic spans implemented using TDM over X with standard message
 *  types.  Message format is as follows:
 *
 *         Byte #:          Meaning
 *         0                Number of samples per channel
 *         1                Current flags on span
 *		   Bit    0: Yellow Alarm
 *	                        Bit    1: Sig bits present
 *		Bits 2-7: reserved for future use
 *         2-3		    16-bit counter value for detecting drops, network byte order.
 *         4-5		    Number of channels in the message, network byte order
 *         6...		    16-bit words, containing sig bits for each
 *                          four channels, least significant 4 bits being
 *                          the least significant channel, network byte order.
 *         the rest	    data for each channel, all samples per channel
                            before moving to the next.
 */

/* Arbitrary limit to the max # of channels in a span */
#define ZT_DYNAMIC_MAX_CHANS	256

#define ZTD_FLAG_YELLOW_ALARM		(1 << 0)
#define ZTD_FLAG_SIGBITS_PRESENT	(1 << 1)
#define ZTD_FLAG_LOOPBACK			(1 << 2)

#define ERR_NSAMP					(1 << 16)
#define ERR_NCHAN					(1 << 17)
#define ERR_LEN						(1 << 18)

#ifdef ENABLE_TASKLETS
static int taskletrun;
static int taskletsched;
static int taskletpending;
static int taskletexec;
static int txerrors;
static struct tasklet_struct ztd_tlet;

static void ztd_tasklet(unsigned long data);
#endif


static struct zt_dynamic {
	char addr[40];
	char dname[20];
	int err;
	int alarm;
	int usecount;
	int dead;
	long rxjif;
	unsigned short txcnt;
	unsigned short rxcnt;
	struct zt_span span;
	struct zt_chan *chans;
	size_t chans_size;
	struct zt_dynamic *next;
	struct zt_dynamic_driver *driver;
	void *pvt;
	int timing;
	int master;
	unsigned char *msgbuf;
	size_t msgbuf_size;
} *dspans;
	
static struct zt_dynamic_driver *drivers =  NULL;

static int debug = 1;

static int hasmaster = 0;

static spinlock_t dlock;

static void checkmaster(void)
{
	unsigned long flags;
	int newhasmaster=0;
	int best = 9999999;
	struct zt_dynamic *z, *master=NULL;
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	while(z) {
		if (z->timing) {
			if (z->timing) {
				z->master = 0;
				newhasmaster = 1;
				if (!z->alarm && (z->timing < best) && !z->dead) {
					/* If not in alarm and they're
					   a better timing source, use them */
					master = z;
					best = z->timing;
				}
			}
		}
		z = z->next;
	}
	hasmaster = newhasmaster;
	/* Mark the new master if there is one */
	if (master)
		master->master = 1;
	spin_unlock_irqrestore(&dlock, flags);
	if (master)
		printk("TDMoX: New master: %s\n", master->span.name);
	else
		printk("TDMoX: No master.\n");
}

static void ztd_sendmessage(struct zt_dynamic *z)
{
	unsigned char *buf = z->msgbuf;
	unsigned short bits;
	int msglen = 0;
	int x;
	int offset;

	/* Byte 0: Number of samples per channel */
	*buf = ZT_CHUNKSIZE;
	buf++; msglen++;

	/* Byte 1: Flags */
	*buf = 0;
	if (z->alarm & ZT_ALARM_RED)
		*buf |= ZTD_FLAG_YELLOW_ALARM;
	*buf |= ZTD_FLAG_SIGBITS_PRESENT;
	buf++; msglen++;

	/* Bytes 2-3: Transmit counter */
	*((unsigned short *)buf) = htons((unsigned short)z->txcnt);
	z->txcnt++;
	buf++; msglen++;
	buf++; msglen++;

	/* Bytes 4-5: Number of channels */
	*((unsigned short *)buf) = htons((unsigned short)z->span.channels);
	buf++; msglen++;
	buf++; msglen++;
	bits = 0;
	offset = 0;
	for (x=0;x<z->span.channels;x++) {
		offset = x % 4;
		bits |= (z->chans[x].txsig & 0xf) << (offset << 2);
		if (offset == 3) {
			/* Write the bits when we have four channels */
			*((unsigned short *)buf) = htons(bits);
			buf++; msglen++;
			buf++; msglen++;
			bits = 0;
		}
	}

	if (offset != 3) {
		/* Finish it off if it's not done already */
		*((unsigned short *)buf) = htons(bits);
		buf++; msglen++;
		buf++; msglen++;
	}
	
	for (x=0;x<z->span.channels;x++) {
		memcpy(buf, z->chans[x].writechunk, ZT_CHUNKSIZE);
		buf += ZT_CHUNKSIZE;
		msglen += ZT_CHUNKSIZE;
	}
	
	z->driver->transmit(z->pvt, z->msgbuf, msglen);
	
}

static inline void __ztdynamic_run(void)
{
	unsigned long flags;
	struct zt_dynamic *z;
	int y;
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	while(z) {
		if (!z->dead) {
			/* Ignore dead spans */
			for (y=0;y<z->span.channels;y++) {
				/* Echo cancel double buffered data */
				zt_ec_chunk(&z->span.chans[y], z->span.chans[y].readchunk, z->span.chans[y].writechunk);
			}
			zt_receive(&z->span);
			zt_transmit(&z->span);
			/* Handle all transmissions now */
			ztd_sendmessage(z);
		}
		z = z->next;
	}
	spin_unlock_irqrestore(&dlock, flags);
}

#ifdef ENABLE_TASKLETS
static void ztdynamic_run(void)
{
	if (!taskletpending) {
		taskletpending = 1;
		taskletsched++;
		tasklet_hi_schedule(&ztd_tlet);
	} else {
		txerrors++;
	}
}
#else
#define ztdynamic_run __ztdynamic_run
#endif

void zt_dynamic_receive(struct zt_span *span, unsigned char *msg, int msglen)
{
	struct zt_dynamic *ztd = span->pvt;
	int newerr=0;
	unsigned long flags;
	int sflags;
	int xlen;
	int x, bits, sig;
	int nchans, master;
	int newalarm;
	unsigned short rxpos;
	
	spin_lock_irqsave(&dlock, flags);
	if (msglen < 6) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_LEN;
		if (newerr != ztd->err) {
			printk("Span %s: Insufficient samples for header (only %d)\n", span->name, msglen);
		}
		ztd->err = newerr;
		return;
	}
	
	/* First, check the chunksize */
	if (*msg != ZT_CHUNKSIZE) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_NSAMP | msg[0];
		if (newerr != 	ztd->err) {
			printk("Span %s: Expected %d samples, but receiving %d\n", span->name, ZT_CHUNKSIZE, msg[0]);
		}
		ztd->err = newerr;
		return;
	}
	msg++;
	sflags = *msg;
	msg++;
	
	rxpos = ntohs(*((unsigned short *)msg));
	msg++;
	msg++;
	
	nchans = ntohs(*((unsigned short *)msg));
	if (nchans != span->channels) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_NCHAN | nchans;
		if (newerr != ztd->err) {
			printk("Span %s: Expected %d channels, but receiving %d\n", span->name, span->channels, nchans);
		}
		ztd->err = newerr;
		return;
	}
	msg++;
	msg++;
	
	/* Okay now we've accepted the header, lets check our message
	   length... */

	/* Start with header */
	xlen = 6;
	/* Add samples of audio */
	xlen += nchans * ZT_CHUNKSIZE;
	/* If RBS info is there, add that */
	if (sflags & ZTD_FLAG_SIGBITS_PRESENT) {
		/* Account for sigbits -- one short per 4 channels*/
		xlen += ((nchans + 3) / 4) * 2;
	}
	
	if (xlen != msglen) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_LEN | xlen;
		if (newerr != ztd->err) {
			printk("Span %s: Expected message size %d, but was %d instead\n", span->name, xlen, msglen);
		}
		ztd->err = newerr;
		return;
	}
	
	bits = 0;
	
	/* Record sigbits if present */
	if (sflags & ZTD_FLAG_SIGBITS_PRESENT) {
		for (x=0;x<nchans;x++) {
			if (!(x%4)) {
				/* Get new bits */
				bits = ntohs(*((unsigned short *)msg));
				msg++;
				msg++;
			}
			
			/* Pick the right bits */
			sig = (bits >> ((x % 4) << 2)) & 0xff;
			
			/* Update signalling if appropriate */
			if (sig != span->chans[x].rxsig)
				zt_rbsbits(&span->chans[x], sig);
				
		}
	}
	
	/* Record data for channels */
	for (x=0;x<nchans;x++) {
		memcpy(span->chans[x].readchunk, msg, ZT_CHUNKSIZE);
		msg += ZT_CHUNKSIZE;
	}

	master = ztd->master;
	
	spin_unlock_irqrestore(&dlock, flags);
	
	/* Check for Yellow alarm */
	newalarm = span->alarms & ~(ZT_ALARM_YELLOW | ZT_ALARM_RED);
	if (sflags & ZTD_FLAG_YELLOW_ALARM)
		newalarm |= ZT_ALARM_YELLOW;

	if (newalarm != span->alarms) {
		span->alarms = newalarm;
		zt_alarm_notify(span);
	}
	
	/* Keep track of last received packet */
	ztd->rxjif = gethrtime();

	/* If this is our master span, then run everything */
	if (master)
		ztdynamic_run();
	
}

static void dynamic_destroy(struct zt_dynamic *z)
{
	/* Unregister span if appropriate */
	if (z->span.flags & ZT_FLAG_REGISTERED)
		zt_unregister(&z->span);

	/* Destroy the pvt stuff if there */
	if (z->pvt)
		z->driver->destroy(z->pvt);

	/* Free message buffer if appropriate */
	if (z->msgbuf)
		kmem_free(z->msgbuf, z->msgbuf_size);

	/* Free channels */
	if (z->chans);
		kmem_free(z->chans, z->chans_size);

	/* Free z */
	kmem_free(z, sizeof(z));

	checkmaster();
}

static struct zt_dynamic *find_dynamic(ZT_DYNAMIC_SPAN *zds)
{
	struct zt_dynamic *z;
	z = dspans;
	while(z) {
		if (!strcmp(z->dname, zds->driver) &&
		    !strcmp(z->addr, zds->addr))
			break;
		z = z->next;
	}
	return z;
}

static struct zt_dynamic_driver *find_driver(char *name)
{
	struct zt_dynamic_driver *ztd;
	ztd = drivers;
	while(ztd) {
		/* here's our driver */
		if (!strcmp(name, ztd->name))
			break;
		ztd = ztd->next;
	}
	return ztd;
}

static int destroy_dynamic(ZT_DYNAMIC_SPAN *zds)
{
	unsigned long flags;
	struct zt_dynamic *z, *cur, *prev=NULL;
	spin_lock_irqsave(&dlock, flags);
	z = find_dynamic(zds);
	if (!z) {
		spin_unlock_irqrestore(&dlock, flags);
		return -EINVAL;
	}
	/* Don't destroy span until it is in use */
	if (z->usecount) {
		spin_unlock_irqrestore(&dlock, flags);
		printk("Attempt to destroy dynamic span while it is in use\n");
		return -EBUSY;
	}
	/* Unlink it */
	cur = dspans;
	while(cur) {
		if (cur == z) {
			if (prev)
				prev->next = z->next;
			else
				dspans = z->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	/* Destroy it */
	dynamic_destroy(z);

	spin_unlock_irqrestore(&dlock, flags);
	return (0);
}

static int ztd_rbsbits(struct zt_chan *chan, int bits)
{
	/* Don't have to do anything */
	return 0;
}

static int ztd_open(struct zt_chan *chan)
{
	struct zt_dynamic *z;
	z = chan->span->pvt;
	if (z) {
		if (z->dead)
			return -ENODEV;
		z->usecount++;
	}
	return 0;
}

static int ztd_chanconfig(struct zt_chan *chan, int sigtype)
{
	return 0;
}

static int ztd_close(struct zt_chan *chan)
{
	struct zt_dynamic *z;
	z = chan->span->pvt;
	if (z) 
		z->usecount--;
	if (z->dead && !z->usecount)
		dynamic_destroy(z);
	return 0;
}

static int create_dynamic(ZT_DYNAMIC_SPAN *zds)
{
	struct zt_dynamic *z;
	struct zt_dynamic_driver *ztd;
	unsigned long flags;
	int x;
	int bufsize;

	if (zds->numchans < 1) {
		printk("Can't be less than 1 channel (%d)!\n", zds->numchans);
		return -EINVAL;
	}
	if (zds->numchans >= ZT_DYNAMIC_MAX_CHANS) {
		printk("Can't create dynamic span with greater than %d channels.  See ztdynamic.c and increase ZT_DYNAMIC_MAX_CHANS\n", zds->numchans);
		return -EINVAL;
	}

	spin_lock_irqsave(&dlock, flags);
	z = find_dynamic(zds);
	spin_unlock_irqrestore(&dlock, flags);
	if (z)
		return -EEXIST;

	/* XXX There is a silly race here.  We check it doesn't exist, but
	       someone could create it between now and then and we'd end up
	       with two of them.  We don't want to hold the spinlock
	       for *too* long though, especially not if there is a possibility
	       of kmalloc.  XXX */


	/* Allocate memory */
	z = (struct zt_dynamic *)kmem_zalloc(sizeof(struct zt_dynamic), KM_NOSLEEP);
	if (!z) 
		return -ENOMEM;

	/* Allocate other memories */
	z->chans_size = sizeof(struct zt_chan) * zds->numchans;
	z->chans = kmem_zalloc(z->chans_size, KM_NOSLEEP);
	if (!z->chans) {
		dynamic_destroy(z);
		return -ENOMEM;
	}

	/* Allocate message buffer with sample space and header space */
	bufsize = zds->numchans * ZT_CHUNKSIZE + zds->numchans / 4 + 48;

	z->msgbuf_size = bufsize;
	z->msgbuf = kmem_zalloc(bufsize, KM_NOSLEEP);

	if (!z->msgbuf) {
		dynamic_destroy(z);
		return -ENOMEM;
	}

	/* Setup parameters properly assuming we're going to be okay. */
	strncpy(z->dname, zds->driver, sizeof(z->driver) - 1);
	strncpy(z->addr, zds->addr, sizeof(z->addr) - 1);
	z->timing = zds->timing;
	sprintf(z->span.name, "ZTD/%s/%s", zds->driver, zds->addr);
	sprintf(z->span.desc, "Dynamic '%s' span at '%s'", zds->driver, zds->addr);
	z->span.channels = zds->numchans;
	z->span.pvt = z;
	z->span.deflaw = ZT_LAW_MULAW;
	z->span.flags |= ZT_FLAG_RBS;
	z->span.chans = z->chans;
	z->span.rbsbits = ztd_rbsbits;
	z->span.open = ztd_open;
	z->span.close = ztd_close;
	z->span.chanconfig = ztd_chanconfig;
	for (x=0;x<zds->numchans;x++) {
		sprintf(z->chans[x].name, "ZTD/%s/%s/%d", zds->driver, zds->addr, x+1);
		z->chans[x].sigcap = ZT_SIG_EM | ZT_SIG_CLEAR | ZT_SIG_FXSLS |
				     ZT_SIG_FXSKS | ZT_SIG_FXSGS | ZT_SIG_FXOLS |
				     ZT_SIG_FXOKS | ZT_SIG_FXOGS | ZT_SIG_SF | ZT_SIG_DACS_RBS;
		z->chans[x].chanpos = x + 1;
		z->chans[x].pvt = z;
	}
	
	spin_lock_irqsave(&dlock, flags);
	ztd = find_driver(zds->driver);
#if 0
	if (!ztd) {
		spin_unlock_irqrestore(&dlock, flags);
		
		/* Try loading the right module */
		size_t size = 6 + strlen(zds->driver);
		char *fn = kmem_zalloc(size, KM_SLEEP);
		(void) sprintf(fn, "ztd-%s", zds->driver);
		
		/* JWB: this isn't a support solaris call, I don't think */
		if (modload("drv", fn) == -1) {
			cmn_err(CE_CONT, "modload failed to load %s\n", fn);
		}
		
		kmem_free(fn, size);
		
		spin_lock_irqsave(&dlock, flags);
		ztd = find_driver(zds->driver);
	}
#endif
	spin_unlock_irqrestore(&dlock, flags);


	/* Another race -- should let the module get unloaded while we
	   have it here */
	if (!ztd) {
		printk("No such driver '%s' for dynamic span\n", zds->driver);
		dynamic_destroy(z);
		return -EINVAL;
	}

	/* Create the stuff */
	z->pvt = ztd->create(&z->span, z->addr);
	if (!z->pvt) {
		printk("Driver '%s' (%s) rejected address '%s'\n", ztd->name, ztd->desc, z->addr);
		/* Creation failed */
		return -EINVAL;
	}

	/* Remember the driver */
	z->driver = ztd;

	/* Whee!  We're created.  Now register the span */
	if (zt_register(&z->span, 0)) {
		printk("Unable to register span '%s'\n", z->span.name);
		dynamic_destroy(z);
		return -EINVAL;
	}

	/* Okay, created and registered. add it to the list */
	spin_lock_irqsave(&dlock, flags);
	z->next = dspans;
	dspans = z;
	spin_unlock_irqrestore(&dlock, flags);

	checkmaster();

	/* All done */
	return z->span.spanno;

}

static int ztdynamic_ioctl(int cmd, intptr_t data, int mode)
{
	ZT_DYNAMIC_SPAN zds;
	int res;
	switch(cmd) {
	case 0:
		/* This is called just before rotation.  If none of our
		   spans are pulling timing, then now is the time to process
		   them */
		if (!hasmaster)
			ztdynamic_run();
		return 0;
	case ZT_DYNAMIC_CREATE:
		ddi_copyin((void *)data, &zds, sizeof(zds), mode);
		//if (copy_from_user(&zds, (ZT_DYNAMIC_SPAN *)data, sizeof(zds)))
		//	return -EFAULT;
		if (debug)
			printk("Dynamic Create\n");
		res = create_dynamic(&zds);
		if (res < 0)
			return res;
		zds.spanno = res;
		/* Let them know the new span number */
		//if (copy_to_user((ZT_DYNAMIC_SPAN *)data, &zds, sizeof(zds)))
		//	return -EFAULT;
		ddi_copyout(&zds, (void *)data, sizeof(zds), mode);
		return 0;
	case ZT_DYNAMIC_DESTROY:
		ddi_copyin((void *)data, &zds, sizeof(zds), mode);
		//if (copy_from_user(&zds, (ZT_DYNAMIC_SPAN *)data, sizeof(zds)))
		//	return -EFAULT;
		if (debug)
			printk("Dynamic Destroy\n");
		return destroy_dynamic(&zds);
	}

	return -ENOTTY;
}

int zt_dynamic_register(struct zt_dynamic_driver *dri)
{
	unsigned long flags;
	int res = 0;
	spin_lock_irqsave(&dlock, flags);
	if (find_driver(dri->name))
		res = -1;
	else {
		dri->next = drivers;
		drivers = dri;
	}
	spin_unlock_irqrestore(&dlock, flags);
	return res;
}

void zt_dynamic_unregister(struct zt_dynamic_driver *dri)
{
	struct zt_dynamic_driver *cur, *prev=NULL;
	struct zt_dynamic *z, *zp, *zn;
	unsigned long flags;

	spin_lock_irqsave(&dlock, flags);
	cur = drivers;
	while(cur) {
		if (cur == dri) {
			if (prev)
				prev->next = cur->next;
			else
				drivers = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	z = dspans;
	zp = NULL;
	while(z) {
		zn = z->next;
		if (z->driver == dri) {
			/* Unlink */
			if (zp)
				zp->next = z->next;
			else
				dspans = z->next;
			if (!z->usecount)
				dynamic_destroy(z);
			else
				z->dead = 1;
		} else {
			zp = z;
		}
		z = zn;
	}
	spin_unlock_irqrestore(&dlock, flags);
}

static void check_for_red_alarm(void *arg)
{
	unsigned long flags;
	int newalarm;
	int alarmchanged = 0;
	struct zt_dynamic *z;
	
	// if (debug) cmn_err(CE_CONT, "Checking for Red Alarms.\n");
	
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	while(z) {
		newalarm = z->span.alarms & ~ZT_ALARM_RED;
		/* If nothing received for a minute, consider that RED ALARM */
		if ((gethrtime() - z->rxjif) > 1000000000) {
			newalarm |= ZT_ALARM_RED;
			if (z->span.alarms != newalarm) {
				if (debug) cmn_err(CE_CONT, "Setting Red Alarm.\n");
				z->span.alarms = newalarm;
				zt_alarm_notify(&z->span);
				alarmchanged++;
			}
		}
		z = z->next;
	}
	spin_unlock_irqrestore(&dlock, flags);
	if (alarmchanged)
		checkmaster();	
}

static int ztdynamic_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int ztdynamic_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result);
static int ztdynamic_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

/* "char/block operations" OS structure */
static struct cb_ops    ztdynamic_cb_ops = {
    nulldev,                    /* open() */
    nulldev,                    /* close() */
    nodev,                      /* strategy()           */
    nodev,                      /* print routine        */
    nodev,                      /* no dump routine      */
    nodev,                      /* read() */
    nodev,                      /* write() */
    ztdynamic_ioctl,            /* generic ioctl */
    nodev,                      /* no devmap routine    */
    nodev,                      /* no mmap routine      */
    nodev,                      /* no segmap routine    */
    nochpoll,                   /* no chpoll routine    */
    ddi_prop_op,
    NULL,                       /* a STREAMS driver     */
    D_NEW | D_MP,               /* safe for multi-thread/multi-processor */
    0,                          /* cb_ops version? */
    nodev,                      /* cb_aread() */
    nodev,                      /* cb_awrite() */
};


/* "device operations" OS structure */
static struct dev_ops ztdynamic_ops = {
    DEVO_REV,                   /* devo_rev */
    0,                          /* devo_refcnt */
    ztdynamic_getinfo,          /* devo_getinfo */
    nulldev,                    /* devo_identify */
    nulldev,                    /* devo_probe */
    ztdynamic_attach,           /* devo_attach */
    ztdynamic_detach,           /* devo_detach */
    nodev,                      /* devo_reset */
    &ztdynamic_cb_ops,          /* devo_cb_ops */
    (struct bus_ops *)0,        /* devo_bus_ops */
    NULL,                       /* devo_power */
};

static  struct modldrv modldrv = {
    &mod_driverops,
    "Zaptel Dynamic Span Driver",
    &ztdynamic_ops,
};

static  struct modlinkage modlinkage = {
    MODREV_1,                   /* MODREV_1 is indicated by manual */
    { &modldrv, NULL, NULL, NULL }
};

int _init(void)
{
    int ret;

    ret = ddi_soft_state_init(&ztdynamic_statep, sizeof(struct ztdynamic_state), 1);

    if (ret)
	return ret;

    if (mod_install(&modlinkage))
    {
      cmn_err(CE_CONT, "zydynamic: _init FAILED");
      return DDI_FAILURE;
    }

	if (debug) cmn_err(CE_CONT, "ztdynamic init finished.\n");
    return DDI_SUCCESS;
}

int _info(struct modinfo *modinfop)
{
    return mod_info(&modlinkage, modinfop);
}

int _fini(void)
{
    int ret;

    if ((ret = mod_remove(&modlinkage)) == 0) {
        ddi_soft_state_fini(&ztdynamic_statep);
    }
    return ret;
}

static int ztdynamic_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd,
              void *arg, void **result)
{
  int instance;
  struct ztdynamic_state *ztd;
  int error = DDI_FAILURE;

  switch (infocmd)
  {
    case DDI_INFO_DEVT2DEVINFO:
      instance = getminor((dev_t) arg);
      ztd = ddi_get_soft_state(ztdynamic_statep, instance);
      if (ztd != NULL)
      {
        *result = ztd->dip;
        error = DDI_SUCCESS;
      } else
        *result = NULL;
      break;

    case DDI_INFO_DEVT2INSTANCE:
      instance = getminor((dev_t) arg);
      *result = (void *)(long)instance;
      break;
  }

  return error;
}

static int ztdynamic_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
    struct ztdynamic_state *ztd;
    int instance, status;
    char *getdev_name;
    cyc_time_t when;
    cyc_handler_t hdlr;

    switch (cmd) {
    case DDI_RESUME:
        cmn_err(CE_CONT, "ztdynamic: Ignoring attach_RESUME");
        return DDI_FAILURE;
    case DDI_PM_RESUME:
        cmn_err(CE_CONT, "ztdynamic: Ignoring attach_PM_RESUME");
        return DDI_FAILURE;
    case DDI_ATTACH:
        break;
    default:
        cmn_err(CE_CONT, "ztdynamic: unknown attach command %d", cmd);
        return DDI_FAILURE;
    }

    instance = ddi_get_instance(dip);

    if (ddi_soft_state_zalloc(ztdynamic_statep, instance) != DDI_SUCCESS)
    {
      cmn_err(CE_CONT, "ztdynamic%d: Failed to alloc soft state", instance);
      return DDI_FAILURE;
    }

    /* Get pointer to that memory */
    ztd = ddi_get_soft_state(ztdynamic_statep, instance);
    if (ztd == NULL) {
	    cmn_err(CE_CONT, "ztdynamic: Unable to allocate memory\n");
	    ddi_soft_state_free(ztdynamic_statep, instance);
	    return DDI_FAILURE;
    }

    ztd->dip = dip;

	/* Get our mutex configured */
	if (debug) cmn_err(CE_CONT, "Initializing mutex.\n");
	spin_lock_init(&dlock);
	
    /* Setup a high-resolution timer using an undocumented API */
    hdlr.cyh_func = check_for_red_alarm;
    hdlr.cyh_arg = 0;
    hdlr.cyh_level = CY_LOW_LEVEL;

    when.cyt_when = 0;
    when.cyt_interval = 1000000000; /* every 1s */

    mutex_enter(&cpu_lock); 
    ztd->cyclic = cyclic_add(&hdlr, &when);
    mutex_exit(&cpu_lock);

	zt_set_dynamic_ioctl(ztdynamic_ioctl);
	
    cmn_err(CE_CONT, "Zaptel Dynamic Span support LOADED\n");
    return 0;
}

static int ztdynamic_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    int instance;
    struct ztdynamic_state *ztd;

    instance = ddi_get_instance(dip);

    ztd = ddi_get_soft_state(ztdynamic_statep, instance);
    if (ztd == NULL) {
        cmn_err(CE_CONT, "ztdynamic%d: detach, failed to get soft state", instance);
        return DDI_FAILURE;
    }

	/* Try to shutdown any open lines */
	
    /* Remove high-resolution timer */
    mutex_enter(&cpu_lock);
    cyclic_remove(ztd->cyclic);
    mutex_exit(&cpu_lock);
    if (debug) {
        cmn_err(CE_CONT, "Removed timer.\n");
    }
	
	/* Remove Mutex */
	mutex_destroy(&dlock);
    if (debug) {
        cmn_err(CE_CONT, "Destroyed mutex dlock\n");
    }

    return DDI_SUCCESS;
}
