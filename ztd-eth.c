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

#define ETH_P_ZTDETH	0xd00d

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


// STREAMS STUFF

static int zdethdevopen(queue_t *, dev_t *, int, int, cred_t *);
static int zdethdevclose(queue_t *, int, cred_t *);

static struct qinit zdeth_rinit = {
	NULL, NULL, zdethdevopen, zdethdevclose, NULL, &zdeth_minfo, NULL
};

static struct qinit zdeth_winit = {
	zdethwput, NULL, NULL, NULL, NULL, &zdeth_minfo, NULL
};

struct streamtab zteth_dev_strtab = {
	&zdeth_rinit, &zdeth_winit
};


struct zt_span *ztdeth_getspan(unsigned char *addr, unsigned short subaddr);
static int zdeth_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
                    void **result);
static int zdeth_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int zdeth_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
void zdethmodrput(queue_t *q, mblk_t *mp);
void zdethmodwput(queue_t *q, mblk_t *mp);
static struct streamtab zdethmod_strtab;
static struct zt_dynamic_driver ztd_eth;

static struct cb_ops zdeth_ops = {
	nodev,		/* cb_open */
	nodev,		/* cb_close */
	nodev,		/* cb_strategy */
	nodev,		/* cb_print */
	nodev,		/* cb_dump */
	nodev,		/* cb_read */
	nodev,		/* cb_write */
	nodev,		/* cb_ioctl */
	nodev,		/* cb_devmap */
	nodev,		/* cb_mmap */
	nodev,		/* cb_segmap */
	nochpoll,	/* cb_chpoll */
	ddi_prop_op,/* cb_prop_op */
	&zdethmod_strtab,	/* cb_stream */
	D_MP		/* cb_flag */
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
	&mod_driverops, "Zaptel Dynamic Ethernet Driver", &zdeth_devops
};

// STREAMS module information
static int zdethmodopen(queue_t *, dev_t *, int, int, cred_t *);
static int zdethmodclose(queue_t *, int, cred_t *);

static struct qinit zdethmod_rinit = {
	(pfi_t)zdethmodrput, NULL, zdethmodopen, zdethmodclose,
	NULL, &zdeth_minfo, NULL
};

static struct qinit zdethmod_winit = {
	(pfi_t)zdethmodwput, NULL, NULL, NULL, NULL, &zdeth_minfo, NULL
};

static struct streamtab zdethmod_strtab = {
	&zdethmod_rinit, &zdethmod_winit
};

static struct fmodsw fsw = {
	"ztd-eth", &zdethmod_strtab, D_MP
};

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "Zaptel Dynamic Streams Driver", &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1,
	{ (void *)&modlstrmod, (void *)&modldrv, NULL }
};

/*
 * STREAMS device functions
 */
static dev_info_t *zdeth_dev_info;




static int zdeth_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);
	
	zdeth_dev_info = devi;
	zt_dynamic_register(&ztd_eth);
	
	return (ddi_create_minor_node(devi, "zdeth", S_IFCHR, 0, NULL, 0));
}

static int zdeth_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);
	
	ASSERT(devi == zdeth_dev_info);
	zt_dynamic_unregister(&ztd_eth);
	
	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

static int zdeth_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **res)
{
	int result = DDI_FAILURE;
	
	switch (infocmd)
	{
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
	
	if ((sflag & MODOPEN) != 0)
		result = ENXIO;
	if (result == 0)
		qprocson(q);
	return result;
}

static int zdethdevclose(queue_t *q, int flag, cred_t *crp)
{
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

static void zdethbind(queue_t *q)
{
	mblk_t *bind_mp;
	mblk_t *attach_mp;
	
	/*
	attach_mp = zdeth_dlpi_comm(DL_ATTACH_REQ, sizeof(dl_attach_req_t));
	if (attach_mp != NULL) {
		((dl_attach_req_t *)attach_mp->b_rptr)->dl_ppa = 0;
		if (debug) cmn_err(CE_CONT, "sending attach req.\n");
		zdeth_dlpi_send(q, attach_mp);
	}
	*/
	
	bind_mp = zdeth_dlpi_comm(DL_BIND_REQ, sizeof(dl_bind_req_t));
	if (bind_mp != NULL) {
		((dl_bind_req_t *)bind_mp->b_rptr)->dl_sap = 0xd00d;
		((dl_bind_req_t *)bind_mp->b_rptr)->dl_service_mode = DL_CLDLS;
		
		if (debug) cmn_err(CE_CONT, "sending sap bind.\n");
		zdeth_dlpi_send(q, bind_mp);
	}
}
	
static int zdethmodopen(queue_t *q, dev_t *devp, int oflag, int sflag, cred_t *crp)
{
	if (sflag != MODOPEN)
		return ENXIO;
	
	if (q->q_ptr != NULL)
		return 0;
	
	q->q_ptr = WR(q)->q_ptr = (void *)1;
	qprocson(q);
	
	zdethbind(q);
	
	if (debug) cmn_err(CE_CONT, "zdethmodopen\n");
	return 0;
}

static int zdethmodclose(queue_t *q, int flag, cred_t *crp)
{
	qprocsoff(q);
	q->q_ptr = WR(q)->q_ptr = NULL;
	if (debug) cmn_err(CE_CONT, "zdethmodclose\n");
	return 0;
}

/*
 * Kernel module functions
 */
int _init(void)
{
	return mod_install(&modlinkage);
}

int _fini(void)
{
	return mod_remove(&modlinkage);
}

int _info(struct modinfo *modinfop)
{
	return mod_info(&modlinkage, modinfop);
}

/*
 * STREAMS module functions
 */
void zdethmodwput(queue_t *q, mblk_t *mp)
{
	union DL_primitives *dl;
	
	switch (MTYPE(mp))
	{
		case M_PROTO:
		case M_PCPROTO:
			dl = (union DL_primitives *)mp->b_rptr;
			
			if ((MLEN(mp) < sizeof(dl_unitdata_req_t)) ||
			(dl->dl_primitive != DL_UNITDATA_REQ)) {
				break;
			}
			
			/*FALLTHROUGH*/
		case M_DATA:
			/* do something with the write request */
			cmn_err(CE_CONT,"zdethmodwput\n");
			break;
			
		default:
			break;
	}
	
	putnext(q, mp);
	return;
}

void dump_mac(const char *log, uchar_t *m)
{
	cmn_err(CE_CONT, "%s %02x:%02x:%02x:%02x:%02x:%02x", log,
	m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void zdethmod_rput_dlpi(queue_t *q, mblk_t *mp)
{
	dl_bind_ack_t	*dlba;
	dl_error_ack_t	*dlea;
	dl_ok_ack_t		*dloa;
	char			*err_str;
	
	if ((mp->b_wptr - mp->b_rptr) < sizeof(dloa->dl_primitive)) {
		putnext(q, mp);
		return;
	}
	
	dloa = (dl_ok_ack_t *)mp->b_rptr;
	dlea = (dl_error_ack_t *)dloa;
	switch (dloa->dl_primitive) {
		case DL_ERROR_ACK:
		switch (dlea->dl_error_primitive) {
			case DL_ATTACH_REQ:
				//zdeth_dlpi_done(DL_ATTACH_REQ);
				err_str = "DL_ATTACH_REQ";
				break;
			case DL_BIND_REQ:
				//zdeth_dlpi_done(DL_BIND_REQ);
				err_str = "DL_BIND_REQ";
				break;
			default:
				err_str = "?";
				break;
		}
		cmn_err(CE_CONT, "rput_dlpi: %s (%d) failed\n", err_str, (int)dlea->dl_error_primitive);
		break;
		case DL_OK_ACK:
		switch (dloa->dl_correct_primitive) {
			case DL_ATTACH_REQ:
			cmn_err(CE_CONT, "got DL_ATTACH_REQ OK\n");
			//zdeth_dlpi_done(DL_ATTACH_REQ);
			break;
		}
		break;
		case DL_BIND_ACK:
		dlba = (dl_bind_ack_t *)dloa;
		cmn_err(CE_CONT, "got DL_BIND_REQ OK\n");
		//zdeth_dlpi_done(DL_BIND_REQ);
		break;
		
		default:
		putnext(q, mp);
		return;
	}
	freemsg(mp);
}

void zdethmodrput(queue_t *q, mblk_t *mp)
{
	mblk_t *m = mp;
	register struct ether_header *eh = NULL;
	size_t off = 0, len, mlen;
	
	switch (MTYPE(mp))
	{	
		case M_PCPROTO:
		case M_PROTO:
			if ((mp->b_wptr - mp->b_rptr) < sizeof(dl_unitdata_ind_t) ||
				((dl_unitdata_ind_t *)mp->b_rptr)->dl_primitive
			!= DL_UNITDATA_IND) {
				zdethmod_rput_dlpi(q, mp);
				return;
			}
		case M_DATA:
			break;
		default:
			putnext(q, mp);
			return;
	}
	
	if (m == NULL) {
		putnext(q, mp);
		return;
	}
	cmn_err(CE_CONT, "zdethmodrput\n");
	
	/* Ok, ready? */
	//if (m->b_datap != NULL) {
	eh = (struct ether_header *)(m->b_rptr - sizeof(struct ether_header));
	//cmn_err(CE_CONT, "!zdeth1: type=%x\n", eh->ether_type);
	
	//dump_mac("shost = ", eh->ether_shost.ether_addr_octet);
	//dump_mac("dhost = ", eh->ether_dhost.ether_addr_octet);
	//}
	len = m->b_wptr - m->b_rptr - off;
	mlen = msgdsize(m);
	if (mlen == 0)
		mlen = m->b_wptr - m->b_rptr;
	mlen -= off;

		if (debug) {
			cmn_err(CE_CONT, "!frame src=%02x:%02x:%02x:%02x:%02x:%02x dst=%02x:%02x:%02x:%02x:%02x:%02x len=%d type=%x\n",
			eh->ether_shost.ether_addr_octet[0], eh->ether_shost.ether_addr_octet[1],
			eh->ether_shost.ether_addr_octet[2], eh->ether_shost.ether_addr_octet[3],
			eh->ether_shost.ether_addr_octet[4], eh->ether_shost.ether_addr_octet[5],
			eh->ether_dhost.ether_addr_octet[0], eh->ether_dhost.ether_addr_octet[1],
			eh->ether_dhost.ether_addr_octet[2], eh->ether_dhost.ether_addr_octet[3],
			eh->ether_dhost.ether_addr_octet[4], eh->ether_dhost.ether_addr_octet[5],
			len, eh->ether_type);
		}
		
	if (eh->ether_type == ETH_P_ZTDETH && len > sizeof(struct ztdeth_header)) {
		struct zt_span *span;
		register struct ztdeth_header *zh;
		zh = (struct ztdeth_header *)(m->b_rptr + off);
		
		span = ztdeth_getspan(eh->ether_shost.ether_addr_octet, zh->subaddr);

		if (span) {
			/* send the data over, minus the zteth_header structure */
			zt_dynamic_receive(span, (unsigned char *)zh + sizeof(struct ztdeth_header), len - sizeof(struct ztdeth_header));
		} else {
			cmn_err(CE_CONT, "!zdeth: got zaptel ethernet frame, but can not find span.\n");
		}
		freemsg(mp);
		return;
	}

	putnext(q, mp);
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

