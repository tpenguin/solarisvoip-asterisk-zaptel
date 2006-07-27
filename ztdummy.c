/*
 * Dummy Zaptel Driver for Zapata Telephony interface
 *
 * Required: usb-uhci module and kernel > 2.4.4 OR kernel > 2.6.0
 *
 * Written by Robert Pleh <robert.pleh@hermes.si>
 * 2.6 version by Tony Hoyle
 * Unified by Mark Spencer <markster@digium.com>
 * Solaris version by simon@slimey.org
 * Solaris patches by joe@thrallingpenguin.com
 *
 * Copyright (C) 2002, Hermes Softlab
 * Copyright (C) 2004, Digium, Inc.
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

#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <stddef.h>

/* Must be after other includes */
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cyclic.h>

#include "zconfig.h"
#include "zaptel.h"

static void *ztdummy_statep;

char _depends_on[] = "drv/zaptel";

struct ztdummy_state {
	dev_info_t *dip;
	timeout_id_t timerid;
	cyclic_id_t cyclic;
	struct zt_span span;
	struct zt_chan chan;
};

static int debug = 0;

static void ztdummy_timer(void *arg)
{
    struct ztdummy_state *ztd;

    ztd = ddi_get_soft_state(ztdummy_statep, 0);
    zt_receive(&ztd->span);
    zt_transmit(&ztd->span);
}

static int ztdummy_initialize(struct ztdummy_state *ztd)
{
	/* Zapata stuff */
	sprintf(ztd->span.name, "ZTDUMMY/1");
	sprintf(ztd->span.desc, "%s %d", ztd->span.name, 1);
	sprintf(ztd->chan.name, "ZTDUMMY/%d/%d", 1, 0);
	ztd->chan.chanpos = 1;
	ztd->span.chans = &ztd->chan;
	ztd->span.channels = 0;		/* no channels on our span */
	ztd->span.deflaw = ZT_LAW_MULAW;
	cv_init(&ztd->span.maintq, NULL, CV_DRIVER, NULL);
	ztd->span.pvt = ztd;
	ztd->chan.pvt = ztd;
	if (zt_register(&ztd->span, 0)) {
		return -1;
	}
	return 0;
}

static int ztdummy_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int ztdummy_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result);
static int ztdummy_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

/* "char/block operations" OS structure */
static struct cb_ops    ztdummy_cb_ops = {
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
static struct dev_ops ztdummy_ops = {
    DEVO_REV,                   /* devo_rev */
    0,                          /* devo_refcnt */
    ztdummy_getinfo,            /* devo_getinfo */
    nulldev,                    /* devo_identify */
    nulldev,                    /* devo_probe */
    ztdummy_attach,             /* devo_attach */
    ztdummy_detach,             /* devo_detach */
    nodev,                      /* devo_reset */
    &ztdummy_cb_ops,            /* devo_cb_ops */
    (struct bus_ops *)0,        /* devo_bus_ops */
    NULL,                       /* devo_power */
};

static  struct modldrv modldrv = {
    &mod_driverops,
    "Dummy Zaptel Driver",
    &ztdummy_ops,
};

static  struct modlinkage modlinkage = {
    MODREV_1,                   /* MODREV_1 is indicated by manual */
    { &modldrv, NULL, NULL, NULL }
};

static int global_sticky = 0;

int _init(void)
{
    int ret;

    ret = ddi_soft_state_init(&ztdummy_statep, sizeof(struct ztdummy_state), 1);

    if (ret)
	return ret;

    if (mod_install(&modlinkage))
    {
      cmn_err(CE_CONT, "zydummy: _init FAILED");
      return DDI_FAILURE;
    }

    return DDI_SUCCESS;
}

int _info(struct modinfo *modinfop)
{
    return mod_info(&modlinkage, modinfop);
}

int _fini(void)
{
    int ret;

	if (global_sticky == 0) return EBUSY;
	
    /*
     * If mod_remove() is successful, we destroy our global mutex
     */
    if ((ret = mod_remove(&modlinkage)) == 0) {
        ddi_soft_state_fini(&ztdummy_statep);
    }
    return ret;
}

static int ztdummy_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd,
              void *arg, void **result)
{
  int instance;
  struct ztdummy_state *ztd;
  int error = DDI_FAILURE;

  switch (infocmd)
  {
    case DDI_INFO_DEVT2DEVINFO:
      instance = getminor((dev_t) arg);
      ztd = ddi_get_soft_state(ztdummy_statep, instance);
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

static int ztdummy_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
    struct ztdummy_state *ztd;
    int instance, status;
    char *getdev_name;
    cyc_time_t when;
    cyc_handler_t hdlr;

    switch (cmd) {
    case DDI_RESUME:
        cmn_err(CE_CONT, "ztdummy: Ignoring attach_RESUME");
        return DDI_FAILURE;
    case DDI_PM_RESUME:
        cmn_err(CE_CONT, "ztdummy: Ignoring attach_PM_RESUME");
        return DDI_FAILURE;
    case DDI_ATTACH:
        break;
    default:
        cmn_err(CE_CONT, "ztdummy: unknown attach command %d", cmd);
        return DDI_FAILURE;
    }

    instance = ddi_get_instance(dip);

    if (ddi_soft_state_zalloc(ztdummy_statep, instance) != DDI_SUCCESS)
    {
      cmn_err(CE_CONT, "ztdummy%d: Failed to alloc soft state", instance);
      return DDI_FAILURE;
    }

    /* Get pointer to that memory */
    ztd = ddi_get_soft_state(ztdummy_statep, instance);

    if (ztd == NULL) {
	    cmn_err(CE_CONT, "ztdummy: Unable to allocate memory\n");
	    ddi_soft_state_free(ztdummy_statep, instance);
	    return DDI_FAILURE;
    }

    ztd->dip = dip;

    if (ztdummy_initialize(ztd)) {
		cmn_err(CE_CONT, "ztdummy: Unable to intialize zaptel driver\n");
		ddi_soft_state_free(ztdummy_statep, instance);
		return DDI_FAILURE;
    }

	/* do not allow us to become unloaded automatically */
	if (ddi_prop_update_int(makedevice(DDI_MAJOR_T_UNKNOWN, instance), dip, "ddi-no-autodetach", 1) == -1) {
		cmn_err(CE_WARN, "ztdummmy: updating ddi-no-autodetach failed");
		return DDI_FAILURE;
	}
	
	/*
	 * Setup a high-resolution timer using an undocumented API in the kernel
	 *
	 * For more information visit the URL below:
	 * http://blogs.sun.com/roller/page/eschrock?entry=inside_the_cyclic_subsystem
	 *
	 */
    hdlr.cyh_func = ztdummy_timer;
    hdlr.cyh_arg = 0;
    hdlr.cyh_level = CY_LOW_LEVEL;

    when.cyt_when = 0;
    when.cyt_interval = 1000000; /* every 1ms */

    mutex_enter(&cpu_lock); 
    ztd->cyclic = cyclic_add(&hdlr, &when);
    mutex_exit(&cpu_lock);

    if (debug)
        cmn_err(CE_CONT, "ztdummy: init() finished\n");
	global_sticky = 1;
    return 0;
}

static int ztdummy_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    int instance;
    struct ztdummy_state *ztd;

    instance = ddi_get_instance(dip);
    cmn_err(CE_CONT, "ztdummy%d: detach", instance);

    ztd = ddi_get_soft_state(ztdummy_statep, instance);
    if (ztd == NULL) {
        cmn_err(CE_CONT, "ztdummy%d: detach, failed to get soft state", instance);
        return DDI_FAILURE;
    }

    /* Remove high-resolution timer */
    cyclic_remove(ztd->cyclic);

    zt_unregister(&ztd->span);

    return DDI_SUCCESS;
}

