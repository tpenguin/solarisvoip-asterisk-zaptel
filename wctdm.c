/*
 * Wilcard TDM400P TDM FXS/FXO Interface Driver for Zapata Telephony interface
 *
 * Written by Mark Spencer <markster@linux-support.net>
 *            Matthew Fredrickson <creslin@linux-support.net>
 *
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * Solaris Port By Joseph Benden and others at SolarisVoip.com
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
 */

#ifdef SOLARIS
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <stddef.h>

/* Must be after other includes */
#include <sys/ddi.h>
#include <sys/sunddi.h>

#else
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#endif

#include "compat.h"

#include "proslic.h"
#include "wctdm.h"

#ifdef SOLARIS
char _depends_on[] = "drv/zaptel";

static void *wctdmstatep;
#endif

/*
 *  Define for audio vs. register based ring detection
 *  
 */
/* #define AUDIO_RINGCHECK  */

/*
  Experimental max loop current limit for the proslic
  Loop current limit is from 20 mA to 41 mA in steps of 3
  (according to datasheet)
  So set the value below to:
  0x00 : 20mA (default)
  0x01 : 23mA
  0x02 : 26mA
  0x03 : 29mA
  0x04 : 32mA
  0x05 : 35mA
  0x06 : 37mA
  0x07 : 41mA
*/
static int loopcurrent = 20;

static alpha  indirect_regs[] =
{
{0,255,"DTMF_ROW_0_PEAK",0x55C2},
{1,255,"DTMF_ROW_1_PEAK",0x51E6},
{2,255,"DTMF_ROW2_PEAK",0x4B85},
{3,255,"DTMF_ROW3_PEAK",0x4937},
{4,255,"DTMF_COL1_PEAK",0x3333},
{5,255,"DTMF_FWD_TWIST",0x0202},
{6,255,"DTMF_RVS_TWIST",0x0202},
{7,255,"DTMF_ROW_RATIO_TRES",0x0198},
{8,255,"DTMF_COL_RATIO_TRES",0x0198},
{9,255,"DTMF_ROW_2ND_ARM",0x0611},
{10,255,"DTMF_COL_2ND_ARM",0x0202},
{11,255,"DTMF_PWR_MIN_TRES",0x00E5},
{12,255,"DTMF_OT_LIM_TRES",0x0A1C},
{13,0,"OSC1_COEF",0x7B30},
{14,1,"OSC1X",0x0063},
{15,2,"OSC1Y",0x0000},
{16,3,"OSC2_COEF",0x7870},
{17,4,"OSC2X",0x007D},
{18,5,"OSC2Y",0x0000},
{19,6,"RING_V_OFF",0x0000},
{20,7,"RING_OSC",0x7EF0},
{21,8,"RING_X",0x0160},
{22,9,"RING_Y",0x0000},
{23,255,"PULSE_ENVEL",0x2000},
{24,255,"PULSE_X",0x2000},
{25,255,"PULSE_Y",0x0000},
//{26,13,"RECV_DIGITAL_GAIN",0x4000},	// playback volume set lower
{26,13,"RECV_DIGITAL_GAIN",0x2000},	// playback volume set lower
{27,14,"XMIT_DIGITAL_GAIN",0x4000},
//{27,14,"XMIT_DIGITAL_GAIN",0x2000},
{28,15,"LOOP_CLOSE_TRES",0x1000},
{29,16,"RING_TRIP_TRES",0x3600},
{30,17,"COMMON_MIN_TRES",0x1000},
{31,18,"COMMON_MAX_TRES",0x0200},
{32,19,"PWR_ALARM_Q1Q2",0x07C0},
{33,20,"PWR_ALARM_Q3Q4",0x2600},
{34,21,"PWR_ALARM_Q5Q6",0x1B80},
{35,22,"LOOP_CLOSURE_FILTER",0x8000},
{36,23,"RING_TRIP_FILTER",0x0320},
{37,24,"TERM_LP_POLE_Q1Q2",0x008C},
{38,25,"TERM_LP_POLE_Q3Q4",0x0100},
{39,26,"TERM_LP_POLE_Q5Q6",0x0010},
{40,27,"CM_BIAS_RINGING",0x0C00},
{41,64,"DCDC_MIN_V",0x0C00},
{42,255,"DCDC_XTRA",0x1000},
{43,66,"LOOP_CLOSE_TRES_LOW",0x1000},
};

static struct fxo_mode {
	char *name;
	/* FXO */
	int ohs;
	int ohs2;
	int rz;
	int rt;
	int ilim;
	int dcv;
	int mini;
	int acim;
	int ring_osc;
	int ring_x;
} fxo_modes[] =
{
	{ "FCC", 0, 0, 0, 0, 0, 0x3, 0, 0 }, 	/* US, Canada */
	{ "TBR21", 0, 0, 0, 0, 1, 0x3, 0, 0x2, 0x7e6c, 0x023a },
										/* Austria, Belgium, Denmark, Finland, France, Germany, 
										   Greece, Iceland, Ireland, Italy, Luxembourg, Netherlands,
										   Norway, Portugal, Spain, Sweden, Switzerland, and UK */
	{ "ARGENTINA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "AUSTRALIA", 1, 0, 0, 0, 0, 0, 0x3, 0x3 },
	{ "AUSTRIA", 0, 1, 0, 0, 1, 0x3, 0, 0x3 },
	{ "BAHRAIN", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "BELGIUM", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "BRAZIL", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "BULGARIA", 0, 0, 0, 0, 1, 0x3, 0x0, 0x3 },
	{ "CANADA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "CHILE", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "CHINA", 0, 0, 0, 0, 0, 0, 0x3, 0xf },
	{ "COLUMBIA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "CROATIA", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "CYRPUS", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "CZECH", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "DENMARK", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "ECUADOR", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "EGYPT", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "ELSALVADOR", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "FINLAND", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "FRANCE", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "GERMANY", 0, 1, 0, 0, 1, 0x3, 0, 0x3 },
	{ "GREECE", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "GUAM", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "HONGKONG", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "HUNGARY", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "ICELAND", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "INDIA", 0, 0, 0, 0, 0, 0x3, 0, 0x4 },
	{ "INDONESIA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "IRELAND", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "ISRAEL", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "ITALY", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "JAPAN", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "JORDAN", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "KAZAKHSTAN", 0, 0, 0, 0, 0, 0x3, 0 },
	{ "KUWAIT", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "LATVIA", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "LEBANON", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "LUXEMBOURG", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "MACAO", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "MALAYSIA", 0, 0, 0, 0, 0, 0, 0x3, 0 },	/* Current loop >= 20ma */
	{ "MALTA", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "MEXICO", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "MOROCCO", 0, 0, 0, 0, 1, 0x3, 0, 0x2 },
	{ "NETHERLANDS", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "NEWZEALAND", 0, 0, 0, 0, 0, 0x3, 0, 0x4 },
	{ "NIGERIA", 0, 0, 0, 0, 0x1, 0x3, 0, 0x2 },
	{ "NORWAY", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "OMAN", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "PAKISTAN", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "PERU", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "PHILIPPINES", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "POLAND", 0, 0, 1, 1, 0, 0x3, 0, 0 },
	{ "PORTUGAL", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "ROMANIA", 0, 0, 0, 0, 0, 3, 0, 0 },
	{ "RUSSIA", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "SAUDIARABIA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "SINGAPORE", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "SLOVAKIA", 0, 0, 0, 0, 0, 0x3, 0, 0x3 },
	{ "SLOVENIA", 0, 0, 0, 0, 0, 0x3, 0, 0x2 },
	{ "SOUTHAFRICA", 1, 0, 1, 0, 0, 0x3, 0, 0x3 },
	{ "SOUTHKOREA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "SPAIN", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "SWEDEN", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "SWITZERLAND", 0, 1, 0, 0, 1, 0x3, 0, 0x2 },
	{ "SYRIA", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "TAIWAN", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "THAILAND", 0, 0, 0, 0, 0, 0, 0x3, 0 },
	{ "UAE", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "UK", 0, 1, 0, 0, 1, 0x3, 0, 0x5 },
	{ "USA", 0, 0, 0, 0, 0, 0x3, 0, 0 },
	{ "YEMEN", 0, 0, 0, 0, 0, 0x3, 0, 0 },
};

#ifdef STANDALONE_ZAPATA
#include "zaptel.h"
#else
#ifdef SOLARIS
#include <zaptel.h>
#else
#include <linux/zaptel.h>
#endif
#endif

#define NUM_FXO_REGS 60

#define WC_MAX_IFACES 128

#define WC_CNTL    	0x00
#define WC_OPER		0x01
#define WC_AUXC    	0x02
#define WC_AUXD    	0x03
#define WC_MASK0   	0x04
#define WC_MASK1   	0x05
#define WC_INTSTAT 	0x06
#define WC_AUXR		0x07

#define WC_DMAWS	0x08
#define WC_DMAWI	0x0c
#define WC_DMAWE	0x10
#define WC_DMARS	0x18
#define WC_DMARI	0x1c
#define WC_DMARE	0x20

#define WC_AUXFUNC	0x2b
#define WC_SERCTL	0x2d
#define WC_FSCDELAY	0x2f

#define WC_REGBASE	0xc0

#define WC_SYNC		0x0
#define WC_TEST		0x1
#define WC_CS		0x2
#define WC_VER		0x3

#define BIT_CS		(1 << 2)
#define BIT_SCLK	(1 << 3)
#define BIT_SDI		(1 << 4)
#define BIT_SDO		(1 << 5)

#define FLAG_EMPTY	0
#define FLAG_WRITE	1
#define FLAG_READ	2

#define RING_DEBOUNCE	64		/* Ringer Debounce (in ms) */
#define DEFAULT_BATT_DEBOUNCE	64		/* Battery debounce (in ms) */
#define POLARITY_DEBOUNCE 64           /* Polarity debounce (in ms) */
#define DEFAULT_BATT_THRESH	3		/* Anything under this is "no battery" */

#define OHT_TIMER		6000	/* How long after RING to retain OHT */

#define FLAG_DOUBLE_CLOCK	(1 << 0)
#define FLAG_3215		(1 << 0)

#define NUM_CARDS 4

#define MAX_ALARMS 10

#define MOD_TYPE_FXS	0
#define MOD_TYPE_FXO	1

#define MINPEGTIME	10 * 8		/* 30 ms peak to peak gets us no more than 100 Hz */
#define PEGTIME		50 * 8		/* 50ms peak to peak gets us rings of 10 Hz or more */
#define PEGCOUNT	5		/* 5 cycles of pegging means RING */

#define NUM_CAL_REGS 12

struct calregs {
	unsigned char vals[NUM_CAL_REGS];
};

struct wctdm {
#ifdef SOLARIS
        dev_info_t *dev;
        ddi_acc_handle_t pci_conf_handle;
        ddi_acc_handle_t devhandle;
        ddi_dma_handle_t        dma_handle;
        ddi_acc_handle_t        dma_acc_handle;
        ddi_iblock_cookie_t     iblock;
        struct wctdm_desc *driver_data;
#else
	struct pci_dev *dev;
#endif
	char *variety;
	struct zt_span span;
	unsigned char ios;
	int usecount;
	int intcount;
	int dead;
	int pos;
	int flags[NUM_CARDS];
	int freeregion;
	int alt;
	int curcard;
	int cards;
	int cardflag;		/* Bit-map of present cards */
	spinlock_t lock;

	/* FXO Stuff */
	union {
		struct {
#ifdef AUDIO_RINGCHECK
			unsigned int pegtimer[NUM_CARDS];
			int pegcount[NUM_CARDS];
			int peg[NUM_CARDS];
			int ring[NUM_CARDS];
#else			
			int wasringing[NUM_CARDS];
#endif			
			int ringdebounce[NUM_CARDS];
			int offhook[NUM_CARDS];
			int battdebounce[NUM_CARDS];
			int nobatttimer[NUM_CARDS];
			int battery[NUM_CARDS];
		        int lastpol[NUM_CARDS];
		        int polarity[NUM_CARDS];
		        int polaritydebounce[NUM_CARDS];
		} fxo;
		struct {
			int oldrxhook[NUM_CARDS];
			int debouncehook[NUM_CARDS];
			int lastrxhook[NUM_CARDS];
			int debounce[NUM_CARDS];
			int ohttimer[NUM_CARDS];
			int idletxhookstate[NUM_CARDS];		/* IDLE changing hook state */
			int lasttxhook[NUM_CARDS];
			int palarms[NUM_CARDS];
			struct calregs calregs[NUM_CARDS];
		} fxs;
	} mod;

	/* Receive hook state and debouncing */
	int modtype[NUM_CARDS];

#ifdef SOLARIS
	caddr_t ioaddr;
#else
	unsigned long ioaddr;
#endif
	dma_addr_t 	readdma;
	dma_addr_t	writedma;
	volatile int *writechunk;					/* Double-word aligned write memory */
	volatile int *readchunk;					/* Double-word aligned read memory */
	struct zt_chan chans[NUM_CARDS];
};


struct wctdm_desc {
	char *name;
	int flags;
};

static struct wctdm_desc wctdm = { "Wildcard S400P Prototype", 0 };
static struct wctdm_desc wctdme = { "Wildcard TDM400P REV E/F", 0 };
static struct wctdm_desc wctdmh = { "Wildcard TDM400P REV H", 0 };
static struct wctdm_desc wctdmi = { "Wildcard TDM400P REV I", 0 };
static int acim2tiss[16] = { 0x0, 0x1, 0x4, 0x5, 0x7, 0x0, 0x0, 0x6, 0x0, 0x0, 0x0, 0x2, 0x0, 0x3 };

#ifdef SOLARIS
/* Data access requirements. */
static struct ddi_device_acc_attr dev_attr = {
        DDI_DEVICE_ATTR_V0,
        DDI_STRUCTURE_LE_ACC,
        DDI_STRICTORDER_ACC
};

static ddi_dma_attr_t dma_attr = {
        DMA_ATTR_V0,                            /* dma_attr_version */
        0,                                      /* dma_attr_addr_lo */
        0xffffffff,                             /* dma_attr_addr_hi */
        0x00ffffff,                             /* dma_attr_count_max */
        0x2000,                                 /* dma_attr_align */
        4,                                      /* dma_attr_burstsizes */
        4,                                      /* dma_attr_minxfer */
        0xfffffff8,                             /* dma_attr_maxxfer */
        0xffffffff,                             /* dma_attr_seg */
        1,                                      /* dma_attr_sgllen */
        4,                                      /* dma_attr_granular */
        0                                       /* dma_attr_flags */
};
#endif


static struct wctdm *ifaces[WC_MAX_IFACES];

static void wctdm_release(struct wctdm *wc);

static int battdebounce = DEFAULT_BATT_DEBOUNCE;
static int battthresh = DEFAULT_BATT_THRESH;
static int debug = 0;
static int robust = 0;
static int timingonly = 0;
static int lowpower = 0;
static int boostringer = 0;
static int _opermode = 0;
static char *opermode = "FCC";
static int fxshonormode = 0;

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast , int manual, int sane);

static unsigned char translate_3215(unsigned char address)
{
	int x;
	for (x=0;x<sizeof(indirect_regs)/sizeof(indirect_regs[0]);x++) {
		if (indirect_regs[x].address == address) {
			address = indirect_regs[x].altaddr;
			break;
		}
	}
	return address;
}

static inline void wctdm_transmitprep(struct wctdm *wc, unsigned char ints)
{
	volatile unsigned int *writechunk;
	int x;
	if (ints & 0x01) 
		/* Write is at interrupt address.  Start writing from normal offset */
		writechunk = wc->writechunk;
	else 
		writechunk = wc->writechunk + ZT_CHUNKSIZE;
	/* Calculate Transmission */
	zt_transmit(&wc->span);

	for (x=0;x<ZT_CHUNKSIZE;x++) {
		/* Send a sample, as a 32-bit word */
		writechunk[x] = 0;
                if (wc->cardflag & (1 << 3))
                        writechunk[x] |= (wc->chans[3].writechunk[x] << 24);
                if (wc->cardflag & (1 << 2))
                        writechunk[x] |= (wc->chans[2].writechunk[x] << 16);
                if (wc->cardflag & (1 << 1))
                        writechunk[x] |= (wc->chans[1].writechunk[x] << 8);
                if (wc->cardflag & (1 << 0))
                        writechunk[x] |= (wc->chans[0].writechunk[x]);
	}

}

#ifdef AUDIO_RINGCHECK
static inline void ring_check(struct wctdm *wc, int card)
{
	int x;
	short sample;
	if (wc->modtype[card] != MOD_TYPE_FXO)
		return;
	wc->mod.fxo.pegtimer[card] += ZT_CHUNKSIZE;
	for (x=0;x<ZT_CHUNKSIZE;x++) {
		/* Look for pegging to indicate ringing */
		sample = ZT_XLAW(wc->chans[card].readchunk[x], (&(wc->chans[card])));
		if ((sample > 10000) && (wc->mod.fxo.peg[card] != 1)) {
			if (debug > 1) printk("High peg!\n");
			if ((wc->mod.fxo.pegtimer[card] < PEGTIME) && (wc->mod.fxo.pegtimer[card] > MINPEGTIME))
				wc->mod.fxo.pegcount[card]++;
			wc->mod.fxo.pegtimer[card] = 0;
			wc->mod.fxo.peg[card] = 1;
		} else if ((sample < -10000) && (wc->mod.fxo.peg[card] != -1)) {
			if (debug > 1) printk("Low peg!\n");
			if ((wc->mod.fxo.pegtimer[card] < (PEGTIME >> 2)) && (wc->mod.fxo.pegtimer[card] > (MINPEGTIME >> 2)))
				wc->mod.fxo.pegcount[card]++;
			wc->mod.fxo.pegtimer[card] = 0;
			wc->mod.fxo.peg[card] = -1;
		}
	}
	if (wc->mod.fxo.pegtimer[card] > PEGTIME) {
		/* Reset pegcount if our timer expires */
		wc->mod.fxo.pegcount[card] = 0;
	}
	/* Decrement debouncer if appropriate */
	if (wc->mod.fxo.ringdebounce[card])
		wc->mod.fxo.ringdebounce[card]--;
	if (!wc->mod.fxo.offhook[card] && !wc->mod.fxo.ringdebounce[card]) {
		if (!wc->mod.fxo.ring[card] && (wc->mod.fxo.pegcount[card] > PEGCOUNT)) {
			/* It's ringing */
			if (debug)
				printk("RING on %d/%d!\n", wc->span.spanno, card + 1);
			if (!wc->mod.fxo.offhook[card])
				zt_hooksig(&wc->chans[card], ZT_RXSIG_RING);
			wc->mod.fxo.ring[card] = 1;
		}
		if (wc->mod.fxo.ring[card] && !wc->mod.fxo.pegcount[card]) {
			/* No more ring */
			if (debug)
				printk("NO RING on %d/%d!\n", wc->span.spanno, card + 1);
			zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
			wc->mod.fxo.ring[card] = 0;
		}
	}
}
#endif
static inline void wctdm_receiveprep(struct wctdm *wc, unsigned char ints)
{
	volatile unsigned int *readchunk;
	int x;

	if (ints & 0x08)
		readchunk = wc->readchunk + ZT_CHUNKSIZE;
	else
		/* Read is at interrupt address.  Valid data is available at normal offset */
		readchunk = wc->readchunk;
	for (x=0;x<ZT_CHUNKSIZE;x++) {
                if (wc->cardflag & (1 << 3))
                        wc->chans[3].readchunk[x] = (readchunk[x] >> 24) & 0xff;
                if (wc->cardflag & (1 << 2))
                        wc->chans[2].readchunk[x] = (readchunk[x] >> 16) & 0xff;
                if (wc->cardflag & (1 << 1))
                        wc->chans[1].readchunk[x] = (readchunk[x] >> 8) & 0xff;
                if (wc->cardflag & (1 << 0))
                        wc->chans[0].readchunk[x] = (readchunk[x]) & 0xff;
	}
#ifdef AUDIO_RINGCHECK
	for (x=0;x<wc->cards;x++)
		ring_check(wc, x);
#endif		
	/* XXX We're wasting 8 taps.  We should get closer :( */
	for (x=0;x<wc->cards;x++) {
		if (wc->cardflag & (1 << x))
			zt_ec_chunk(&wc->chans[x], wc->chans[x].readchunk, wc->chans[x].writechunk);
	}
	zt_receive(&wc->span);
}

static void wctdm_stop_dma(struct wctdm *wc);
static void wctdm_reset_tdm(struct wctdm *wc);
static void wctdm_restart_dma(struct wctdm *wc);

static inline void __write_8bits(struct wctdm *wc, unsigned char bits)
{
	/* Drop chip select */
	int x;
	wc->ios |= BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	wc->ios &= ~BIT_CS;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	for (x=0;x<8;x++) {
		/* Send out each bit, MSB first, drop SCLK as we do so */
		if (bits & 0x80)
			wc->ios |= BIT_SDI;
		else
			wc->ios &= ~BIT_SDI;
		wc->ios &= ~BIT_SCLK;
		outb(wc->ios, wc->ioaddr + WC_AUXD);
		/* Now raise SCLK high again and repeat */
		wc->ios |= BIT_SCLK;
		outb(wc->ios, wc->ioaddr + WC_AUXD);
		bits <<= 1;
	}
	/* Finally raise CS back high again */
	wc->ios |= BIT_CS;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	
}

static inline void __reset_spi(struct wctdm *wc)
{
	/* Drop chip select and clock once and raise and clock once */
	wc->ios |= BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	wc->ios &= ~BIT_CS;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	wc->ios |= BIT_SDI;
	wc->ios &= ~BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	/* Now raise SCLK high again and repeat */
	wc->ios |= BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	/* Finally raise CS back high again */
	wc->ios |= BIT_CS;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	/* Clock again */
	wc->ios &= ~BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	/* Now raise SCLK high again and repeat */
	wc->ios |= BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	
}

static inline unsigned char __read_8bits(struct wctdm *wc)
{
	unsigned char res=0, c;
	int x;
	wc->ios |= BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	/* Drop chip select */
	wc->ios &= ~BIT_CS;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	for (x=0;x<8;x++) {
		res <<= 1;
		/* Get SCLK */
		wc->ios &= ~BIT_SCLK;
		outb(wc->ios, wc->ioaddr + WC_AUXD);
		/* Read back the value */
		c = inb(wc->ioaddr + WC_AUXR);
		if (c & BIT_SDO)
			res |= 1;
		/* Now raise SCLK high again */
		wc->ios |= BIT_SCLK;
		outb(wc->ios, wc->ioaddr + WC_AUXD);
	}
	/* Finally raise CS back high again */
	wc->ios |= BIT_CS;
	outb(wc->ios, wc->ioaddr + WC_AUXD);
	wc->ios &= ~BIT_SCLK;
	outb(wc->ios, wc->ioaddr + WC_AUXD);

	/* And return our result */
	return res;
}

static void __wctdm_setcreg(struct wctdm *wc, unsigned char reg, unsigned char val)
{
	outb(val, wc->ioaddr + WC_REGBASE + ((reg & 0xf) << 2));
}

static unsigned char __wctdm_getcreg(struct wctdm *wc, unsigned char reg)
{
	return inb(wc->ioaddr + WC_REGBASE + ((reg & 0xf) << 2));
}

static inline void __wctdm_setcard(struct wctdm *wc, int card)
{
	if (wc->curcard != card) {
		__wctdm_setcreg(wc, WC_CS, (1 << card));
		wc->curcard = card;
	}
}

static void __wctdm_setreg(struct wctdm *wc, int card, unsigned char reg, unsigned char value)
{
	__wctdm_setcard(wc, card);
	if (wc->modtype[card] == MOD_TYPE_FXO) {
		__write_8bits(wc, 0x20);
		__write_8bits(wc, reg & 0x7f);
	} else {
		__write_8bits(wc, reg & 0x7f);
	}
	__write_8bits(wc, value);
}

static void wctdm_setreg(struct wctdm *wc, int card, unsigned char reg, unsigned char value)
{
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	__wctdm_setreg(wc, card, reg, value);
	spin_unlock_irqrestore(&wc->lock, flags);
}

static unsigned char __wctdm_getreg(struct wctdm *wc, int card, unsigned char reg)
{
	__wctdm_setcard(wc, card);
	if (wc->modtype[card] == MOD_TYPE_FXO) {
		__write_8bits(wc, 0x60);
		__write_8bits(wc, reg & 0x7f);
	} else {
		__write_8bits(wc, reg | 0x80);
	}
	return __read_8bits(wc);
}

static inline void reset_spi(struct wctdm *wc, int card)
{
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	__wctdm_setcard(wc, card);
	__reset_spi(wc);
	__reset_spi(wc);
	spin_unlock_irqrestore(&wc->lock, flags);
}

static unsigned char wctdm_getreg(struct wctdm *wc, int card, unsigned char reg)
{
	unsigned long flags;
	unsigned char res;
	spin_lock_irqsave(&wc->lock, flags);
	res = __wctdm_getreg(wc, card, reg);
	spin_unlock_irqrestore(&wc->lock, flags);
	return res;
}

static int __wait_access(struct wctdm *wc, int card)
{
    unsigned char data;
    long origjiffies;
    int count = 0;

    #define MAX 6000 /* attempts */


    /* origjiffies = jiffies; */
    /* Wait for indirect access */
    while (count++ < MAX)
	 {
		data = __wctdm_getreg(wc, card, I_STATUS);

		if (!data)
			return 0;

	 }

    if(count > (MAX-1)) printk(" ##### Loop error (%02x) #####\n", data);

	return 0;
}

static int wctdm_proslic_setreg_indirect(struct wctdm *wc, int card, unsigned char address, unsigned short data)
{
	unsigned long flags;
	int res = -1;
	/* translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	spin_lock_irqsave(&wc->lock, flags);
	if(!__wait_access(wc, card)) {
		__wctdm_setreg(wc, card, IDA_LO,(unsigned char)(data & 0xFF));
		__wctdm_setreg(wc, card, IDA_HI,(unsigned char)((data & 0xFF00)>>8));
		__wctdm_setreg(wc, card, IAA,address);
		res = 0;
	};
	spin_unlock_irqrestore(&wc->lock, flags);
	return res;
}

static int wctdm_proslic_getreg_indirect(struct wctdm *wc, int card, unsigned char address)
{ 
	unsigned long flags;
	int res = -1;
	char *p=NULL;
	/* translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	spin_lock_irqsave(&wc->lock, flags);
	if (!__wait_access(wc, card)) {
		__wctdm_setreg(wc, card, IAA, address);
		if (!__wait_access(wc, card)) {
			unsigned char data1, data2;
			data1 = __wctdm_getreg(wc, card, IDA_LO);
			data2 = __wctdm_getreg(wc, card, IDA_HI);
			res = data1 | (data2 << 8);
		} else
			p = "Failed to wait inside\n";
	} else
		p = "failed to wait\n";
	spin_unlock_irqrestore(&wc->lock, flags);
	if (p)
		printk(p);
	return res;
}

static int wctdm_proslic_init_indirect_regs(struct wctdm *wc, int card)
{
	unsigned char i;

	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++)
	{
		if(wctdm_proslic_setreg_indirect(wc, card, indirect_regs[i].address,indirect_regs[i].initial))
			return -1;
	}

	return 0;
}

static int wctdm_proslic_verify_indirect_regs(struct wctdm *wc, int card)
{ 
	int passed = 1;
	unsigned short i, initial;
	int j;

	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++) 
	{
		if((j = wctdm_proslic_getreg_indirect(wc, card, (unsigned char) indirect_regs[i].address)) < 0) {
			printk("Failed to read indirect register %d\n", i);
			return -1;
		}
		initial= indirect_regs[i].initial;

		if ( j != initial && (!(wc->flags[card] & FLAG_3215) || (indirect_regs[i].altaddr != 255)) )
		{
			 printk("!!!!!!! %s  iREG %X = %X  should be %X\n",
				indirect_regs[i].name,indirect_regs[i].address,j,initial );
			 passed = 0;
		}	
	}

    if (passed) {
		if (debug)
			printk("Init Indirect Registers completed successfully.\n");
    } else {
		printk(" !!!!! Init Indirect Registers UNSUCCESSFULLY.\n");
		return -1;
    }
    return 0;
}

static inline void wctdm_proslic_recheck_sanity(struct wctdm *wc, int card)
{
	int res;
	/* Check loopback */
	res = wctdm_getreg(wc, card, 8);
	if (res) {
		printk("Ouch, part reset, quickly restoring reality (%d)\n", card);
		wctdm_init_proslic(wc, card, 1, 0, 1);
	} else {
		res = wctdm_getreg(wc, card, 64);
		if (!res && (res != wc->mod.fxs.lasttxhook[card])) {
			if (wc->mod.fxs.palarms[card]++ < MAX_ALARMS) {
				printk("Power alarm on module %d, resetting!\n", card + 1);
				if (wc->mod.fxs.lasttxhook[card] == 4)
					wc->mod.fxs.lasttxhook[card] = 1;
				wctdm_setreg(wc, card, 64, wc->mod.fxs.lasttxhook[card]);
			} else {
				if (wc->mod.fxs.palarms[card] == MAX_ALARMS)
					printk("Too many power alarms on card %d, NOT resetting!\n", card + 1);
			}
		}
	}
}

static inline void wctdm_voicedaa_check_hook(struct wctdm *wc, int card)
{
#ifndef AUDIO_RINGCHECK
	unsigned char res;
#endif	
	signed char b;
	int poopy = 0;
	/* Try to track issues that plague slot one FXO's */
	b = wctdm_getreg(wc, card, 5);
	if ((b & 0x2) || !(b & 0x8)) {
		/* Not good -- don't look at anything else */
		if (debug)
			printk("Poopy (%02x) on card %d!\n", b, card + 1); 
		poopy++;
	}
	b &= 0x9b;
	if (wc->mod.fxo.offhook[card]) {
		if (b != 0x9)
			wctdm_setreg(wc, card, 5, 0x9);
	} else {
		if (b != 0x8)
			wctdm_setreg(wc, card, 5, 0x8);
	}
	if (poopy)
		return;
#ifndef AUDIO_RINGCHECK
	if (!wc->mod.fxo.offhook[card]) {
		res = wctdm_getreg(wc, card, 5);
		if ((res & 0x60) && wc->mod.fxo.battery[card]) {
			wc->mod.fxo.ringdebounce[card] += (ZT_CHUNKSIZE * 4);
			if (wc->mod.fxo.ringdebounce[card] >= ZT_CHUNKSIZE * 64) {
				if (!wc->mod.fxo.wasringing[card]) {
					wc->mod.fxo.wasringing[card] = 1;
					zt_hooksig(&wc->chans[card], ZT_RXSIG_RING);
					if (debug)
						printk("RING on %d/%d!\n", wc->span.spanno, card + 1);
				}
				wc->mod.fxo.ringdebounce[card] = ZT_CHUNKSIZE * 64;
			}
		} else {
			wc->mod.fxo.ringdebounce[card] -= ZT_CHUNKSIZE;
			if (wc->mod.fxo.ringdebounce[card] <= 0) {
				if (wc->mod.fxo.wasringing[card]) {
					wc->mod.fxo.wasringing[card] =0;
					zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
					if (debug)
						printk("NO RING on %d/%d!\n", wc->span.spanno, card + 1);
				}
				wc->mod.fxo.ringdebounce[card] = 0;
			}
				
		}
	}
#endif
	b = wctdm_getreg(wc, card, 29);
#if 0 
	{
		static int count = 0;
		if (!(count++ % 100)) {
			printk("Card %d: Voltage: %d  Debounce %d\n", card + 1, 
			       b, wc->mod.fxo.battdebounce[card]);
		}
	}
#endif	
	if (abs(b) < battthresh) {
		wc->mod.fxo.nobatttimer[card]++;
#if 0
		if (wc->mod.fxo.battery[card])
			printk("Battery loss: %d (%d debounce)\n", b, wc->mod.fxo.battdebounce[card]);
#endif
		if (wc->mod.fxo.battery[card] && !wc->mod.fxo.battdebounce[card]) {
			if (debug)
				printk("NO BATTERY on %d/%d!\n", wc->span.spanno, card + 1);
			wc->mod.fxo.battery[card] =  0;
#ifdef	JAPAN
			if ((!wc->ohdebounce) && wc->offhook) {
				zt_hooksig(&wc->chans[card], ZT_RXSIG_ONHOOK);
				if (debug)
					printk("Signalled On Hook\n");
#ifdef	ZERO_BATT_RING
				wc->onhook++;
#endif
			}
#else
			zt_hooksig(&wc->chans[card], ZT_RXSIG_ONHOOK);
#endif
			wc->mod.fxo.battdebounce[card] = battdebounce;
		} else if (!wc->mod.fxo.battery[card])
			wc->mod.fxo.battdebounce[card] = battdebounce;
	} else if (abs(b) > battthresh) {
		if (!wc->mod.fxo.battery[card] && !wc->mod.fxo.battdebounce[card]) {
			if (debug)
				printk("BATTERY on %d/%d (%s)!\n", wc->span.spanno, card + 1, 
					(b < 0) ? "-" : "+");			    
#ifdef	ZERO_BATT_RING
			if (wc->onhook) {
				wc->onhook = 0;
				zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
				if (debug)
					printk("Signalled Off Hook\n");
			}
#else
			zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
#endif
			wc->mod.fxo.battery[card] = 1;
			wc->mod.fxo.nobatttimer[card] = 0;
			wc->mod.fxo.battdebounce[card] = battdebounce;
		} else if (wc->mod.fxo.battery[card])
			wc->mod.fxo.battdebounce[card] = battdebounce;

		if (wc->mod.fxo.lastpol[card] >= 0) {
		    if (b < 0) {
			wc->mod.fxo.lastpol[card] = -1;
			wc->mod.fxo.polaritydebounce[card] = POLARITY_DEBOUNCE;
		    }
		} 
		if (wc->mod.fxo.lastpol[card] <= 0) {
		    if (b > 0) {
			wc->mod.fxo.lastpol[card] = 1;
			wc->mod.fxo.polaritydebounce[card] = POLARITY_DEBOUNCE;
		    }
		}
	} else {
		/* It's something else... */
		wc->mod.fxo.battdebounce[card] = battdebounce;
	}
	if (wc->mod.fxo.battdebounce[card])
		wc->mod.fxo.battdebounce[card]--;
	if (wc->mod.fxo.polaritydebounce[card]) {
	        wc->mod.fxo.polaritydebounce[card]--;
		if (wc->mod.fxo.polaritydebounce[card] < 1) {
		    if (wc->mod.fxo.lastpol[card] != wc->mod.fxo.polarity[card]) {
			if (debug)
				printk("Polarity reversed (%d -> %d)\n",
			       wc->mod.fxo.polarity[card], 
			       wc->mod.fxo.lastpol[card]);
			if (wc->mod.fxo.polarity[card])
			    zt_qevent_lock(&wc->chans[card], ZT_EVENT_POLARITY);
			wc->mod.fxo.polarity[card] = wc->mod.fxo.lastpol[card];
		    }
		}
	}
}

static inline void wctdm_proslic_check_hook(struct wctdm *wc, int card)
{
	char res;
	int hook;

	/* For some reason we have to debounce the
	   hook detector.  */

	res = wctdm_getreg(wc, card, 68);
	hook = (res & 1);
	if (hook != wc->mod.fxs.lastrxhook[card]) {
		/* Reset the debounce (must be multiple of 4ms) */
		wc->mod.fxs.debounce[card] = 8 * (4 * 8);
#if 0
		printk("Resetting debounce card %d hook %d, %d\n", card, hook, wc->mod.fxs.debounce[card]);
#endif
	} else {
		if (wc->mod.fxs.debounce[card] > 0) {
			wc->mod.fxs.debounce[card]-= 4 * ZT_CHUNKSIZE;
#if 0
			printk("Sustaining hook %d, %d\n", hook, wc->mod.fxs.debounce[card]);
#endif
			if (!wc->mod.fxs.debounce[card]) {
#if 0
				printk("Counted down debounce, newhook: %d...\n", hook);
#endif
				wc->mod.fxs.debouncehook[card] = hook;
			}
			if (!wc->mod.fxs.oldrxhook[card] && wc->mod.fxs.debouncehook[card]) {
				/* Off hook */
#if 1
				if (debug)
#endif				
					printk("wctdm: Card %d Going off hook\n", card);
				zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
				if (robust)
					wctdm_init_proslic(wc, card, 1, 0, 1);
				wc->mod.fxs.oldrxhook[card] = 1;
			
			} else if (wc->mod.fxs.oldrxhook[card] && !wc->mod.fxs.debouncehook[card]) {
				/* On hook */
#if 1
				if (debug)
#endif				
					printk("wctdm: Card %d Going on hook\n", card);
				zt_hooksig(&wc->chans[card], ZT_RXSIG_ONHOOK);
				wc->mod.fxs.oldrxhook[card] = 0;
			}
		}
	}
	wc->mod.fxs.lastrxhook[card] = hook;
}

static uint_t wctdm_interrupt(caddr_t dev_id)
{
	struct wctdm *wc = (struct wctdm *)dev_id;
	unsigned char ints;
	int x;

	ints = inb(wc->ioaddr + WC_INTSTAT);
	outb(ints, wc->ioaddr + WC_INTSTAT);

	if (!ints) {
		cmn_err(CE_CONT, "random interrupt caught - we have no work to do.\n");
		return DDI_INTR_UNCLAIMED;
	}


	if (ints & 0x10) {
		/* Stop DMA, wait for watchdog */
		printk("TDM PCI Master abort\n");
		wctdm_stop_dma(wc);
		return DDI_INTR_CLAIMED;
	}
	
	if (ints & 0x20) {
		printk("PCI Target abort\n");
		return DDI_INTR_CLAIMED;
	}

	for (x=0;x<4;x++) {
		if ((x < wc->cards) && (wc->cardflag & (1 << x)) &&
			(wc->modtype[x] == MOD_TYPE_FXS)) {
			if (wc->mod.fxs.lasttxhook[x] == 0x4) {
				/* RINGing, prepare for OHT */
				wc->mod.fxs.ohttimer[x] = OHT_TIMER << 3;
				wc->mod.fxs.idletxhookstate[x] = 0x2;	/* OHT mode when idle */
			} else {
				if (wc->mod.fxs.ohttimer[x]) {
					wc->mod.fxs.ohttimer[x]-= ZT_CHUNKSIZE;
					if (!wc->mod.fxs.ohttimer[x]) {
						wc->mod.fxs.idletxhookstate[x] = 0x1;	/* Switch to active */
						if (wc->mod.fxs.lasttxhook[x] == 0x2) {
							/* Apply the change if appropriate */
							wc->mod.fxs.lasttxhook[x] = 0x1;
							wctdm_setreg(wc, x, 64, wc->mod.fxs.lasttxhook[x]);
						}
					}
				}
			}
		}
	}

	if (ints & 0x0f) {
		wc->intcount++;
		x = wc->intcount % 4;
		if ((x < wc->cards) && (wc->cardflag & (1 << x))) {
			if (wc->modtype[x] == MOD_TYPE_FXS) {
				wctdm_proslic_check_hook(wc, x);
				if (!(wc->intcount & 0xfc))
					wctdm_proslic_recheck_sanity(wc, x);
			} else if (wc->modtype[x] == MOD_TYPE_FXO) {
				wctdm_voicedaa_check_hook(wc, x);
			}
		}
		if (!(wc->intcount % 10000)) {
			/* Accept an alarm once per 10 seconds */
			for (x=0;x<4;x++) 
				if (wc->modtype[x] == MOD_TYPE_FXS) {
					if (wc->mod.fxs.palarms[x])
						wc->mod.fxs.palarms[x]--;
				}
		}
		wctdm_receiveprep(wc, ints);
		wctdm_transmitprep(wc, ints);
	}
	return DDI_INTR_CLAIMED;
}

static int wctdm_voicedaa_insane(struct wctdm *wc, int card)
{
	int blah;
	blah = wctdm_getreg(wc, card, 2);
	if (blah != 0x3)
		return -2;
	blah = wctdm_getreg(wc, card, 11);
	if (debug)
		printk("VoiceDAA System: %02x\n", blah & 0xf);
	return 0;
}

static int wctdm_proslic_insane(struct wctdm *wc, int card)
{
	int blah,insane_report;
	insane_report=0;

	blah = wctdm_getreg(wc, card, 0);
	if (debug) 
		printk("ProSLIC on module %d, product %d, version %d\n", card, (blah & 0x30) >> 4, (blah & 0xf));

#if	0
	if ((blah & 0x30) >> 4) {
		printk("ProSLIC on module %d is not a 3210.\n", card);
		return -1;
	}
#endif
	if (((blah & 0xf) == 0) || ((blah & 0xf) == 0xf)) {
		/* SLIC not loaded */
		return -1;
	}
	if ((blah & 0xf) < 2) {
		printk("ProSLIC 3210 version %d is too old\n", blah & 0xf);
		return -1;
	}

	if ((blah & 0xf) == 2) {
		/* ProSLIC 3215, not a 3210 */
		wc->flags[card] |= FLAG_3215;
	}

	blah = wctdm_getreg(wc, card, 8);
	if (blah != 0x2) {
		printk("ProSLIC on module %d insane (1) %d should be 2\n", card, blah);
		return -1;
	} else if ( insane_report)
		printk("ProSLIC on module %d Reg 8 Reads %d Expected is 0x2\n",card,blah);

	blah = wctdm_getreg(wc, card, 64);
	if (blah != 0x0) {
		printk("ProSLIC on module %d insane (2)\n", card);
		return -1;
	} else if ( insane_report)
		printk("ProSLIC on module %d Reg 64 Reads %d Expected is 0x0\n",card,blah);

	blah = wctdm_getreg(wc, card, 11);
	if (blah != 0x33) {
		printk("ProSLIC on module %d insane (3)\n", card);
		return -1;
	} else if ( insane_report)
		printk("ProSLIC on module %d Reg 11 Reads %d Expected is 0x33\n",card,blah);

	/* Just be sure it's setup right. */
	wctdm_setreg(wc, card, 30, 0);

	if (debug) 
		printk("ProSLIC on module %d seems sane.\n", card);
	return 0;
}

static int wctdm_proslic_powerleak_test(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	unsigned char vbat;

	/* Turn off linefeed */
	wctdm_setreg(wc, card, 64, 0);

	/* Power down */
	wctdm_setreg(wc, card, 14, 0x10);

	/* Wait for one second */
	origjiffies = jiffies;

	while((vbat = wctdm_getreg(wc, card, 82)) > 0x6) {
		if ((jiffies - origjiffies) >= (HZ/2))
			break;;
	}

	if (vbat < 0x06) {
		printk("Excessive leakage detected on module %d: %d volts (%02x) after %d ms\n", card,
		       376 * vbat / 1000, vbat, (int)((jiffies - origjiffies) * 1000 / HZ));
		return -1;
	} else if (debug) {
		printk("Post-leakage voltage: %d volts\n", 376 * vbat / 1000);
	}
	return 0;
}

static int wctdm_powerup_proslic(struct wctdm *wc, int card, int fast)
{
	unsigned char vbat;
	unsigned long origjiffies;
	int lim;

	/* Set period of DC-DC converter to 1/64 khz */
	wctdm_setreg(wc, card, 92, 0xff /* was 0xff */);

	/* Wait for VBat to powerup */
	origjiffies = jiffies;

	/* Disable powerdown */
	wctdm_setreg(wc, card, 14, 0);

	/* If fast, don't bother checking anymore */
	if (fast)
		return 0;

	int retries = 0;
	while((vbat = wctdm_getreg(wc, card, 82)) < 0xc0) {
		/* Wait no more than 500ms */
		if (retries++ > 5) {
			break;
		}
		drv_usecwait(100000);
	}

	if (vbat < 0xc0) {
		printk("ProSLIC on module %d failed to powerup within %d ms (%d mV only)\n\n -- DID YOU REMEMBER TO PLUG IN THE HD POWER CABLE TO THE TDM400P??\n",
		       card, (int)(((jiffies - origjiffies) * 1000 / HZ)),
			vbat * 375);
		return -1;
	} else if (debug) {
		printk("ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
	}

        /* Proslic max allowed loop current, reg 71 LOOP_I_LIMIT */
        /* If out of range, just set it to the default value     */
        lim = (loopcurrent - 20) / 3;
        if ( loopcurrent > 41 ) {
                lim = 0;
                if (debug)
                        printk("Loop current out of range! Setting to default 20mA!\n");
        }
        else if (debug)
                        printk("Loop current set to %dmA!\n",(lim*3)+20);
        wctdm_setreg(wc,card,LOOP_I_LIMIT,lim);

	/* Engage DC-DC converter */
	wctdm_setreg(wc, card, 93, 0x19 /* was 0x19 */);
#if 0
	origjiffies = jiffies;
	while(0x80 & wctdm_getreg(wc, card, 93)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk("Timeout waiting for DC-DC calibration on module %d\n", card);
			return -1;
		}
	}

#if 0
	/* Wait a full two seconds */
	while((jiffies - origjiffies) < 2 * HZ);

	/* Just check to be sure */
	vbat = wctdm_getreg(wc, card, 82);
	printk("ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
#endif
#endif
	return 0;

}

static int wctdm_proslic_manual_calibrate(struct wctdm *wc, int card){
	unsigned long origjiffies;
	unsigned char i;

	wctdm_setreg(wc, card, 21, 0);//(0)  Disable all interupts in DR21
	wctdm_setreg(wc, card, 22, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 23, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 64, 0);//(0)

	wctdm_setreg(wc, card, 97, 0x18); //(0x18)Calibrations without the ADC and DAC offset and without common mode calibration.
	wctdm_setreg(wc, card, 96, 0x47); //(0x47)	Calibrate common mode and differential DAC mode DAC + ILIM

	origjiffies=jiffies;
	while( wctdm_getreg(wc,card,96)!=0 ){
		if((jiffies-origjiffies)>80)
			return -1;
	}
//Initialized DR 98 and 99 to get consistant results.
// 98 and 99 are the results registers and the search should have same intial conditions.

/*******************************The following is the manual gain mismatch calibration****************************/
/*******************************This is also available as a function *******************************************/
	// Delay 10ms
	origjiffies=jiffies; 
	while((jiffies-origjiffies)<1);
	wctdm_proslic_setreg_indirect(wc, card, 88,0);
	wctdm_proslic_setreg_indirect(wc,card,89,0);
	wctdm_proslic_setreg_indirect(wc,card,90,0);
	wctdm_proslic_setreg_indirect(wc,card,91,0);
	wctdm_proslic_setreg_indirect(wc,card,92,0);
	wctdm_proslic_setreg_indirect(wc,card,93,0);

	wctdm_setreg(wc, card, 98,0x10); // This is necessary if the calibration occurs other than at reset time
	wctdm_setreg(wc, card, 99,0x10);

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 98,i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc,card,88)) == 0)
			break;
	} // for

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 99,i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc,card,89)) == 0)
			break;
	}//for

/*******************************The preceding is the manual gain mismatch calibration****************************/
/**********************************The following is the longitudinal Balance Cal***********************************/
	wctdm_setreg(wc,card,64,1);
	while((jiffies-origjiffies)<10); // Sleep 100?

	wctdm_setreg(wc, card, 64, 0);
	wctdm_setreg(wc, card, 23, 0x4);  // enable interrupt for the balance Cal
	wctdm_setreg(wc, card, 97, 0x1); // this is a singular calibration bit for longitudinal calibration
	wctdm_setreg(wc, card, 96,0x40);

	wctdm_getreg(wc,card,96); /* Read Reg 96 just cause */

	wctdm_setreg(wc, card, 21, 0xFF);
	wctdm_setreg(wc, card, 22, 0xFF);
	wctdm_setreg(wc, card, 23, 0xFF);

	/**The preceding is the longitudinal Balance Cal***/
	return(0);

}
#if 1
static int wctdm_proslic_calibrate(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	int x;
	/* Perform all calibrations */
	wctdm_setreg(wc, card, 97, 0x1f);
	
	/* Begin, no speedup */
	wctdm_setreg(wc, card, 96, 0x5f);

	/* Wait for it to finish */
	int retries = 0;
	while(wctdm_getreg(wc, card, 96)) {
		if (retries++ > 10) {
			printk("Timeout waiting for calibration of module %d\n", card);
			return -1;
		}
		drv_usecwait(100000);
	}
	
	if (debug) {
		/* Print calibration parameters */
		printk("Calibration Vector Regs 98 - 107: \n");
		for (x=98;x<108;x++) {
			printk("%d: %02x\n", x, wctdm_getreg(wc, card, x));
		}
	}
	return 0;
}
#endif

static int wctdm_init_voicedaa(struct wctdm *wc, int card, int fast, int manual, int sane)
{
	unsigned char reg16=0, reg26=0, reg30=0, reg31=0;
	long newjiffies;
	wc->modtype[card] = MOD_TYPE_FXO;
	/* Sanity check the ProSLIC */
	reset_spi(wc, card);
	if (!sane && wctdm_voicedaa_insane(wc, card))
		return -2;

	/* Software reset */
	wctdm_setreg(wc, card, 1, 0x80);

	/* Wait just a bit */
	drv_usecwait(10000);

	/* Enable PCM, ulaw */
	wctdm_setreg(wc, card, 33, 0x28);

	/* Set On-hook speed, Ringer impedence, and ringer threshold */
	reg16 |= (fxo_modes[_opermode].ohs << 6);
	reg16 |= (fxo_modes[_opermode].rz << 1);
	reg16 |= (fxo_modes[_opermode].rt);
	wctdm_setreg(wc, card, 16, reg16);
	
	/* Set DC Termination:
	   Tip/Ring voltage adjust, minimum operational current, current limitation */
	reg26 |= (fxo_modes[_opermode].dcv << 6);
	reg26 |= (fxo_modes[_opermode].mini << 4);
	reg26 |= (fxo_modes[_opermode].ilim << 1);
	wctdm_setreg(wc, card, 26, reg26);

	/* Set AC Impedence */
	reg30 = (fxo_modes[_opermode].acim);
	wctdm_setreg(wc, card, 30, reg30);

	/* Misc. DAA parameters */
	reg31 = 0xa3;
	reg31 |= (fxo_modes[_opermode].ohs2 << 3);
	wctdm_setreg(wc, card, 31, reg31);

	/* Set Transmit/Receive timeslot */
	wctdm_setreg(wc, card, 34, (3-card) * 8);
	wctdm_setreg(wc, card, 35, 0x00);
	wctdm_setreg(wc, card, 36, (3-card) * 8);
	wctdm_setreg(wc, card, 37, 0x00);

	/* Enable ISO-Cap */
	wctdm_setreg(wc, card, 6, 0x00);

	/* Wait 1000ms for ISO-cap to come up */
	int retries = 10;
	while((retries > 0) && !(wctdm_getreg(wc, card, 11) & 0xf0)) {
		--retries;
		drv_usecwait(100000);
	}

	if (!(wctdm_getreg(wc, card, 11) & 0xf0)) {
		printk("VoiceDAA did not bring up ISO link properly!\n");
		return -1;
	}
	if (debug)
		printk("ISO-Cap is now up, line side: %02x rev %02x\n", 
		       wctdm_getreg(wc, card, 11) >> 4,
		       (wctdm_getreg(wc, card, 13) >> 2) & 0xf);
	/* Enable on-hook line monitor */
	wctdm_setreg(wc, card, 5, 0x08);
	/* wc->mod.fxo.offhook[card] = 1; */
	return 0;
		
}

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast, int manual, int sane)
{

	unsigned short tmp[5];
	unsigned char r19;
	int x;
	int fxsmode=0;

	/* By default, don't send on hook */
	wc->mod.fxs.idletxhookstate [card] = 1;

	/* Sanity check the ProSLIC */
	if (!sane && wctdm_proslic_insane(wc, card))
		return -2;
		
	if (sane) {
		/* Make sure we turn off the DC->DC converter to prevent anything from blowing up */
		wctdm_setreg(wc, card, 14, 0x10);
	}

	if (wctdm_proslic_init_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed to initialize on module %d.\n", card);
		return -1;
	}

	/* Clear scratch pad area */
	wctdm_proslic_setreg_indirect(wc, card, 97,0);

	/* Clear digital loopback */
	wctdm_setreg(wc, card, 8, 0);

	/* Revision C optimization */
	wctdm_setreg(wc, card, 108, 0xeb);

	/* Disable automatic VBat switching for safety to prevent
	   Q7 from accidently turning on and burning out. */
	wctdm_setreg(wc, card, 67, 0x17);

	/* Turn off Q7 */
	wctdm_setreg(wc, card, 66, 1);

	/* Flush ProSLIC digital filters by setting to clear, while
	   saving old values */
	for (x=0;x<5;x++) {
		tmp[x] = wctdm_proslic_getreg_indirect(wc, card, x + 35);
		wctdm_proslic_setreg_indirect(wc, card, x + 35, 0x8000);
	}

	/* Power up the DC-DC converter */
	if (wctdm_powerup_proslic(wc, card, fast)) {
		printk("Unable to do INITIAL ProSLIC powerup on module %d\n", card);
		return -1;
	}

	if (!fast) {

		/* Check for power leaks */
		if (wctdm_proslic_powerleak_test(wc, card)) {
			printk("ProSLIC module %d failed leakage test.  Check for short circuit\n", card);
		}
		/* Power up again */
		if (wctdm_powerup_proslic(wc, card, fast)) {
			printk("Unable to do FINAL ProSLIC powerup on module %d\n", card);
			return -1;
		}
#ifndef NO_CALIBRATION
		/* Perform calibration */
		if(manual) {
			if (wctdm_proslic_manual_calibrate(wc, card)) {
				//printk("Proslic failed on Manual Calibration\n");
				if (wctdm_proslic_manual_calibrate(wc, card)) {
					printk("Proslic Failed on Second Attempt to Calibrate Manually. (Try -DNO_CALIBRATION in Makefile)\n");
					return -1;
				}
				printk("Proslic Passed Manual Calibration on Second Attempt\n");
			}
		}
		else {
			if(wctdm_proslic_calibrate(wc, card))  {
				//printk("ProSlic died on Auto Calibration.\n");
				if (wctdm_proslic_calibrate(wc, card)) {
					printk("Proslic Failed on Second Attempt to Auto Calibrate\n");
					return -1;
				}
				printk("Proslic Passed Auto Calibration on Second Attempt\n");
			}
		}
		/* Perform DC-DC calibration */
		wctdm_setreg(wc, card, 93, 0x99);
		r19 = wctdm_getreg(wc, card, 107);
		if ((r19 < 0x2) || (r19 > 0xd)) {
			printk("DC-DC cal has a surprising direct 107 of 0x%02x!\n", r19);
			wctdm_setreg(wc, card, 107, 0x8);
		}

		/* Save calibration vectors */
		for (x=0;x<NUM_CAL_REGS;x++)
			wc->mod.fxs.calregs[card].vals[x] = wctdm_getreg(wc, card, 96 + x);
#endif

	} else {
		/* Restore calibration registers */
		for (x=0;x<NUM_CAL_REGS;x++)
			wctdm_setreg(wc, card, 96 + x, wc->mod.fxs.calregs[card].vals[x]);
	}
	/* Calibration complete, restore original values */
	for (x=0;x<5;x++) {
		wctdm_proslic_setreg_indirect(wc, card, x + 35, tmp[x]);
	}

	if (wctdm_proslic_verify_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed verification.\n");
		return -1;
	}


#if 0
    /* Disable Auto Power Alarm Detect and other "features" */
    wctdm_setreg(wc, card, 67, 0x0e);
    blah = wctdm_getreg(wc, card, 67);
#endif

#if 0
    if (wctdm_proslic_setreg_indirect(wc, card, 97, 0x0)) { // Stanley: for the bad recording fix
		 printk(KERN_INFO "ProSlic IndirectReg Died.\n");
		 return -1;
	}
#endif

    wctdm_setreg(wc, card, 1, 0x28);
 	// U-Law 8-bit interface
    wctdm_setreg(wc, card, 2, (3-card) * 8);    // Tx Start count low byte  0
    wctdm_setreg(wc, card, 3, 0);    // Tx Start count high byte 0
    wctdm_setreg(wc, card, 4, (3-card) * 8);    // Rx Start count low byte  0
    wctdm_setreg(wc, card, 5, 0);    // Rx Start count high byte 0
    wctdm_setreg(wc, card, 18, 0xff);     // clear all interrupt
    wctdm_setreg(wc, card, 19, 0xff);
    wctdm_setreg(wc, card, 20, 0xff);
    wctdm_setreg(wc, card, 73, 0x04);
	if (fxshonormode) {
		fxsmode = acim2tiss[fxo_modes[_opermode].acim];
		wctdm_setreg(wc, card, 10, 0x08 | fxsmode);
		if (fxo_modes[_opermode].ring_osc)
			wctdm_proslic_setreg_indirect(wc, card, 20, fxo_modes[_opermode].ring_osc);
		if (fxo_modes[_opermode].ring_x)
			wctdm_proslic_setreg_indirect(wc, card, 21, fxo_modes[_opermode].ring_x);
	}
    if (lowpower)
    	wctdm_setreg(wc, card, 72, 0x10);

#if 0
    wctdm_setreg(wc, card, 21, 0x00); 	// enable interrupt
    wctdm_setreg(wc, card, 22, 0x02); 	// Loop detection interrupt
    wctdm_setreg(wc, card, 23, 0x01); 	// DTMF detection interrupt
#endif

#if 0
    /* Enable loopback */
    wctdm_setreg(wc, card, 8, 0x2);
    wctdm_setreg(wc, card, 14, 0x0);
    wctdm_setreg(wc, card, 64, 0x0);
    wctdm_setreg(wc, card, 1, 0x08);
#endif

	/* Beef up Ringing voltage to 89V */
	if (boostringer) {
		if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x1d1)) 
			return -1;
		printk("Boosting ringinger on slot %d (89V peak)\n", card + 1);
	} else if (lowpower) {
		if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x108)) 
			return -1;
		printk("Reducing ring power on slot %d (50V peak)\n", card + 1);
	}
	return 0;
}


static int wctdm_ioctl(struct zt_chan *chan, unsigned int cmd, unsigned long data, int mode)
{
	struct wctdm_stats stats;
	struct wctdm_regs regs;
	struct wctdm_regop regop;
	struct wctdm *wc = chan->pvt;
	int x;
	switch (cmd) {
	case ZT_ONHOOKTRANSFER:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (int *)data))
			return -EFAULT;
		wc->mod.fxs.ohttimer[chan->chanpos - 1] = x << 3;
		wc->mod.fxs.idletxhookstate[chan->chanpos - 1] = 0x2;	/* OHT mode when idle */
		if (wc->mod.fxs.lasttxhook[chan->chanpos - 1] == 0x1) {
				/* Apply the change if appropriate */
				wc->mod.fxs.lasttxhook[chan->chanpos - 1] = 0x2;
				wctdm_setreg(wc, chan->chanpos - 1, 64, wc->mod.fxs.lasttxhook[chan->chanpos - 1]);
		}
		break;
	case WCTDM_GET_STATS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			stats.tipvolt = wctdm_getreg(wc, chan->chanpos - 1, 80) * -376;
			stats.ringvolt = wctdm_getreg(wc, chan->chanpos - 1, 81) * -376;
			stats.batvolt = wctdm_getreg(wc, chan->chanpos - 1, 82) * -376;
		} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			stats.tipvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.ringvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.batvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
		} else 
			return -EINVAL;
		if (copy_to_user((struct wctdm_stats *)data, &stats, sizeof(stats)))
			return -EFAULT;
		break;
	case WCTDM_GET_REGS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			for (x=0;x<NUM_INDIRECT_REGS;x++)
				regs.indirect[x] = wctdm_proslic_getreg_indirect(wc, chan->chanpos -1, x);
			for (x=0;x<NUM_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		} else {
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<NUM_FXO_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		}
		if (copy_to_user((struct wctdm_regs *)data, &regs, sizeof(regs)))
			return -EFAULT;
		break;
	case WCTDM_SET_REG:
		if (copy_from_user(&regop, (struct wctdm_regop *)data, sizeof(regop)))
			return -EFAULT;
		if (regop.indirect) {
			if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
				return -EINVAL;
			printk("Setting indirect %d to 0x%04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_proslic_setreg_indirect(wc, chan->chanpos - 1, regop.reg, regop.val);
		} else {
			regop.val &= 0xff;
			printk("Setting direct %d to %04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_setreg(wc, chan->chanpos - 1, regop.reg, regop.val);
		}
		break;
	default:
		return -ENOTTY;
	}
	return 0;

}

static int wctdm_open(struct zt_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	if (!(wc->cardflag & (1 << (chan->chanpos - 1))))
		return -ENODEV;
	if (wc->dead)
		return -ENODEV;
	wc->usecount++;

	return 0;
}

static int wctdm_watchdog(struct zt_span *span, int event)
{
	printk("TDM: Restarting DMA\n");
	wctdm_restart_dma(span->pvt);
	return 0;
}

static int wctdm_close(struct zt_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	int x;
	wc->usecount--;

	for (x=0;x<wc->cards;x++)
		wc->mod.fxs.idletxhookstate[x] = 1;
	/* If we're dead, release us now */
	if (!wc->usecount && wc->dead) 
		wctdm_release(wc);
	return 0;
}

static int wctdm_hooksig(struct zt_chan *chan, zt_txsig_t txsig)
{
	struct wctdm *wc = chan->pvt;
	int reg=0;
	if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
		/* XXX Enable hooksig for FXO XXX */
		switch(txsig) {
		case ZT_TXSIG_START:
		case ZT_TXSIG_OFFHOOK:
			wc->mod.fxo.offhook[chan->chanpos - 1] = 1;
			wctdm_setreg(wc, chan->chanpos - 1, 5, 0x9);
			break;
		case ZT_TXSIG_ONHOOK:
			wc->mod.fxo.offhook[chan->chanpos - 1] = 0;
			wctdm_setreg(wc, chan->chanpos - 1, 5, 0x8);
			break;
		default:
			printk("wcfxo: Can't set tx state to %d\n", txsig);
		}
	} else {
		switch(txsig) {
		case ZT_TXSIG_ONHOOK:
			switch(chan->sig) {
			case ZT_SIG_EM:
			case ZT_SIG_FXOKS:
			case ZT_SIG_FXOLS:
				wc->mod.fxs.lasttxhook[chan->chanpos-1] = wc->mod.fxs.idletxhookstate[chan->chanpos-1];
				break;
			case ZT_SIG_FXOGS:
				wc->mod.fxs.lasttxhook[chan->chanpos-1] = 3;
				break;
			}
			break;
		case ZT_TXSIG_OFFHOOK:
			switch(chan->sig) {
			case ZT_SIG_EM:
				wc->mod.fxs.lasttxhook[chan->chanpos-1] = 5;
				break;
			default:
				wc->mod.fxs.lasttxhook[chan->chanpos-1] = wc->mod.fxs.idletxhookstate[chan->chanpos-1];
				break;
			}
			break;
		case ZT_TXSIG_START:
			wc->mod.fxs.lasttxhook[chan->chanpos-1] = 4;
			break;
		case ZT_TXSIG_KEWL:
			wc->mod.fxs.lasttxhook[chan->chanpos-1] = 0;
			break;
		default:
			printk("wctdm: Can't set tx state to %d\n", txsig);
		}
		if (debug)
			printk("Setting FXS hook state to %d (%02x)\n", txsig, reg);

#if 1
		wctdm_setreg(wc, chan->chanpos - 1, 64, wc->mod.fxs.lasttxhook[chan->chanpos-1]);
#endif
	}
	return 0;
}

static int wctdm_initialize(struct wctdm *wc)
{
	int x;

	/* Zapata stuff */
	sprintf(wc->span.name, "WCTDM/%d", wc->pos);
	sprintf(wc->span.desc, "%s Board %d", wc->variety, wc->pos + 1);
	wc->span.deflaw = ZT_LAW_MULAW;
	for (x=0;x<wc->cards;x++) {
		sprintf(wc->chans[x].name, "WCTDM/%d/%d", wc->pos, x);
		wc->chans[x].sigcap = ZT_SIG_FXOKS | ZT_SIG_FXOLS | ZT_SIG_FXOGS | ZT_SIG_SF | ZT_SIG_EM | ZT_SIG_CLEAR;
		wc->chans[x].sigcap |= ZT_SIG_FXSKS | ZT_SIG_FXSLS | ZT_SIG_SF | ZT_SIG_CLEAR;
		wc->chans[x].chanpos = x+1;
		wc->chans[x].pvt = wc;
	}
	wc->span.chans = wc->chans;
	wc->span.channels = wc->cards;
	wc->span.hooksig = wctdm_hooksig;
	wc->span.open = wctdm_open;
	wc->span.close = wctdm_close;
	wc->span.flags = ZT_FLAG_RBS;
	wc->span.ioctl = wctdm_ioctl;
	wc->span.watchdog = wctdm_watchdog;
	init_waitqueue_head(&wc->span.maintq);

	wc->span.pvt = wc;
	if (zt_register(&wc->span, 0)) {
		printk("Unable to register span with zaptel\n");
		return -1;
	}
	return 0;
}

static void wctdm_post_initialize(struct wctdm *wc)
{
	int x;
	/* Finalize signalling  */
	for (x=0;x<wc->cards;x++) {
		if (wc->cardflag & (1 << x)) {
			if (wc->modtype[x] == MOD_TYPE_FXO)
				wc->chans[x].sigcap = ZT_SIG_FXSKS | ZT_SIG_FXSLS | ZT_SIG_SF | ZT_SIG_CLEAR;
			else
				wc->chans[x].sigcap = ZT_SIG_FXOKS | ZT_SIG_FXOLS | ZT_SIG_FXOGS | ZT_SIG_SF | ZT_SIG_EM | ZT_SIG_CLEAR;
		}
	}
}

static int wctdm_hardware_init(struct wctdm *wc)
{
	/* Hardware stuff */
	unsigned char ver;
	unsigned char x,y;
	int failed;

	/* Signal Reset */
	outb(0x01, wc->ioaddr + WC_CNTL);

	/* Check Freshmaker chip */
	x=inb(wc->ioaddr + WC_CNTL);
	ver = __wctdm_getcreg(wc, WC_VER);
	failed = 0;
	if (ver != 0x59) {
		printk("Freshmaker version: %02x\n", ver);
		for (x=0;x<255;x++) {
			/* Test registers */
			if (ver >= 0x70) {
				__wctdm_setcreg(wc, WC_CS, x);
				y = __wctdm_getcreg(wc, WC_CS);
			} else {
				__wctdm_setcreg(wc, WC_TEST, x);
				y = __wctdm_getcreg(wc, WC_TEST);
			}
			if (x != y) {
				printk("%02x != %02x\n", x, y);
				failed++;
			}
		}
		if (!failed) {
			printk("Freshmaker passed register test\n");
		} else {
			printk("Freshmaker failed register test\n");
			return -1;
		}
		/* Go to half-duty FSYNC */
		__wctdm_setcreg(wc, WC_SYNC, 0x01);
		y = __wctdm_getcreg(wc, WC_SYNC);
	} else {
		printk("No freshmaker chip\n");
	}

	/* Reset PCI Interface chip and registers (and serial) */
	outb(0x06, wc->ioaddr + WC_CNTL);
	/* Setup our proper outputs for when we switch for our "serial" port */
	wc->ios = BIT_CS | BIT_SCLK | BIT_SDI;

	outb(wc->ios, wc->ioaddr + WC_AUXD);

	/* Set all to outputs except AUX 5, which is an input */
	outb(0xdf, wc->ioaddr + WC_AUXC);

	/* Select alternate function for AUX0 */
	outb(0x4, wc->ioaddr + WC_AUXFUNC);
	
	/* Wait 1/4 of a sec */
	drv_usecwait(250000);

	/* Back to normal, with automatic DMA wrap around */
	outb(0x30 | 0x01, wc->ioaddr + WC_CNTL);
	
	/* Make sure serial port and DMA are out of reset */
	outb(inb(wc->ioaddr + WC_CNTL) & 0xf9, wc->ioaddr + WC_CNTL);
	
	/* Configure serial port for MSB->LSB operation */
	outb(0xc1, wc->ioaddr + WC_SERCTL);

	/* Delay FSC by 0 so it's properly aligned */
	outb(0x0, wc->ioaddr + WC_FSCDELAY);

	/* Setup DMA Addresses */
	outl(wc->writedma,                    wc->ioaddr + WC_DMAWS);		/* Write start */
	outl(wc->writedma + ZT_CHUNKSIZE * 4 - 4, wc->ioaddr + WC_DMAWI);		/* Middle (interrupt) */
	outl(wc->writedma + ZT_CHUNKSIZE * 8 - 4, wc->ioaddr + WC_DMAWE);			/* End */
	
	outl(wc->readdma,                    	 wc->ioaddr + WC_DMARS);	/* Read start */
	outl(wc->readdma + ZT_CHUNKSIZE * 4 - 4, 	 wc->ioaddr + WC_DMARI);	/* Middle (interrupt) */
	outl(wc->readdma + ZT_CHUNKSIZE * 8 - 4, wc->ioaddr + WC_DMARE);	/* End */
	
	/* Clear interrupts */
	outb(0xff, wc->ioaddr + WC_INTSTAT);

	/* Wait 1/4 of a second more */
	drv_usecwait(250000);

	for (x=0;x<wc->cards;x++) {
		int sane=0,ret=0,readi=0;
#if 1
		/* Init with Auto Calibration */
		if (!(ret=wctdm_init_proslic(wc, x, 0, 0, sane))) {
			wc->cardflag |= (1 << x);
                        if (debug) {
                                readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
                                printk("Proslic module %d loop current is %dmA\n",x,
                                ((readi*3)+20));
                        }
			printk("Module %d: Installed -- AUTO FXS/DPO\n",x);
		} else {
			if (ret!=-2) {
				sane=1;
				/* Init with Manual Calibration */
				if (!wctdm_init_proslic(wc, x, 0, 1, sane)) {
					wc->cardflag |= (1 << x);
                                if (debug) {
                                        readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
                                        printk("Proslic module %d loop current is %dmA\n",x,
                                        ((readi*3)+20));
                                }
					printk("Module %d: Installed -- MANUAL FXS\n",x);
				} else {
					printk("Module %d: FAILED FXS (%s)\n", x, fxshonormode ? fxo_modes[_opermode].name : "FCC");
				} 
			} else if (!(ret = wctdm_init_voicedaa(wc, x, 0, 0, sane))) {
				wc->cardflag |= (1 << x);
				printk("Module %d: Installed -- AUTO FXO (%s mode)\n",x, fxo_modes[_opermode].name);
			} else
				printk("Module %d: Not installed\n", x);
		}
#endif
	}

	/* Return error if nothing initialized okay. */
	if (!wc->cardflag && !timingonly)
		return -1;
	__wctdm_setcreg(wc, WC_SYNC, (wc->cardflag << 1) | 0x1);
	return 0;
}

static void wctdm_enable_interrupts(struct wctdm *wc)
{
	/* Enable interrupts (we care about all of them) */
	outb(0x3f, wc->ioaddr + WC_MASK0);
	/* No external interrupts */
	outb(0x00, wc->ioaddr + WC_MASK1);
}

static void wctdm_restart_dma(struct wctdm *wc)
{
	/* Reset Master and TDM */
	outb(0x01, wc->ioaddr + WC_CNTL);
	outb(0x01, wc->ioaddr + WC_OPER);
}

static void wctdm_start_dma(struct wctdm *wc)
{
	/* Reset Master and TDM */
	outb(0x0f, wc->ioaddr + WC_CNTL);
	drv_usecwait(10000);
	outb(0x01, wc->ioaddr + WC_CNTL);
	outb(0x01, wc->ioaddr + WC_OPER);
}

static void wctdm_stop_dma(struct wctdm *wc)
{
	outb(0x00, wc->ioaddr + WC_OPER);
}

static void wctdm_reset_tdm(struct wctdm *wc)
{
	/* Reset TDM */
	outb(0x0f, wc->ioaddr + WC_CNTL);
}

static void wctdm_disable_interrupts(struct wctdm *wc)	
{
	outb(0x00, wc->ioaddr + WC_MASK0);
	outb(0x00, wc->ioaddr + WC_MASK1);
}

static int wctdm_init_one(struct wctdm *wc)
{
	int res;
	struct wctdm_desc *d = (struct wctdm_desc *)wc->driver_data;
	size_t rlen;
	ddi_dma_cookie_t cookie;
	unsigned int count;
	unsigned short comm;
	int x;
	int size;
	static int initd_ifaces=0;
	
	if(initd_ifaces){
		memset((void *)ifaces,0,(sizeof(struct wctdm *))*WC_MAX_IFACES);
		initd_ifaces=1;
	}
	for (x=0;x<WC_MAX_IFACES;x++)
		if (!ifaces[x]) break;
	if (x >= WC_MAX_IFACES) {
		printk("Too many interfaces\n");
		return -EIO;
	}
	
	if (wc) {
		ifaces[x] = wc;
		// spin_lock_init(&wc->lock);
		wc->curcard = -1;
		wc->cards = 4;

		if (debug) cmn_err(CE_CONT, "ddi_regs_map_setup\n");
		if (ddi_regs_map_setup(wc->dev, 1, &wc->ioaddr, 0, 255,
			&dev_attr, &wc->devhandle) != DDI_SUCCESS) {
				cmn_err(CE_CONT, "ddi_regs_map_setup failed.\n");
				return DDI_FAILURE;
		}

		wc->pos = x;
		wc->variety = d->name;
		wc->flags[x] = d->flags;

		/* Allocate enough memory for two zt chunks, receive and transmit.  Each sample uses
		   32 bits.  Allocate an extra set just for control too */
		size = ZT_MAX_CHUNKSIZE * 2 * 2 * 2 * 4;

		/* Get a handle for doing DMA stuff */
		if (ddi_dma_alloc_handle(wc->dev,
			&dma_attr,
			DDI_DMA_SLEEP,
			NULL,
			&wc->dma_handle) != DDI_SUCCESS) {
				cmn_err(CE_CONT, "ddi_dma_alloc_handle failed.\n");
				return (DDI_FAILURE);
		}

		if (debug) cmn_err(CE_CONT, "ddi_dma_alloc_handle\n");
		
		/* Allocate a block of DMA'able memory */
		if (ddi_dma_mem_alloc(wc->dma_handle,
			(uint_t)size,
			&dev_attr,
			DDI_DMA_CONSISTENT,
			DDI_DMA_SLEEP,
			NULL,
			(caddr_t *)&wc->writechunk,      /* ptr to space */
			&rlen,                           /* real length  */
			&wc->dma_acc_handle) != DDI_SUCCESS) {
				cmn_err(CE_CONT, "ddi_dma_mem_alloc failed.\n");   
				ddi_dma_free_handle(&wc->dma_handle);
				return (DDI_FAILURE);
		}
		if (debug) cmn_err(CE_CONT, "ddi_dma_mem_alloc\n");
		
		/* Now get a cookie (ptr) to pass to card */
		if (ddi_dma_addr_bind_handle(wc->dma_handle,
			(struct as *)0,
			(caddr_t)wc->writechunk,
			(u_int)rlen,
			DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
			DDI_DMA_SLEEP,
			0,
			&cookie,
			&count) != DDI_SUCCESS) {
			cmn_err(CE_CONT, "ddi_dma_addr_bind_handle failed.\n");
				(void) ddi_dma_mem_free(&wc->dma_acc_handle);
				ddi_dma_free_handle(&wc->dma_handle);
				return (DDI_FAILURE);
		}
		if (debug) cmn_err(CE_CONT, "ddi_dma_addr_bind_handle\n");
		
		wc->writedma = cookie.dmac_address;

		wc->readchunk = wc->writechunk + ZT_MAX_CHUNKSIZE * 2;	/* in doublewords */
		wc->readdma = wc->writedma + ZT_MAX_CHUNKSIZE * 8;		/* in bytes */

		if (debug) cmn_err(CE_CONT, "wctdm_initialize\n");
		if (wctdm_initialize(wc)) {
			printk("wctdm: Unable to intialize FXS\n");
			/* Set Reset Low */
			x=inb(wc->ioaddr + WC_CNTL);
			outb((~0x1)&x, wc->ioaddr + WC_CNTL);
			/* Free Resources */
			(void) ddi_dma_mem_free(&wc->dma_acc_handle);
							ddi_dma_free_handle(&wc->dma_handle);
							return DDI_FAILURE;
		}
		if (debug) cmn_err(CE_CONT, "wctdm_initialized OK\n");
		
		/* Enable bus mastering */
		comm = pci_config_get16(wc->pci_conf_handle, PCI_CONF_COMM);
		comm |= PCI_COMM_ME;
		comm &= ~PCI_COMM_PARITY_DETECT;
		pci_config_put16(wc->pci_conf_handle, PCI_CONF_COMM, comm);

		if (debug) cmn_err(CE_CONT, "checking for High Level interrupt\n");
		if (ddi_intr_hilevel(wc->dev, 0) != 0) {
			cmn_err(CE_CONT, "High Level interrupt is not supported.\n");
		}
		
		int resultp;
		ddi_dev_nintrs(wc->dev, &resultp);
		if (debug) cmn_err(CE_CONT, "The number of support interrupts is %d\n", resultp);
		
		if (debug) cmn_err(CE_CONT, "retrieve interrupt block cookie.\n");
		resultp = ddi_get_iblock_cookie(wc->dev, 0, &wc->iblock);
		if (resultp != DDI_SUCCESS) {
			cmn_err(CE_CONT, "ddi_get_iblock_cookie failed.\n");
							(void) ddi_dma_mem_free(&wc->dma_acc_handle);
							ddi_dma_free_handle(&wc->dma_handle);
			zt_unregister(&wc->span);
			return DDI_FAILURE;
		}
		if (debug) cmn_err(CE_CONT, "Got iblock_cookie\n");
		
		spin_lock_init(&wc->lock);
		if (debug) cmn_err(CE_CONT, "Mutex Initialized\n");
		
		if (ddi_add_intr(wc->dev, 0, 0, 0, 
				wctdm_interrupt, (caddr_t)wc) != DDI_SUCCESS) {
							cmn_err(CE_CONT, "wctdm: Unable to request IRQ\n");
							(void) ddi_dma_mem_free(&wc->dma_acc_handle);
							ddi_dma_free_handle(&wc->dma_handle);
							return DDI_FAILURE;
					}
		if (debug) cmn_err(CE_CONT, "Interrupt Handler Installed.\n");
		
		if (wctdm_hardware_init(wc)) {
			unsigned char x;
			/* Set Reset Low */
			x=inb(wc->ioaddr + WC_CNTL);
			outb((~0x1)&x, wc->ioaddr + WC_CNTL);
							ddi_remove_intr(wc->dev, 0, wc->iblock);
							(void) ddi_dma_mem_free(&wc->dma_acc_handle);
							ddi_dma_free_handle(&wc->dma_handle);
			zt_unregister(&wc->span);
			return -EIO;

		}
		if (debug) cmn_err(CE_CONT, "wctdm_hardware_init OK\n");
		
		wctdm_post_initialize(wc);
		if (debug) cmn_err(CE_CONT, "wctdm_post_initialize OK\n");
		
		/* Enable interrupts */
		wctdm_enable_interrupts(wc);
		if (debug) cmn_err(CE_CONT, "wctdm_enable_interrupts OK\n");
		
		/* Initialize Write/Buffers to all blank data */
		memset((void *)wc->writechunk,0,ZT_MAX_CHUNKSIZE * 2 * 2 * 4);

		/* Start DMA */
		wctdm_start_dma(wc);
		if (debug) cmn_err(CE_CONT, "wctdm_start_dma OK\n");

		printk("Found a Wildcard TDM: %s (%d modules)\n", wc->variety, wc->cards);
		res = 0;
	}
	if (debug) cmn_err(CE_CONT, "leaving init_one\n");
	return res;
}

static void wctdm_release(struct wctdm *wc)
{
	zt_unregister(&wc->span);
        (void) ddi_dma_mem_free(&wc->dma_acc_handle);
        ddi_dma_free_handle(&wc->dma_handle);
	printk("Freed a Wildcard\n");
}

static void wctdm_remove_one(struct wctdm *wc)
{
	if (wc) {
		/* Stop any DMA */
		if (debug) cmn_err(CE_CONT, "Disable DMA\n");
		wctdm_stop_dma(wc);
		if (debug) cmn_err(CE_CONT, "Reset TDM\n");
		wctdm_reset_tdm(wc);

		/* In case hardware is still there */
		if (debug) cmn_err(CE_CONT, "Disable hardware interrupts\n");
		wctdm_disable_interrupts(wc);
		
		if (debug) cmn_err(CE_CONT, "Disable interrupt\n");
                ddi_remove_intr(wc->dev, 0, wc->iblock);

		/* Reset PCI chip and registers */
		if (debug) cmn_err(CE_CONT, "Reset PCI chip and registers\n");
		outb(0x0e, wc->ioaddr + WC_CNTL);

		/* Release span, possibly delayed */
		if (!wc->usecount)
			wctdm_release(wc);
		else
			wc->dead = 1;
	}
}

static struct pci_device_id wctdm_pci_tbl[] = {
	{ 0xe159, 0x0001, 0xa159, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdm },
	{ 0xe159, 0x0001, 0xe159, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdm },
	{ 0xe159, 0x0001, 0xb100, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdme },
	{ 0xe159, 0x0001, 0xa9fd, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
	{ 0xe159, 0x0001, 0xa8fd, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
	{ 0xe159, 0x0001, 0xa800, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
	{ 0xe159, 0x0001, 0xa801, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
	{ 0xe159, 0x0001, 0xa908, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
	{ 0xe159, 0x0001, 0xa901, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
#ifdef TDM_REVH_MATCHALL
	{ 0xe159, 0x0001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (kernel_ulong_t) &wctdmh },
#endif
	{ 0 }
};

static int wctdm_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
                    void **result);
static int wctdm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int wctdm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int wctdm_property_update(dev_info_t *dip);

/* "char/block operations" OS structure */
static struct cb_ops    wctdm_cb_ops = {
    nulldev,                    /* open() */
    nulldev,                    /* close() */
    nodev,                      /* strategy()           */
    nodev,                      /* print routine        */
    nodev,                      /* no dump routine      */
    nodev,                      /* read() */
    nodev,                      /* write() */
    nodev,                      /* generic ioctl */
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
static struct dev_ops wctdm_ops = {
    DEVO_REV,                   /* devo_rev */
    0,                          /* devo_refcnt */
    wctdm_getinfo,              /* devo_getinfo */
    nulldev,                    /* devo_identify */
    nulldev,                    /* devo_probe */
    wctdm_attach,               /* devo_attach */
    wctdm_detach,               /* devo_detach */
    nodev,                      /* devo_reset */
    &wctdm_cb_ops,              /* devo_cb_ops */
    (struct bus_ops *)0,        /* devo_bus_ops */
    NULL,                       /* devo_power */
};

static  struct modldrv modldrv = {
    &mod_driverops,
    "Wildcard TDM400P Zaptel Driver",
    &wctdm_ops,
};

static  struct modlinkage modlinkage = {
    MODREV_1,                   /* MODREV_1 is indicated by manual */
    { &modldrv, NULL, NULL, NULL }
};

int _init(void)
{
	int ret;
	
	ret = ddi_soft_state_init(&wctdmstatep, sizeof(struct wctdm), WC_MAX_IFACES);
	if (ret != 0)
	{
		return ret;
	}
	
	if ((ret = mod_install(&modlinkage)) != 0) {
		cmn_err(CE_CONT, "wctdm: _init FAILED");
		return DDI_FAILURE;
	}
	
	if (debug) cmn_err(CE_CONT, "wctdm: _init SUCCESS");
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
        ddi_soft_state_fini(&wctdmstatep);
    }
    if (debug) cmn_err(CE_CONT, "wctdm: _fini %s", ret == DDI_SUCCESS ? "SUCCESS" : "FAILURE");
    return ret;
}

static int wctdm_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd,
              void *arg, void **result)
{
	int instance;
	struct wctdm *state;
	int error = DDI_FAILURE;
	
	switch (infocmd) {
		case DDI_INFO_DEVT2DEVINFO:
			instance = getminor((dev_t) arg);
			state = ddi_get_soft_state(wctdmstatep, instance);
			if (state != NULL)
			{
				*result = state->dev;
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

static int wctdm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct wctdm *state;
	int instance, status;
	char *getdev_name;
		
	switch (cmd) {
		case DDI_RESUME:
			cmn_err(CE_CONT, "wctdm: Ignoring attach_RESUME");
			return DDI_FAILURE;
		case DDI_PM_RESUME:
			cmn_err(CE_CONT, "wctdm: Ignoring attach_PM_RESUME");
			return DDI_FAILURE;
		case DDI_ATTACH:
			break;
		default:
			cmn_err(CE_CONT, "wctdm: unknown attach command %d", cmd);
			return DDI_FAILURE;
	}

	instance = ddi_get_instance(dip);
	cmn_err(CE_CONT, "wctdm: attach for instance %d", instance);

	/* Allocate some memory for this instance */
	if (ddi_soft_state_zalloc(wctdmstatep, instance) != DDI_SUCCESS)
	{
		cmn_err(CE_CONT, "wctdm%d: Failed to alloc soft state", instance);
		return DDI_FAILURE;
	}

	/* Get pointer to that memory */
	state = ddi_get_soft_state(wctdmstatep, instance);
	if (state == NULL)
	{
		cmn_err(CE_CONT, "wctdm%d: attach, failed to get soft state", instance);
		ddi_soft_state_free(wctdmstatep, instance);
		return DDI_FAILURE;
	}

	if (wctdm_property_update(dip) != DDI_SUCCESS)
		return DDI_FAILURE;
	
	state->dev = dip;
	if (pci_config_setup(state->dev, &state->pci_conf_handle) != DDI_SUCCESS) {
		cmn_err(CE_CONT, "wctdm%d: attach, failed to run pci_config_setup", instance);
		ddi_soft_state_free(wctdmstatep, instance);
		return DDI_FAILURE;
	}

	/*
	debug = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"debug-level", 0);
	robust = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"robust", 0);
	timingonly = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"timingonly", 0);
	lowpower = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"lowpower", 0);
	boostringer = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"boostringer", 0);
		
	char *om = NULL;
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
			"opermode", &om) == DDI_PROP_SUCCESS) {
		if (om) {
			int x;
			for (x=0; x<(sizeof(fxo_modes) / sizeof(fxo_modes[0])); x++) {
				if (!strcmp(fxo_modes[x].name, om))
					break;
			}
			if (x < sizeof(fxo_modes) / sizeof(fxo_modes[0]))
				_opermode = x;
			else
				cmn_err(CE_CONT, "Invalid/unknown operating mode.\n");
			ddi_prop_free(om);
		}
	}
	*/
	
	ushort vendor_id = pci_config_get16(state->pci_conf_handle, 0x00);
	ushort device_id = pci_config_get16(state->pci_conf_handle, 0x02);
	ushort subsystem_vendor_id = pci_config_get16(state->pci_conf_handle, 0x2c);
	ushort subsystem_id = pci_config_get16(state->pci_conf_handle, 0x2e);
  
	/* determine which card is installed */
	if (vendor_id == 0xe159 && device_id == 0x1) {
		switch (subsystem_vendor_id) {
			case 0xa159:
			case 0xe159:
				state->driver_data = &wctdm;
				break;
			case 0xb100:
				state->driver_data = &wctdme;
				break;
			case 0xa9fd:
			case 0xa8fd:
			case 0xa800:
			case 0xa801:
			case 0xa908:
			case 0xa901:
				state->driver_data = &wctdmh;
				break;
			case 0xb1d9:
			case 0xb119:
				state->driver_data = &wctdmi;
				break;
			default:
				cmn_err(CE_CONT, "Subsystem vendor id not recognized.\n");
				state->driver_data = &wctdm;
		}
	} else {
		cmn_err(CE_CONT, "Unknown card found\n");
		state->driver_data = &wctdm;
	}
  
	if (debug) 
		cmn_err(CE_CONT, "vendor=%x dev=%x svendor=%x ssid=%x\n", vendor_id, device_id,
			subsystem_vendor_id, subsystem_id);
	
	if (wctdm_init_one(state) != DDI_SUCCESS)
	{
		cmn_err(CE_CONT, "wctdm%d: Failed to initialise device", instance);
		ddi_soft_state_free(wctdmstatep, instance);
		return DDI_FAILURE;
	}

	if (debug) cmn_err(CE_CONT, "wctdm: attach successful!");
	return DDI_SUCCESS;
}

static int wctdm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    int instance;
    struct wctdm *state;

    instance = ddi_get_instance(dip);
    if (debug) cmn_err(CE_CONT, "wctdm%d: detach", instance);

    state = ddi_get_soft_state(wctdmstatep, instance);
    if (state == NULL) {
        cmn_err(CE_CONT, "wctdm%d: detach, failed to get soft state", instance);
        return DDI_FAILURE;
    }

    wctdm_remove_one(state);

    ddi_regs_map_free(&state->devhandle);
    
    pci_config_teardown(&state->pci_conf_handle);
    
    ddi_soft_state_free(wctdmstatep, instance);

    return DDI_SUCCESS;
}

static int wctdm_property_update(dev_info_t *dip)
{
	if (ddi_prop_update_int(DDI_DEV_T_ANY, dip, "ddi-no-autodetach", 1) == -1) {
		cmn_err(CE_WARN, "!updating ddi-no-autodetach failed");
		return DDI_FAILURE;
	}
	return DDI_SUCCESS;
}

