/*
 * Dynamic Span Interface for Zaptel (Ethernet Interface via STREAMS module)
 *
 * Written by Joseph Benden <joe@thrallingpenguin.com>
 *
 * Copyright (C) 2006 Thralling Penguin LLC. All rights reserved.
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

#ifndef ALIGN32
# define        ALIGN32(x)      (x)
#endif
#ifndef MTYPE
# define        MTYPE(m)        ((m)->b_datap->db_type)
#endif

#ifndef MLEN
# define        MLEN(m)         ((m)->b_wptr - (m)->b_rptr)
#endif

char _depends_on[] = "drv/ip drv/zaptel drv/ztdynamic";

static struct module_info zdeth_minfo = {
	0x666b, "ztd-eth", 0, INFPSZ, 0, 0
};

static int debug = 1;
static spinlock_t zlock;

#define ETH_P_ZTDETH	0xd00d /* Ethernet Type Field Value */

struct ztdeth_header {
	unsigned short subaddr;
};

static struct ztdeth {
	unsigned char addr[64];
	unsigned short subaddr; /* Network byte order */
	struct zt_span *span;
	char ethdev[64];
	struct net_device *dev;
	struct ztdeth *next;
} *zdevs = NULL;

typedef struct ztdeth_mod_s {
    uint_t mm_flags;
    int mm_muxid;
    queue_t *mm_wq;
    t_uscalar_t mm_sap;
} ztdeth_mod_t;

typedef struct ztdeth_drv_s {
    uint_t md_flags;
    t_uscalar_t md_dlstate;
    queue_t *md_rq;
    minor_t md_minor;
} ztdeth_drv_t;

#define MD_ISDRIVER 0x00000001
#define MM_TUNNELIN 0x00000004

static dev_info_t *zdeth_dev_info = NULL;

/**
 * Minor node number allocations
 */
static uint_t ztdeth_max_minors;
static vmem_t *ztdeth_drv_minors;


static int zdethdevopen(queue_t *, dev_t *, int, int, cred_t *);
static int zdethdevclose(queue_t *, int, cred_t *);

static struct qinit zdeth_rinit = {
	NULL, NULL, zdethdevopen, zdethdevclose, NULL, &zdeth_minfo, NULL
};

static struct qinit zdeth_winit = {
	/* zdethwput */ NULL, NULL, NULL, NULL, NULL, &zdeth_minfo, NULL
};

struct streamtab zteth_dev_strtab = {
	&zdeth_rinit, &zdeth_winit, NULL, NULL
};


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
	cmn_err(CE_NOTE, "unexpected lrput");
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
		break;

	default:
		freemsg(mp);
		return;
	}
	
	if (m == NULL) {
		freemsg(mp);
		return;
	}
    freemsg(mp);
}

/* Nobody should ever use this function */
/* ARGSUSED */
static void
zdethmodlwput(queue_t *wq, mblk_t *mp)
{
	cmn_err(CE_NOTE, "unexpected lwput");
	freemsg(mp);
}

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
	D_NEW | D_MP		/* cb_flag */
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

static struct qinit zdethmod_rinit = {
	(pfi_t)zdethmodrput, NULL, zdethmodopen, zdethmodclose,
	NULL, &zdeth_minfo, NULL
};

static struct qinit zdethmod_winit = {
	(pfi_t)zdethmodwput, NULL, NULL, NULL, 
    NULL, &zdeth_minfo, NULL
};

static struct qinit zdethmod_lrinit = {
    (pfi_t)zdethmodlrput, NULL, zdethmodopen, zdethmodclose,
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
    D_NEW | D_MP
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
    NULL
};

static int zdeth_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

    if (ddi_create_minor_node(devi, "ztdeth", S_IFCHR, 0, "ddi_zaptel",
        CLONE_DEV) == DDI_FAILURE) {
        ddi_remove_minor_node(devi, NULL);
        return (DDI_FAILURE);
    }

	zdeth_dev_info = devi;
	zt_dynamic_register(&ztd_eth);

	return (DDI_SUCCESS);
}

static int zdeth_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);
	
	// ASSERT(devi == zdeth_dev_info);
	zt_dynamic_unregister(&ztd_eth);
	ddi_remove_minor_node(devi, NULL);

	return (DDI_SUCCESS);
}

static int zdeth_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **res)
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
	return result;
}


static int zdethdevopen(queue_t *q, dev_t *devp, int oflag, int sflag, cred_t *crp)
{
	int result = 0;

    cmn_err(CE_CONT, "devopen!\n");
	if ((sflag & MODOPEN) != 0)
		result = ENXIO;
	if (result == 0)
		qprocson(q);
	return result;
}

static int zdethdevclose(queue_t *q, int flag, cred_t *crp)
{
    cmn_err(CE_CONT, "devclose!\n");
	qprocsoff(q);
	return 0;
}


static mblk_t *zdeth_dlpi_comm(t_uscalar_t prim, size_t size)
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

static void zdeth_dlpi_send(queue_t *q, mblk_t *mp)
{
	putnext(q, mp);
}
	
static int zdethmodopen(queue_t *rq, dev_t *devp, int oflag, int sflag, cred_t *credp)
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
        minor_t minor;

        drvp = kmem_zalloc(sizeof (*drvp), KM_NOSLEEP);
        if (drvp == NULL)
            return (ENOMEM);
        minor = (minor_t)(uintptr_t)vmem_alloc(ztdeth_drv_minors, 1, VM_NOSLEEP);
        if (minor == 0) {
            void *vaddr;

            vaddr = vmem_add(ztdeth_drv_minors,
              (void *)((uintptr_t)ztdeth_max_minors + 1),
              10, VM_NOSLEEP);
            if (vaddr != NULL) {
                ztdeth_max_minors += 10;
                minor = (minor_t)(uintptr_t)vmem_alloc(ztdeth_drv_minors, 1, VM_NOSLEEP);
            }
            if (minor == 0) {
                kmem_free(drvp, sizeof(*drvp));
                return (ENOMEM);
            }
        }
        drvp->md_flags = MD_ISDRIVER;
        drvp->md_dlstate = DL_UNATTACHED;
        drvp->md_rq = rq;
        drvp->md_minor = minor;
        rq->q_ptr = wq->q_ptr = drvp;
        if (debug) cmn_err(CE_CONT, "Major device is %d\n", minor);
        *devp = makedevice(getmajor(*devp), minor);
    }
	qprocson(rq);
	
	if (debug) cmn_err(CE_CONT, "zdethmodopen\n");
	return (0);
}

static int zdethmodclose(queue_t *rq, int flag, cred_t *crp)
{
    ztdeth_drv_t *drvp = rq->q_ptr;

    if (drvp->md_flags & MD_ISDRIVER) {
        qprocsoff(rq);
        vmem_free(ztdeth_drv_minors, (void *)(uintptr_t)drvp->md_minor, 1);
        kmem_free(drvp, sizeof (*drvp));
    } else {
        ztdeth_mod_t *modp = (ztdeth_mod_t *)drvp;

        qprocsoff(rq);
        kmem_free(modp, sizeof (*modp));
    }
	if (debug) cmn_err(CE_CONT, "zdethmodclose\n");
	return (0);
}

/*
 * Kernel module functions
 */
int _init(void)
{
    ztdeth_max_minors = 10;
    ztdeth_drv_minors = vmem_create("ztdeth_minor", (void *)1, 
                                    ztdeth_max_minors, 1, NULL,
                                    NULL, NULL, 0, VM_SLEEP | VMC_IDENTIFIER);
    if (ztdeth_drv_minors == NULL) {
        return (ENOMEM);
    }
    if (debug) cmn_err(CE_CONT, "Created ztdeth_drv_minors\n");
	return mod_install(&modlinkage);
}

int _fini(void)
{
    if (debug) cmn_err(CE_CONT, "_fini\n");
    if (ztdeth_drv_minors != NULL) {
        vmem_destroy(ztdeth_drv_minors);
    }
	return mod_remove(&modlinkage);
}

int _info(struct modinfo *modinfop)
{
    if (debug) cmn_err(CE_CONT, "_info\n");
	return mod_info(&modlinkage, modinfop);
}

static void zdethmod_watchproto(queue_t *wq, mblk_t *mp)
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

void zdethmodwput(queue_t *q, mblk_t *mp)
{
    ztdeth_drv_t *drvp = q->q_ptr;

    if (debug) cmn_err(CE_CONT, "entered wput\n");
	switch (DB_TYPE(mp)) {
    case M_IOCTL:
        if (debug) cmn_err(CE_CONT, "Handling IOCTL\n");
        zdethmod_ioctl(q, mp);
        break;

	case M_PROTO:
    case M_PCPROTO:
        if (debug) cmn_err(CE_CONT, "M_PROTO\n");
        if (drvp->md_flags & MD_ISDRIVER) {
            zdethmod_outproto(q, mp);
        } else {
            zdethmod_watchproto(q, mp);
        }
		break;

    case M_DATA:
        if (debug) cmn_err(CE_CONT, "M_DATA\n");
        if (drvp->md_flags & MD_ISDRIVER) {
            cmn_err(CE_CONT,"zdethmodwput\n");
        } else {
            freemsg(mp);
        }
		break;

    case M_CTL:
        if (debug) cmn_err(CE_CONT, "M_CTL\n");
        if (drvp->md_flags & MD_ISDRIVER) {
            freemsg(mp);
        } else {
            zdethmod_ctl(q, mp);
        }
        break;

    case M_FLUSH:
        if (debug) cmn_err(CE_CONT, "M_FLUSH\n");
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
        if (debug) cmn_err(CE_CONT, "zdethmodwput default handler.\n");
		if (drvp->md_flags & MD_ISDRIVER) {
            freemsg(mp);
        } else {
            putnext(q, mp);
        }
        break;
	}
}

void dump_mac(const char *log, uchar_t *m)
{
    if (debug <= 0) return;

	cmn_err(CE_CONT, "%s %02x:%02x:%02x:%02x:%02x:%02x", log,
            m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void zdethmod_ioctl(queue_t *q, mblk_t *mp)
{
    struct iocblk *ioc = (struct iocblk *)mp->b_rptr;
    mblk_t *newmp;

    switch (ioc->ioc_cmd) {
    case I_LINK:
    case I_PLINK:
    case I_UNLINK:
    case I_PUNLINK:
        if (debug) cmn_err(CE_CONT, "Processing LINK/UNLINK\n");
        if ((newmp = copymsg(mp)) == NULL) {
            miocnak(q, mp, 0, ENOMEM);
        } else {
            struct linkblk *lwq =
                (struct linkblk *)mp->b_cont->b_rptr;
            newmp->b_datap->db_type = M_CTL;
            putnext(lwq->l_qbot, newmp);
            miocack(q, mp, 0, 0);
        }
        break;

    default:
        putnext(q, mp);
        break;
    }
}
static void zdethmod_ctl(queue_t *wq, mblk_t *mp)
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
    lwp = (struct linkblk *)mpnext->b_rptr;
    if (ioc->ioc_cmd == I_LINK || ioc->ioc_cmd == I_PLINK) {
        modp->mm_muxid = lwp->l_index;
    }
    freemsg(mp);
}

static void zdethmod_outproto(queue_t *q, mblk_t *mp)
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
        cmn_err(CE_CONT, "ack bind req\n");
        if (drvp->md_dlstate == DL_UNBOUND) {
            drvp->md_dlstate = DL_IDLE;
            dlbindack(q, mp, dlp->bind_req.dl_sap, NULL, 0, 0, 0);
        } else {
            dlerrorack(q, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
        }
		break;

    case DL_UNBIND_REQ:
        cmn_err(CE_CONT, "ack unbind req\n");
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
        cmn_err(CE_CONT, "ack attach req\n");
        if (drvp->md_dlstate == DL_UNATTACHED) {
            drvp->md_dlstate = DL_UNBOUND;
            dlokack(q, mp, DL_ATTACH_REQ);
        } else {
            dlerrorack(q, mp, DL_ATTACH_REQ, DL_OUTSTATE, 0);
        }
        break;

    case DL_DETACH_REQ:
        cmn_err(CE_CONT, "ack detach req\n");
        if (drvp->md_dlstate == DL_UNBOUND) {
            drvp->md_dlstate = DL_UNATTACHED;
            dlokack(q, mp, DL_DETACH_REQ);
        } else {
            dlerrorack(q, mp, DL_DETACH_REQ, DL_OUTSTATE, 0);
        }
        break;
		
	default:
		cmn_err(CE_NOTE, "error ack on %d", dlp->dl_primitive);
        dlerrorack(q, mp, dlp->dl_primitive, DL_BADPRIM, 0);
        break;
	}
}

static void zdethmod_inproto(queue_t *q, mblk_t *mp)
{
    ztdeth_mod_t *modp = q->q_ptr;
    mblk_t *m = mp;
	register struct ether_header *eh = NULL;
	uint_t off = 0, len, mlen;
    int handled = 0;

    for (; m != NULL; m = m->b_cont) {
        if ((DB_TYPE(m) == M_DATA) && (MBLKL(m) > 0)) {
            if (debug) cmn_err(CE_CONT, "We've got some data!\n");
            eh = (struct ether_header *)(m->b_rptr - sizeof(struct ether_header));
            len = MBLKL(m) - off;
            mlen = msgdsize(m);
            
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
            // @todo How about not calling htons a whole bunch of times!
            if (eh->ether_type == htons(ETH_P_ZTDETH) && len > sizeof(struct ztdeth_header)) {
                struct zt_span *span;
                register struct ztdeth_header *zh;
                zh = (struct ztdeth_header *)(m->b_rptr + off);
            
                span = ztdeth_getspan(eh->ether_shost.ether_addr_octet, zh->subaddr);
            
                if (span) {
                    /* send the data over, minus the zteth_header structure */
                    zt_dynamic_receive(span, (unsigned char *)zh + sizeof(struct ztdeth_header), len - sizeof(struct ztdeth_header));
                } else {
                    cmn_err(CE_CONT, "got zaptel frame, but can not find matching span.\n");
                }
            }
            handled = 1;
        }
    }
    if (handled == 1)
        freemsg(mp);
    else
        putnext(q, mp);
}

void zdethmodrput(queue_t *q, mblk_t *mp)
{
    ztdeth_drv_t *drvp = q->q_ptr;

    if (debug) cmn_err(CE_CONT, "Entered rput\n");

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
        if (debug) cmn_err(CE_CONT, "Got DATA\n");
		break;

	default:
		putnext(q, mp);
        break;
	}
}

struct zt_span *ztdeth_getspan(unsigned char *addr, unsigned short subaddr)
{
	unsigned long flags;
	struct ztdeth *z;
	struct zt_span *span = NULL;
	spin_lock_irqsave(&zlock, flags);
	z = zdevs;
	while(z) {
		if (!memcmp(addr, z->addr, ETHERADDRL) &&
			z->subaddr == subaddr)
			break;
		z = z->next;
	}
	if (z)
		span = z->span;
	spin_unlock_irqrestore(&zlock, flags);
	return span;
}

static int digit2int(char d)
{
	switch(d) {
	case 'F':
	case 'E':
	case 'D':
	case 'C':
	case 'B':
	case 'A':
		return d - 'A' + 10;
	case 'f':
	case 'e':
	case 'd':
	case 'c':
	case 'b':
	case 'a':
		return d - 'a' + 10;
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
		return d - '0';
	}
	return -1;
}

static int hex2int(char *s)
{
	int res;
	int tmp;
	/* Gotta be at least one digit */
	if (strlen(s) < 1)
		return -1;
	/* Can't be more than two */
	if (strlen(s) > 2)
		return -1;
	/* Grab the first digit */
	res = digit2int(s[0]);
	if (res < 0)
		return -1;
	tmp = res;
	/* Grab the next */
	if (strlen(s) > 1) {
		res = digit2int(s[1]);
		if (res < 0)
			return -1;
		tmp = tmp * 16 + res;
	}
	return tmp;
}

static void ztdeth_destroy(void *pvt)
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

static void *ztdeth_create(struct zt_span *span, char *addr)
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
			return NULL;
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
				return NULL;
			}
		} else {
			printk("TDMoE: Missing MAC address\n");
			kmem_free(z, sizeof(struct ztdeth));
			return NULL;
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
					return NULL;
				}
				mul *= 10;
				tmp3--;
			}
			z->subaddr = htons(sub);
		}
		z->span = span;

		printk("TDMoE: Added new interface for %s (addr=%s, subaddr=%d)\n", span->name, addr, ntohs(z->subaddr));
			
		spin_lock_irqsave(&zlock, flags);
		z->next = zdevs;
		zdevs = z;
		spin_unlock_irqrestore(&zlock, flags);
	}
	return z;
}

static int ztdeth_transmit(void *pvt, unsigned char *msg, int msglen)
{
	struct ztdeth *z;
	struct sk_buff *skb;
	struct ztdeth_header *zh;
	unsigned long flags;
	struct net_device *dev;
	unsigned char addr[ETHERADDRL];
	unsigned short subaddr; 

	spin_lock_irqsave(&zlock, flags);
	z = pvt;
	if (z->dev) {
		/* Copy fields to local variables to remove spinlock ASAP */
		dev = z->dev;
		memcpy(addr, z->addr, sizeof(z->addr));
		subaddr = z->subaddr;
		spin_unlock_irqrestore(&zlock, flags);
#if 0
		skb = dev_alloc_skb(msglen + dev->hard_header_len + sizeof(struct ztdeth_header) + 32);
		if (skb) {
			/* Reserve header space */
			skb_reserve(skb, dev->hard_header_len + sizeof(struct ztdeth_header));

			/* Copy message body */
			memcpy(skb_put(skb, msglen), msg, msglen);

			/* Throw on header */
			zh = (struct ztdeth_header *)skb_push(skb, sizeof(struct ztdeth_header));
			zh->subaddr = subaddr;

			/* Setup protocol and such */
			skb->protocol = __constant_htons(ETH_P_ZTDETH);
			skb->nh.raw = skb->data;
			skb->dev = dev;
			if (dev->hard_header)
				dev->hard_header(skb, dev, ETH_P_ZTDETH, addr, dev->dev_addr, skb->len);
			dev_queue_xmit(skb);
		}
#endif
		cmn_err(CE_CONT, "transmitting tdm data on subaddr %d\n", subaddr);
	}
	else
		spin_unlock_irqrestore(&zlock, flags);
	return 0;
}

static struct zt_dynamic_driver ztd_eth = {
	"eth",
	"Ethernet",
	ztdeth_create,
	ztdeth_destroy,
	ztdeth_transmit
};

