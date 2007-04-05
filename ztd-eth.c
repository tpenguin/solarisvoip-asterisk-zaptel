/*
 * Dynamic Span Interface for Zaptel (Ethernet Interface via STREAMS module)
 *
 * Written by Joseph Benden <joe@thrallingpenguin.com>
 *
 * Copyright (C) 2006-2007 Thralling Penguin LLC. All rights reserved.
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
#include <sys/ethernet.h>
#include <sys/stream.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/cred.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <inet/common.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stddef.h>
/* Must be after other includes */
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/cmn_err.h>
#include <sys/dlpi.h>

#ifdef STANDALONE_ZAPATA
#include "zaptel.h"
#else
#include <zaptel.h>
#endif

#include "compat.h"
#include "ztd-eth.h"

#ifndef ALIGN32
# define        ALIGN32(x)      (x)
#endif
#ifndef MTYPE
# define        MTYPE(m)        ((m)->b_datap->db_type)
#endif
#ifndef MLEN
# define        MLEN(m)         ((m)->b_wptr - (m)->b_rptr)
#endif

/* we depend on these drivers to operate */
char _depends_on[] = "drv/ip drv/zaptel drv/ztdynamic";



static queue_t *global_queue = NULL;
static int debug = 0;
static spinlock_t zlock;

struct ztdeth_header {
	uint16_t subaddr;           /* Zaptel ethernet subaddress */
};

static struct ztdeth {
	unsigned char addr[64];
	uint16_t subaddr;           /* Network byte order */
	struct zt_span *span;
	char ethdev[64];            /* actual interface named passed, ie: rtls0 */
	struct net_device *dev;     /* this isn't used */
	struct ztdeth *next;
} *zdevs = NULL;

typedef struct ztdeth_mod_s {
    uint_t mm_flags;            /* Flags below */
    int mm_muxid;               /* This is our muxid, which is needed for UNPLUMB */
    queue_t *mm_wq;
    t_uscalar_t mm_sap;
    uint_t mm_ref;
    ztdeth_strid_t mm_strid;
    list_node_t mm_node;
} ztdeth_mod_t;

typedef struct ztdeth_drv_s {
    uint_t md_flags;            /* Flags below */
    t_uscalar_t md_dlstate;
    queue_t *md_rq;
    minor_t md_minor;
} ztdeth_drv_t;

#define MD_ISDRIVER 0x00000001
#define MM_INLIST   0x00000002
#define MM_TUNNELIN 0x00000004

static dev_info_t *zdeth_dev_info = NULL;

/**
 * Minor node number allocations
 */
/*
static uint_t ztdeth_max_minors;
static vmem_t *ztdeth_drv_minors;
*/

/*                                                              
 * List of streams on which ztdeth is plumbed                                                                
 */
static list_t ztdeth_mod_list;
static kmutex_t ztdeth_mod_lock;

static int zdethdevopen(queue_t *, dev_t *, int, int, cred_t *);
static int zdethdevclose(queue_t *, int, cred_t *);

/*
static struct qinit zdeth_rinit = {
	NULL, NULL, zdethdevopen, zdethdevclose, NULL, &zdeth_minfo, NULL
};

static struct qinit zdeth_winit = {
	//zdethwput
    NULL, NULL, NULL, NULL, NULL, &zdeth_minfo, NULL
};

struct streamtab zteth_dev_strtab = {
	&zdeth_rinit, &zdeth_winit, NULL, NULL
};
*/

struct zt_span *ztdeth_getspan(unsigned char *addr, unsigned short subaddr);
static int zdeth_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
                         void **result);
static int zdeth_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int zdeth_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
void zdethmodrput(queue_t *, mblk_t *);
void zdethmodwput(queue_t *, mblk_t *);
static void zdethmod_ctl(queue_t *, mblk_t *);
static void zdethmod_ioctl(queue_t *, mblk_t *);
static struct streamtab zdethmod_strtab;
static struct zt_dynamic_driver ztd_eth;
static int zdethmodopen(queue_t *, dev_t *, int, int, cred_t *);
static int zdethmodclose(queue_t *, int, cred_t *);
static void zdethmod_outproto(queue_t *, mblk_t *);

/* Nobody should ever use this function */
/* ARGSUSED */
static void
zdethmodlrput(queue_t *rq, mblk_t *mp)
{
	
	mblk_t *m = mp;
	register struct ether_header *eh = NULL;
	size_t off = 0, len, mlen;
	
	switch (mp->b_datap->db_type)
	{
    case M_FLUSH:
        if (*mp->b_rptr & FLUSHW) {
            *mp->b_rptr &= ~FLUSHR;
            qreply(rq, mp);
        } else
            freemsg(mp);
        break;

    case M_ERROR:
    case M_HANGUP:
        freemsg(mp);
        break;

    case M_DATA:
    case M_PCPROTO:
    case M_PROTO:
    default:
        freemsg(mp);
		break;
	}
}

/* Nobody should ever use this function */
/* ARGSUSED */
static void
zdethmodlwput(queue_t *wq, mblk_t *mp)
{
	if (debug) cmn_err(CE_NOTE, "unexpected lwput");
	freemsg(mp);
}

static struct streamtab zdethmod_strtab;

static struct cb_ops zdeth_ops = {
	nulldev,	        /* cb_open */
	nulldev,	        /* cb_close */
	nodev,		        /* cb_strategy */
	nodev,		        /* cb_print */
	nodev,		        /* cb_dump */
	nodev,		        /* cb_read */
	nodev,		        /* cb_write */
	nodev,		        /* cb_ioctl */
	nodev,		        /* cb_devmap */
	nodev,		        /* cb_mmap */
	nodev,		        /* cb_segmap */
	nochpoll,	        /* cb_chpoll */
	ddi_prop_op,        /* cb_prop_op */
	&zdethmod_strtab,	/* cb_stream */
	(D_NEW | D_MP)		/* cb_flag */
};

static struct dev_ops zdeth_devops = {
	DEVO_REV,			/* devo_rev */
	0,					/* devo_refcnt */
	zdeth_getinfo,		/* devo_getinfo */
	nulldev,			/* devo_identify */
	nulldev,			/* devo_probe */
	zdeth_attach,		/* devo_attach */
	zdeth_detach,		/* devo_detach */
	nodev,				/* devo_reset */
	&zdeth_ops,			/* devo_cb_ops */
	NULL				/* devo_bus_ops */
};

static struct modldrv modldrv = {
	&mod_driverops, 
    "Zaptel Dynamic Ethernet Driver", 
    &zdeth_devops
};

static struct module_info zdeth_minfo = {
	0x666b, 
    "ztd-eth", 
    0, 
    INFPSZ, 
    0, 
    0
};

static struct qinit zdethmod_rinit = {
	(pfi_t)zdethmodrput, NULL, zdethmodopen, zdethmodclose,
	NULL, &zdeth_minfo, NULL
};

static struct qinit zdethmod_winit = {
	(pfi_t)zdethmodwput, NULL, NULL, NULL, 
    NULL, &zdeth_minfo, NULL
};

static struct qinit zdethmod_lrinit = {
    (pfi_t)zdethmodlrput, NULL, /*zdethmodopen*/ NULL, /*zdethmodclose*/ NULL,
    NULL, &zdeth_minfo
};

static struct qinit zdethmod_lwinit = {
    (pfi_t)zdethmodlwput, NULL, NULL, NULL,
    NULL, &zdeth_minfo
};

static struct streamtab zdethmod_strtab = {
	&zdethmod_rinit, 
    &zdethmod_winit, 
    &zdethmod_lrinit, 
    &zdethmod_lwinit
};

static struct fmodsw fsw = {
	"ztd-eth", 
    &zdethmod_strtab, 
    (D_NEW | D_MP)
};

static struct modlstrmod modlstrmod = {
	&mod_strmodops, 
    "Zaptel Dynamic Streams Driver", 
    &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1,
    (void *)&modlstrmod,
    (void *)&modldrv,
};

static int zdeth_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

    //if (debug) 
        cmn_err(CE_CONT, "zdeth_attach\n");
    if (ddi_create_minor_node(devi, "ztdeth", S_IFCHR, 0, "ddi_zaptel",
        CLONE_DEV) == DDI_FAILURE) {
        ddi_remove_minor_node(devi, NULL);
        return (DDI_FAILURE);
    }

	zdeth_dev_info = devi;
	zt_dynamic_register(&ztd_eth);
	return (DDI_SUCCESS);
}

static int 
zdeth_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

    // if (debug) 
    cmn_err(CE_CONT, "zdeth_detach\n");
	zt_dynamic_unregister(&ztd_eth);
	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

static int 
zdeth_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **res)
{
	int result = DDI_FAILURE;
	
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (zdeth_dev_info != NULL) {
			*res = (void *)zdeth_dev_info;
			result = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*res = NULL;
		result = DDI_SUCCESS;
		break;
	default:
		break;
	}
	return (result);
}

static int 
zdethdevopen(queue_t *q, dev_t *devp, int oflag, int sflag, cred_t *crp)
{
	int result = 0;

    //if (debug) 
        cmn_err(CE_CONT, "devopen!\n");
	if ((sflag & MODOPEN) != 0)
		result = ENXIO;
	if (result == 0)
		qprocson(q);
	return (result);
}

static int 
zdethdevclose(queue_t *q, int flag, cred_t *crp)
{
    //if (debug) 
        cmn_err(CE_CONT, "devclose!\n");
	qprocsoff(q);
	return (0);
}

/*
static mblk_t *
zdeth_dlpi_comm(t_uscalar_t prim, size_t size)
{
	mblk_t *mp;
	
	if ((mp = allocb(size, BPRI_HI)) == NULL)
		return (NULL);
	
	MTYPE(mp) = (prim == DL_INFO_REQ) ? M_PCPROTO : M_PROTO;
	
	mp->b_wptr = mp->b_rptr + size;
	bzero(mp->b_rptr, size);
	((union DL_primitives *)mp->b_rptr)->dl_primitive = prim;
	
	return (mp);
}

static void 
zdeth_dlpi_send(queue_t *q, mblk_t *mp)
{
	putnext(q, mp);
}
*/

static int 
zdethmodopen(queue_t *rq, dev_t *devp, int oflag, int sflag, cred_t *credp)
{
    queue_t *wq = OTHERQ(rq);

    if (rq->q_ptr != NULL) {
        return (0);
    }

#ifdef PRIV_POLICY
    if (secpolicy_net_rawaccess(credp) != 0)
#else
    if (drv_priv(credp) != 0)
#endif
        return (EPERM);

    if (sflag & MODOPEN) {
        ztdeth_mod_t *modp;

        modp = kmem_zalloc(sizeof (*modp), KM_NOSLEEP);
        if (modp == NULL)
            return (ENOMEM);
        modp->mm_wq = wq;
        rq->q_ptr = wq->q_ptr = modp;
        if (debug) cmn_err(CE_CONT, "sflags & MODOPEN\n");
    } else {
        if (debug) cmn_err(CE_CONT, "sflags not MODOPEN\n");
        ztdeth_drv_t *drvp;
        static minor_t minor = 0;

        drvp = kmem_zalloc(sizeof (*drvp), KM_NOSLEEP);
        if (drvp == NULL)
            return (ENOMEM);

        // Lock us because of the static minor_t variable
        mutex_enter(&ztdeth_mod_lock);

        drvp->md_flags = MD_ISDRIVER;
        drvp->md_dlstate = DL_UNATTACHED;
        drvp->md_rq = rq;
        drvp->md_minor = minor;
        rq->q_ptr = wq->q_ptr = drvp;
        
        *devp = makedevice(getmajor(*devp), minor);

        if (debug) cmn_err(CE_CONT, "Major device is %d\n", minor++);

        mutex_exit(&ztdeth_mod_lock);

    }
	qprocson(rq);
	
	//if (debug) 
        cmn_err(CE_CONT, "zdethmodopen\n");
	return (0);
}

static int 
zdethmodclose(queue_t *rq, int flag, cred_t *crp)
{
    ztdeth_drv_t *drvp = rq->q_ptr;

    if (drvp == NULL) {
        qprocsoff(rq);
        cmn_err(CE_CONT, "zdethmodclose: drvp was NULL\n");
        return (0);
    }

    if (drvp->md_flags & MD_ISDRIVER) {
        qprocsoff(rq);

        kmem_free(drvp, sizeof (*drvp));
    } else {
        ztdeth_mod_t *modp = (ztdeth_mod_t *)drvp;
        qprocsoff(rq);
        kmem_free(modp, sizeof (*modp));
    }

	//if (debug) 
        cmn_err(CE_CONT, "zdethmodclose\n");
	return (0);
}

/*
 * Kernel module functions
 */
static int mod_is_init = 0; /* this module loads twice, so we need to keep track */

int 
_init(void)
{
    cmn_err(CE_CONT, "Zaptel Ethernet STREAMS and Module Driver\n");

    if (mod_is_init == 0) {

        list_create(&ztdeth_mod_list, sizeof (ztdeth_mod_t),
                    offsetof (ztdeth_mod_t, mm_node));
        mutex_init(&ztdeth_mod_lock, NULL, MUTEX_DRIVER, NULL);
        mod_is_init = 1;
    } else {

        cmn_err(CE_CONT, "DUPLICATE _init CALL.\n");
    }
	if (mod_install(&modlinkage) != DDI_SUCCESS) {
        cmn_err(CE_CONT, "mod_install failed.\n");
        if (mod_is_init == 1) {

            mutex_destroy(&ztdeth_mod_lock);
            list_destroy(&ztdeth_mod_list);
            mod_is_init = 0;
        }
        return (DDI_FAILURE);
    }
    return (DDI_SUCCESS);
}

int 
_fini(void)
{
    cmn_err(CE_CONT, "Zaptel Ethernet STREAMS and Module Driver Unloaded\n");

    if (mod_is_init == 1) {
        mutex_destroy(&ztdeth_mod_lock);
        list_destroy(&ztdeth_mod_list);
        mod_is_init = 0;
    } else {
        cmn_err(CE_CONT, "DUPLICATE _fini CALL\n");
    }
	return (mod_remove(&modlinkage));
}

int 
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static void 
zdethmod_watchproto(queue_t *wq, mblk_t *mp)
{
    ztdeth_mod_t *modp = wq->q_ptr;
    union DL_primitives *dlp = (union DL_primitives *)mp->b_rptr;
    uint_t blkl = MBLKL(mp);

    if (blkl >= sizeof (dlp->dl_primitive)) {
        switch (dlp->dl_primitive) {
        case DL_BIND_REQ:
            if (blkl >= sizeof (dlp->bind_ack)) {
                modp->mm_sap = dlp->bind_ack.dl_sap;
            }
            break;
        case DL_PROMISCON_REQ:
            if (blkl >= sizeof (dlp->promiscon_req)) {
                modp->mm_flags |= MM_TUNNELIN;
            }
            break;
        }
    }
    putnext(wq, mp);
}

void 
zdethmodwput(queue_t *q, mblk_t *mp)
{
    ztdeth_drv_t *drvp;

    ASSERT(q != NULL && q->q_ptr != NULL);
    ASSERT(mp != NULL && mp->b_rptr != NULL);
    drvp = (ztdeth_drv_t *)q->q_ptr;

	switch (DB_TYPE(mp)) {
    case M_IOCTL:
        zdethmod_ioctl(q, mp);
        break;

	case M_PROTO:
    case M_PCPROTO:
        if (drvp->md_flags & MD_ISDRIVER) {
            zdethmod_outproto(q, mp);
        } else {
            zdethmod_watchproto(q, mp);
        }
		break;

    case M_DATA:
        if (drvp->md_flags & MD_ISDRIVER) {
            cmn_err(CE_CONT,"zdethmodwput\n");
        } else {
            freemsg(mp);
        }
		break;

    case M_CTL:
        if (drvp->md_flags & MD_ISDRIVER) {
            freemsg(mp);
        } else {
            zdethmod_ctl(q, mp);
        }
        break;

    case M_FLUSH:
        if (drvp->md_flags & MD_ISDRIVER) {
            if (*mp->b_rptr & FLUSHR) {
                *mp->b_rptr &= ~FLUSHW;
                qreply(q, mp);
            } else {
                freemsg(mp);
            }
        } else {
            putnext(q, mp);
        }
        break;

    default:
        /*
		if (drvp->md_flags & MD_ISDRIVER) {
            freemsg(mp);
        } else {
            putnext(q, mp);
        }
        */
        freemsg(mp);
        break;
	}
}

static void 
zdethmod_ioctl(queue_t *q, mblk_t *mp)
{
    struct iocblk *ioc = (struct iocblk *)mp->b_rptr;
    ztdeth_drv_t *drvp = q->q_ptr;
    ztdeth_mod_t *modp = (ztdeth_mod_t *)drvp;
    mblk_t *newmp;
    int err = EINVAL;

    switch (ioc->ioc_cmd) {
    case I_LINK:
    case I_PLINK:
        cmn_err(CE_CONT, "Processing LINK\n");
        if ((newmp = copymsg(mp)) == NULL) {
            miocnak(q, mp, 0, ENOMEM);
        } else {
            struct linkblk *lwq =
                (struct linkblk *)mp->b_cont->b_rptr;
            newmp->b_datap->db_type = M_CTL;
            putnext(lwq->l_qbot, newmp);

            // This is ugly. We are bound to a single NIC in a machine
            // So this needs fixed in the future!
            global_queue = lwq->l_qbot;

            miocack(q, mp, 0, 0);
        }
        break;

    case I_UNLINK:
    case I_PUNLINK:
        cmn_err(CE_CONT, "Processing UNLINK\n");
        global_queue = NULL;
        miocack(q, mp, 0, 0);
        break;

    case ZTDIOC_SETID:
        if ((modp->mm_flags & (MM_INLIST)) ||
            (err = miocpullup(mp, sizeof (ztdeth_strid_t))) != 0) {
            miocnak(q, mp, 0, err);
        } else {
            bcopy(mp->b_cont->b_rptr, &modp->mm_strid, 
                  sizeof (modp->mm_strid));
            modp->mm_flags |= MM_INLIST;
            mutex_enter(&ztdeth_mod_lock);
            list_insert_tail(&ztdeth_mod_list, modp);
            mutex_exit(&ztdeth_mod_lock);
            miocack(q, mp, 0, 0);
            if (debug) cmn_err(CE_CONT, "Added modp to mod_list\n");
        }
        break;

    case ZTDIOC_GETMUXID:
        if (!(drvp->md_flags & MD_ISDRIVER) ||
            (err = miocpullup(mp, sizeof (ztdeth_strid_t))) != 0) {
            miocnak(q, mp, 0, err);
        } else {
            int muxid = 0;
            ztdeth_strid_t *si = (ztdeth_strid_t *)mp->b_cont->b_rptr;

            err = ENXIO;
            mutex_enter(&ztdeth_mod_lock);
            for (modp = list_head(&ztdeth_mod_list); modp != NULL;
                  modp = list_next(&ztdeth_mod_list, modp)) {
                if (modp->mm_strid.si_ismcast == si->si_ismcast &&
                    strcmp(modp->mm_strid.si_name, si->si_name) == 0) {
                    if (modp->mm_ref > 0) {
                        err = EBUSY;
                    } else {
                        muxid = modp->mm_muxid;
                    }
                    break;
                }
            }
            mutex_exit(&ztdeth_mod_lock);
            if (muxid == 0) {
                miocnak(q, mp, 0, err);
            } else {
                *(int *)si = muxid;
                miocack(q, mp, sizeof (int), 0);
            }
            if (debug) cmn_err(CE_CONT, "Returning muxid %d\n", muxid);
        }
        break;

    default:
        putnext(q, mp);
        break;
    }
}

static void 
zdethmod_ctl(queue_t *wq, mblk_t *mp)
{
    ztdeth_mod_t *modp = wq->q_ptr;
    mblk_t *mpnext = mp->b_cont;
    struct iocblk *ioc;
    struct linkblk *lwp;

    if (mpnext == NULL ||
        mp->b_wptr - mp->b_rptr != sizeof (*ioc) ||
        mpnext->b_wptr - mpnext->b_rptr != sizeof (*lwp) ||
        mpnext->b_cont != NULL) {
        cmn_err(CE_NOTE, "unexpected M_CTL");
        freemsg(mp);
        return;
    }

    /*                                                              
     * This is a copy of the ioctl. It gives us our mux id.                                                                
     */
    ioc = (struct iocblk *)mp->b_rptr;
    ASSERT(ioc != NULL);
    lwp = (struct linkblk *)mpnext->b_rptr;
    ASSERT(lwp != NULL);
    if (ioc->ioc_cmd == I_LINK || ioc->ioc_cmd == I_PLINK) {
        modp->mm_muxid = lwp->l_index;
    }
    freemsg(mp);
}

static void 
zdethmod_outproto(queue_t *q, mblk_t *mp)
{
    ztdeth_drv_t    *drvp = q->q_ptr;
	union DL_primitives *dlp = (union DL_primitives *)mp->b_rptr;
    uint_t blkl = MBLKL(mp);

	if (blkl < sizeof (dlp->dl_primitive)) {
		freemsg(mp);
		return;
	}

	switch (dlp->dl_primitive) {
    case DL_INFO_REQ:
        if (debug) cmn_err(CE_CONT, "Processing DL_INFO_REQ\n");
        mp = mexchange(q, mp, sizeof(dl_info_ack_t), M_PCPROTO, -1);
        if (mp == NULL) {
            break;
        }
        dlp = (union DL_primitives *)mp->b_rptr;
        bzero(dlp, sizeof(dlp->info_ack));
        dlp->info_ack.dl_primitive = DL_INFO_ACK;
        dlp->info_ack.dl_max_sdu = 4096;
        dlp->info_ack.dl_mac_type = DL_OTHER;
        dlp->info_ack.dl_current_state = drvp->md_dlstate;
        dlp->info_ack.dl_service_mode = DL_CLDLS;
        dlp->info_ack.dl_provider_style = DL_STYLE2;
        dlp->info_ack.dl_version = DL_VERSION_2;
        qreply(q, mp);
        break;

    case DL_BIND_REQ:
        if (debug) cmn_err(CE_CONT, "ack bind req\n");
        if (drvp->md_dlstate == DL_UNBOUND) {
            drvp->md_dlstate = DL_IDLE;
            dlbindack(q, mp, dlp->bind_req.dl_sap, NULL, 0, 0, 0);
        } else {
            dlerrorack(q, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
        }
		break;

    case DL_UNBIND_REQ:
        if (debug) cmn_err(CE_CONT, "ack unbind req\n");
        if (drvp->md_dlstate == DL_IDLE) {
            drvp->md_dlstate = DL_UNBOUND;
            dlokack(q, mp, DL_UNBIND_REQ);
        } else {
            dlerrorack(q, mp, DL_UNBIND_REQ, DL_OUTSTATE, 0);
        }
        break;

    case DL_ENABMULTI_REQ:
    case DL_DISABMULTI_REQ:
    case DL_PROMISCON_REQ:
    case DL_PROMISCOFF_REQ:
        dlokack(q, mp, dlp->dl_primitive);
        break;

    case DL_UNITDATA_REQ:
        freemsg(mp);
        break;

    case DL_NOTIFY_REQ:
        dlnotifyack(q, mp, 0);
        break;

    case DL_ATTACH_REQ:
        if (debug) cmn_err(CE_CONT, "ack attach req\n");
        if (drvp->md_dlstate == DL_UNATTACHED) {
            drvp->md_dlstate = DL_UNBOUND;
            dlokack(q, mp, DL_ATTACH_REQ);
        } else {
            dlerrorack(q, mp, DL_ATTACH_REQ, DL_OUTSTATE, 0);
        }
        break;

    case DL_DETACH_REQ:
        if (debug) cmn_err(CE_CONT, "ack detach req\n");
        if (drvp->md_dlstate == DL_UNBOUND) {
            drvp->md_dlstate = DL_UNATTACHED;
            dlokack(q, mp, DL_DETACH_REQ);
        } else {
            dlerrorack(q, mp, DL_DETACH_REQ, DL_OUTSTATE, 0);
        }
        break;
		
	default:
		if (debug) cmn_err(CE_NOTE, "error ack on %d", dlp->dl_primitive);
        dlerrorack(q, mp, dlp->dl_primitive, DL_BADPRIM, 0);
        break;
	}
}

static void 
zdethmod_inproto(queue_t *q, mblk_t *mp)
{
    ztdeth_mod_t *modp = q->q_ptr;
    mblk_t *m = mp;
	register struct ether_header *eh = NULL;
	uint_t len, mlen;
    int handled = 0;

    for (; m != NULL; m = m->b_cont) {
        if ((DB_TYPE(m) == M_DATA) && (MBLKL(m) > 0)) {
            eh = (struct ether_header *)(m->b_rptr - sizeof(struct ether_header));
            len = MBLKL(m);
            mlen = msgdsize(m);

#ifdef DUMP_PACKET
            if (debug) {
                cmn_err(CE_CONT, "!frame src=%02x:%02x:%02x:%02x:%02x:%02x dst=%02x:%02x:%02x:%02x:%02x:%02x len=%d/%d type=%x\n",
                    eh->ether_shost.ether_addr_octet[0], eh->ether_shost.ether_addr_octet[1],
                    eh->ether_shost.ether_addr_octet[2], eh->ether_shost.ether_addr_octet[3],
                    eh->ether_shost.ether_addr_octet[4], eh->ether_shost.ether_addr_octet[5],
                    eh->ether_dhost.ether_addr_octet[0], eh->ether_dhost.ether_addr_octet[1],
                    eh->ether_dhost.ether_addr_octet[2], eh->ether_dhost.ether_addr_octet[3],
                    eh->ether_dhost.ether_addr_octet[4], eh->ether_dhost.ether_addr_octet[5],
                    len, mlen, eh->ether_type);
            }
#endif

#ifndef __sparc
#   define ZTDETH_HTONS 0x0DD0
#else
#   define ZTDETH_HTONS 0xD00D 
#endif

            if (eh && eh->ether_type == ZTDETH_HTONS && len > sizeof(struct ztdeth_header)) {
                struct zt_span *span;
                register struct ztdeth_header *zh;
                zh = (struct ztdeth_header *)m->b_rptr;

                if (zh) {
                    span = ztdeth_getspan(eh->ether_shost.ether_addr_octet, zh->subaddr);

                    if (span != NULL) {
                        zt_dynamic_receive(span, (unsigned char *)zh + sizeof(struct ztdeth_header), len - sizeof(struct ztdeth_header));
                    }
                }
                freemsg(mp);
                handled++;
            }
        }
    }
    if (handled <= 0) {
        putnext(q, mp);
    }
    if (handled > 1) {
        cmn_err(CE_CONT, "Multipacket frames\n");
    }
}

void 
zdethmodrput(queue_t *q, mblk_t *mp)
{
    ztdeth_drv_t *drvp = q->q_ptr;

    ASSERT(drvp != NULL);
    ASSERT(mp != NULL);

    if (drvp->md_flags & MD_ISDRIVER) {
        freemsg(mp);
        return;
    }

	switch (DB_TYPE(mp)) {	
	case M_PCPROTO:
    case M_PROTO:
        zdethmod_inproto(q, mp);
        break;

    case M_DATA:
        if (debug) cmn_err(CE_CONT, "Got DATA, but what to do with it?\n");
		break;

	default:
		putnext(q, mp);
        break;
	}
}

struct zt_span *
ztdeth_getspan(unsigned char *addr, uint16_t subaddr)
{
	unsigned long flags;
	struct ztdeth *z;
	struct zt_span *span = NULL;

	spin_lock_irqsave(&zlock, flags);
	z = zdevs;
	while(z) {
		if (!memcmp(addr, z->addr, 6 /* ETHERADDRL */) &&
			z->subaddr == subaddr)
			break;
		z = z->next;
	}
	if (z)
		span = z->span;
	spin_unlock_irqrestore(&zlock, flags);
	return (span);
}

static int 
digit2int(char d)
{
	switch(d) {
	case 'F':
	case 'E':
	case 'D':
	case 'C':
	case 'B':
	case 'A':
		return (d - 'A' + 10);
	case 'f':
	case 'e':
	case 'd':
	case 'c':
	case 'b':
	case 'a':
		return (d - 'a' + 10);
	case '9':
	case '8':
	case '7':
	case '6':
	case '5':
	case '4':
	case '3':
	case '2':
	case '1':
	case '0':
		return (d - '0');
	}
	return (-1);
}

static int 
hex2int(char *s)
{
	int res;
	int tmp;
	/* Gotta be at least one digit */
	if (strlen(s) < 1)
		return (-1);
	/* Can't be more than two */
	if (strlen(s) > 2)
		return (-1);
	/* Grab the first digit */
	res = digit2int(s[0]);
	if (res < 0)
		return (-1);
	tmp = res;
	/* Grab the next */
	if (strlen(s) > 1) {
		res = digit2int(s[1]);
		if (res < 0)
			return (-1);
		tmp = tmp * 16 + res;
	}
	return (tmp);
}

static void 
ztdeth_destroy(void *pvt)
{
	struct ztdeth *z = pvt;
	unsigned long flags;
	struct ztdeth *prev=NULL, *cur;

	spin_lock_irqsave(&zlock, flags);
	cur = zdevs;
	while(cur) {
		if (cur == z) {
			if (prev)
				prev->next = cur->next;
			else
				zdevs = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	spin_unlock_irqrestore(&zlock, flags);
	if (cur == z) {	/* Successfully removed */
		cmn_err(CE_CONT, "TDMoE: Removed interface for %s\n", z->span->name);
		kmem_free(z, sizeof(struct ztdeth));
	}
}

static void *
ztdeth_create(struct zt_span *span, char *addr)
{
	struct ztdeth *z;
	char src[256];
	char tmp[256], *tmp2, *tmp3, *tmp4 = NULL;
	int res,x;
	unsigned long flags;

	z = (struct ztdeth *)kmem_zalloc(sizeof(struct ztdeth), KM_NOSLEEP);
	if (z) {
		/* Address should be <dev>/<macaddr>[/subaddr] */
		strncpy(tmp, addr, sizeof(tmp) - 1);
		tmp2 = strchr(tmp, '/');
		if (tmp2) {
			*tmp2 = '\0';
			tmp2++;
			strncpy(z->ethdev, tmp, sizeof(z->ethdev) - 1);
		} else {
			printk("Invalid TDMoE address (no device) '%s'\n", addr);
			kmem_free(z, sizeof(struct ztdeth));
			return (NULL);
		}
		if (tmp2) {
			tmp4 = strchr(tmp2+1, '/');
			if (tmp4) {
				*tmp4 = '\0';
				tmp4++;
			}
			/* We don't have SSCANF :(  Gotta do this the hard way */
			tmp3 = strchr(tmp2, ':');
			for (x=0;x<6;x++) {
				if (tmp2) {
					if (tmp3) {
						*tmp3 = '\0';
						tmp3++;
					}
					res = hex2int(tmp2);
					if (res < 0)
						break;
					z->addr[x] = res & 0xff;
				} else
					break;
				if ((tmp2 = tmp3))
					tmp3 = strchr(tmp2, ':');
			}
			if (x != 6) {
				printk("TDMoE: Invalid MAC address in: %s\n", addr);
				kmem_free(z, sizeof(struct ztdeth));
				return (NULL);
			}
		} else {
			printk("TDMoE: Missing MAC address\n");
			kmem_free(z, sizeof(struct ztdeth));
			return (NULL);
		}
		if (tmp4) {
			int sub = 0;
			int mul = 1;

			/* We have a subaddr */
			tmp3 = tmp4 + strlen (tmp4) - 1;
			while (tmp3 >= tmp4) {
				if (*tmp3 >= '0' && *tmp3 <= '9') {
					sub += (*tmp3 - '0') * mul;
				} else {
					printk("TDMoE: Invalid subaddress\n");
					kmem_free(z, sizeof(struct ztdeth));
					return (NULL);
				}
				mul *= 10;
				tmp3--;
			}
			z->subaddr = htons(sub);
		}
		z->span = span;

		cmn_err(CE_CONT, "TDMoE: Added new interface for %s (addr=%s, subaddr=%d)\n", span->name, addr, ntohs(z->subaddr));
    
		spin_lock_irqsave(&zlock, flags);
		z->next = zdevs;
		zdevs = z;
		spin_unlock_irqrestore(&zlock, flags);
	}
	return (z);
}

static int 
ztdeth_transmit_frame(queue_t *q, unsigned char *daddr, char *msg, int msglen, unsigned short subaddr)
{
    mblk_t *mb, *mbd;
    struct ether_header *ehp;
    dl_unitdata_req_t *udr;
    struct ztdeth_header *zh;
    unsigned char buf[8];

    if (q == NULL) {
        return (-2);
    }

    mb = allocb(DL_UNITDATA_REQ_SIZE + 8, BPRI_HI);
    if (mb == NULL) {
        cmn_err(CE_CONT, "Major problem, mb alloc failed.\n");
        return (-1);
    }

    MTYPE(mb) = M_PROTO;
    mb->b_wptr += DL_UNITDATA_REQ_SIZE;
    udr = (dl_unitdata_req_t *)mb->b_rptr;
    udr->dl_primitive = DL_UNITDATA_REQ;
    udr->dl_dest_addr_length = 8; // Size of ethernet address + ethernet type
    udr->dl_dest_addr_offset = DL_UNITDATA_REQ_SIZE;
    udr->dl_priority.dl_min = udr->dl_priority.dl_max = 0;

    bcopy((caddr_t)daddr, (caddr_t)mb->b_wptr, 6);
    mb->b_wptr += 6;

#ifdef __sparc
    *((unsigned char *)mb->b_wptr) = 0xd0;
    mb->b_wptr += 1;
    *((unsigned char *)mb->b_wptr) = 0x0d;
    mb->b_wptr += 1;
#else
    *((unsigned char *)mb->b_wptr) = 0x0d;
    mb->b_wptr += 1;
    *((unsigned char *)mb->b_wptr) = 0xd0;
    mb->b_wptr += 1;
#endif
    
    mbd = allocb(msglen + sizeof (struct ztdeth_header), BPRI_HI);
    if (mbd == NULL) {
        cmn_err(CE_CONT, "Major problem, mbd alloc failed.\n");
        freemsg(mb);
        return (-1);
    }

    mb->b_cont = mbd; /* connect proto mblock to data mblock */
    MTYPE(mbd) = M_DATA;
    zh = (struct ztdeth_header *)mbd->b_rptr;
    zh->subaddr = subaddr;
    mbd->b_wptr += sizeof(struct ztdeth_header);
    bcopy((caddr_t)msg, (caddr_t)mbd->b_wptr, msglen);
    mbd->b_wptr += msglen;

#ifdef DEBUG_SEND_SIDE
    cmn_err(CE_CONT, "Sending zaptel frame dst=%02x:%02x:%02x:%02x:%02x:%02x subaddr=%d len=%d\n",
        daddr[0], daddr[1],
        daddr[2], daddr[3],
        daddr[4], daddr[5],
        subaddr, msglen);
#endif

    /* send the message */
    putnext(q, mb);
    return (0);
}

static int 
ztdeth_transmit(void *pvt, unsigned char *msg, int msglen)
{
	struct ztdeth *z;

    if (pvt == NULL)
        return (-1);

    z = (struct ztdeth *)pvt;
    ztdeth_transmit_frame(global_queue, z->addr, (char *)msg, msglen, z->subaddr);

	return (0);
}

static struct zt_dynamic_driver 
ztd_eth = {
	"eth",
	"Ethernet",
	ztdeth_create,
	ztdeth_destroy,
	ztdeth_transmit
};

