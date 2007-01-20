
/*
 * This module implements the ZTD-ETH DLPI-based prototype user space control
 * program on Solaris.
 *
 * Usage:
 *	zapadm list
 *	    - List all of the ZTD-ETH streams in the system
 *	zapadm plumb [-t] <interface>
 *	    - Configure ZTD-ETH on given interface, such as "hme0."  The -t flag
 *	      (tunnel) causes the interface to be treated as an owned
 *	      interface for bridging purposes.
 *	zapadm unplumb <interface>
 *	    - Remove ZTD-ETH from given interface, such as "hme0."
 */

#include <stdio.h>
#include <unistd.h>
#include <stropts.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dlpi.h>
#include <sys/strsun.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>

#include "ztd-eth.h"

#define	ETHERTYPE_ZAPTEL	0xD00D

static int
open_ztdeth(void)
{
	int fd;

	if ((fd = open("/dev/zap/ztdeth", O_RDWR)) == -1)
		perror("zapadm: /dev/zap/ztdeth");
	return (fd);
}

static int
open_dev(const char *dev)
{
	int fd;

	if ((fd = open(dev, O_RDWR)) == -1) {
		perror(dev);
		return (-1);
	}
	while (ioctl(fd, I_FIND, "ztd-eth") == 1)
		(void) ioctl(fd, I_POP, 0);
	return (fd);
}

static boolean_t
dlpiput(int fd, void *buf, size_t bufsize, int flags)
{
	struct strbuf ctl;

	(void) memset(&ctl, 0, sizeof (ctl));
	ctl.len = bufsize;
	ctl.buf = buf;
	if (putmsg(fd, &ctl, NULL, flags) == -1) {
		perror("zapadm: putmsg");
		return (B_FALSE);
	}
	return (B_TRUE);
}

static ssize_t
dlpiget(int fd, void *buf, size_t bufsize)
{
	struct strbuf ctl, dat;
	char dummy[1024];
	int flags;

	(void) memset(&ctl, 0, sizeof (ctl));
	(void) memset(&dat, 0, sizeof (dat));
	ctl.maxlen = bufsize;
	ctl.buf = buf;
	dat.maxlen = sizeof (dummy);
	dat.buf = dummy;
	flags = 0;
	(void) alarm(5);
	if (getmsg(fd, &ctl, &dat, &flags) == -1) {
		perror("zapadm: getmsg");
		(void) alarm(0);
		return (-1);
	}
	(void) alarm(0);
	return (ctl.len);
}

/*
 * Note that this is used after DL_BIND_REQ, and thus it needs to handle
 * inbound data as well, by discarding it.
 */
static boolean_t
dlpiokack(int fd, t_uscalar_t prim)
{
	dl_ok_ack_t ack;
	ssize_t retv;

	for (;;) {
		if ((retv = dlpiget(fd, &ack, sizeof (ack))) == -1)
			return (B_FALSE);
		if (retv < sizeof (ack))
			continue;
		if (ack.dl_primitive == DL_ERROR_ACK) {
			(void) fprintf(stderr,
			    "zapadm: got DL_ERROR_ACK for %x\n",
			    ack.dl_correct_primitive);
			return (B_FALSE);
		}
		if (ack.dl_primitive == DL_OK_ACK)
			break;
	}
	if (ack.dl_correct_primitive != prim) {
		(void) fprintf(stderr,
		    "zapadm: ack for unexpected primitive %x, wanted %x\n",
		    ack.dl_correct_primitive, prim);
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
dlinforeq(int fd, void *buf, size_t bufsize)
{
	dl_info_req_t req;
	ssize_t retv;
	dl_info_ack_t *dia;

	(void) memset(&req, 0, sizeof (req));
	req.dl_primitive = DL_INFO_REQ;
	if (!dlpiput(fd, &req, sizeof (req), RS_HIPRI))
		return (B_FALSE);
	if ((retv = dlpiget(fd, buf, bufsize)) == -1)
		return (B_FALSE);
	dia = buf;
	if (retv < sizeof (*dia) || dia->dl_primitive != DL_INFO_ACK) {
		(void) fprintf(stderr,
		    "zapadm: unexpected response to DL_INFO_REQ\n");
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
dlattachreq(int fd, t_uscalar_t ppa)
{
	dl_attach_req_t req;

	(void) memset(&req, 0, sizeof (req));
	req.dl_primitive = DL_ATTACH_REQ;
	req.dl_ppa = ppa;
	if (!dlpiput(fd, &req, sizeof (req), 0))
		return (B_FALSE);
	return (dlpiokack(fd, DL_ATTACH_REQ));
}

static boolean_t
dlbindreq(int fd, t_uscalar_t sap)
{
	dl_bind_req_t req;
	struct {
		dl_bind_ack_t ack;
		char buf[1024];
	} ack;
	ssize_t retv;

	(void) memset(&req, 0, sizeof (req));
	req.dl_primitive = DL_BIND_REQ;
	req.dl_sap = sap;
	req.dl_service_mode = DL_CLDLS;
	if (!dlpiput(fd, &req, sizeof (req), 0))
		return (B_FALSE);
	if ((retv = dlpiget(fd, &ack, sizeof (ack))) == -1)
		return (B_FALSE);
	if (retv < sizeof (ack.ack) || ack.ack.dl_primitive != DL_BIND_ACK) {
		(void) fprintf(stderr,
		    "zapadm: expected DL_BIND_ACK, but got %x length %d\n",
		    ack.ack.dl_primitive, retv);
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
dlsapmulti(int fd)
{
	dl_promiscon_req_t req;

	(void) memset(&req, 0, sizeof (req));
	req.dl_primitive = DL_PROMISCON_REQ;
	req.dl_level = DL_PROMISC_SAP;
	if (!dlpiput(fd, &req, sizeof (req), 0))
		return (B_FALSE);
	return (dlpiokack(fd, DL_PROMISCON_REQ));
}

static boolean_t
strioctl(int fd, uint_t cmd, void *buf, size_t buflen)
{
	struct strioctl ic;

	ic.ic_cmd = cmd;
	ic.ic_timout = 0;
	ic.ic_len = buflen;
	ic.ic_dp = buf;
	if (ioctl(fd, I_STR, &ic) == -1)
		return (B_FALSE);
	else
		return (B_TRUE);
}

static boolean_t
driver_attach(int dfd, t_uscalar_t dlstyle, t_uscalar_t ppa, const char *drv,
    boolean_t ismcast, boolean_t istunnel)
{
	int mfd;
	struct {
		dl_info_ack_t ack;
		char buf[1024];
	} ack;
	ztdeth_strid_t strid;

	/*
	 * Push the ztd-eth module on top.  We do this before anything else so
	 * that the module can grab the information it needs on its rput side.
	 */
	if (ioctl(dfd, I_PUSH, "ztd-eth") != 0) {
		perror("zapadm: I_PUSH ZTD-ETH");
		return (B_FALSE);
	}

	/*
	 * Get the driver information.  If the driver isn't really DLPI (i.e.,
	 * the user specified something bogus), then this could time out.
	 */
	if (!dlinforeq(dfd, &ack, sizeof (ack)))
		return (B_FALSE);

	/*
	 * Check the DLPI Style.  If it's Style 2, we need to attach to the
	 * instance first.
	 */
	if (dlstyle != ack.ack.dl_provider_style) {
		(void) fprintf(stderr,
		    "zapadm: unexpected style %x on %s\n",
		    ack.ack.dl_provider_style, drv);
		return (B_FALSE);
	}
	if (dlstyle == DL_STYLE2 && !dlattachreq(dfd, ppa))
		return (B_FALSE);

	/*
	 * Now that we're attached to the right instance, get the driver
	 * information again.
	 */
	if (!dlinforeq(dfd, &ack, sizeof (ack)))
		return (B_FALSE);

	/*
	 * Check out the medium.  We support only Ethernet-type interfaces
	 * here.  (PPP must be plumbed by pppd itself, not this utility.)
	 */
	switch (ack.ack.dl_mac_type) {
	case DL_CSMACD:
	case DL_ETHER:
	case DL_100VG:
	case DL_ETH_CSMA:
	case DL_100BT:
		break;
	default:
		(void) fprintf(stderr, "zapadm: unknown mac type %x on %s\n",
		    ack.ack.dl_mac_type, drv);
		return (B_FALSE);
	}

	/*
	 * Now bind to the correct SAP.  We know the right SAP value to use
	 * because we support only Ethernet.
	 */
	if (!dlbindreq(dfd, ETHERTYPE_ZAPTEL))
		return (B_FALSE);

	/*
	 * If it's a tunnel, then we need to turn on SAP multicast mode.
	 */
	if (istunnel && !dlsapmulti(dfd))
		return (B_FALSE);

	/*
	 * Use a private ioctl to tell the stream its name.
	 */
	(void) strlcpy(strid.si_name, drv, sizeof (strid.si_name));
	strid.si_ismcast = ismcast;
	if (!strioctl(dfd, ZTDIOC_SETID, &strid, sizeof (strid))) {
		(void) fprintf(stderr, "zapadm: internal failure\n");
		return (B_FALSE);
	}

	/*
	 * Open up the ztd-eth driver and link this new stream into the kernel.
	 */
	if ((mfd = open_ztdeth()) == -1)
		return (B_FALSE);
	if (ioctl(mfd, I_PLINK, dfd) == -1) {
		perror("zapadm: I_PLINK");
		(void) close(mfd);
		return (B_FALSE);
	}
	(void) close(mfd);

	return (B_TRUE);
}

static boolean_t
unplumb_instance(const char *drv, boolean_t ismcast)
{
	int mfd;
	int muxid;
    ztdeth_strid_t strid;

	if ((mfd = open_ztdeth()) == -1)
		return (B_FALSE);

	/*
	 * Use a private ioctl to get the multiplexor ID back from the kernel.
	 */
	(void) strlcpy(strid.si_name, drv, sizeof (strid.si_name));
	strid.si_ismcast = ismcast;
	if (!strioctl(mfd, ZTDIOC_GETMUXID, &strid, sizeof (strid))) {
		(void) close(mfd);
		return (B_FALSE);
	}
	muxid = *(int *)&strid;
	if (ioctl(mfd, I_PUNLINK, muxid) == -1) {
		perror("zapadm: I_PUNLINK");
		(void) close(mfd);
		return (B_FALSE);
	}

	(void) close(mfd);
	return (B_TRUE);
}

static void
alarm_handler(int signo)
{
	(void) fprintf(stderr, "zapadm: non-DLPI device specified\n");
	exit(1);
}

static int
ztdeth_plumb(int argc, char **argv)
{
	int dfd;
	t_uscalar_t dlstyle, ppa;
	const char *intf;
	char drv[MAXPATHLEN], *cp;
	int chr;
	boolean_t istunnel = B_FALSE;
	struct sigaction act;

	while ((chr = getopt(argc, argv, "t")) != -1) {
		switch (chr) {
		case 't':
			istunnel = B_TRUE;
			break;
		default:
			return (1);
		}
	}

	if (optind != argc - 1) {
		(void) fprintf(stderr, "zapadm: unknown plumb arguments\n");
		return (1);
	}
	intf = argv[optind];

	(void) memset(&act, 0, sizeof (act));
	act.sa_handler = alarm_handler;
	act.sa_flags = SA_RESETHAND;
	(void) sigaction(SIGALRM, &act, NULL);

	/*
	 * Try Style 1 first.
	 */
	dlstyle = DL_STYLE1;
	ppa = 0;
	(void) snprintf(drv, sizeof (drv), "/dev/%s", intf);
	if ((dfd = open_dev(drv)) == -1) {
		if (errno != ENOENT) {
			perror(drv);
			return (1);
		}
		/*
		 * If a Style 1 node doesn't exist, we have to try Style 2.
		 */
		cp = drv + strlen(drv);
		if (!isdigit(*--cp)) {
			(void) fprintf(stderr,
			    "zapadm: unknown interface %s\n", intf);
			return (1);
		}
		while (isdigit(*cp))
			cp--;
		ppa = atoi(cp++);
		*cp = '\0';
		if ((dfd = open_dev(drv)) == -1) {
			perror(drv);
			return (1);
		}
		dlstyle = DL_STYLE2;
	}

	if (!driver_attach(dfd, dlstyle, ppa, intf, B_FALSE, istunnel))
		return (1);

	/* Tunnel ingress (bridging) interfaces need just one stream */
	if (istunnel)
		return (0);

	if ((dfd = open_dev(drv)) == -1) {
		perror(drv);
		(void) unplumb_instance(intf, B_FALSE);
		return (1);
	}
	if (!driver_attach(dfd, dlstyle, ppa, intf, B_TRUE, istunnel)) {
		(void) unplumb_instance(intf, B_FALSE);
		return (1);
	}
	return (0);
}

static int
ztdeth_unplumb(int argc, char **argv)
{
	boolean_t did_mcast, did_ucast;

	if (argc != 2) {
		(void) fprintf(stderr, "zapadm: unknown unplumb arguments\n");
		return (1);
	}
	did_mcast = unplumb_instance(argv[1], B_TRUE);
	did_ucast = unplumb_instance(argv[1], B_FALSE);
	/*
	 * It's ok for the stream to be plumbed for unicast only.  That
	 * represents tunnel ingress (bridging) streams.
	 */
	if (did_ucast)
		return (0);
	/* Not plumbed for unicast is an error */
	if (did_mcast) {
		(void) fprintf(stderr,
		    "zapadm: %s was not plumbed for unicast\n", argv[1]);
	} else {
		(void) fprintf(stderr, "zapadm: %s is not plumbed\n",
		    argv[1]);
	}
	return (1);
}

typedef struct cmd_list_s {
	const char *cmd_name;
	int (*cmd_func)(int, char **);
} cmd_list_t;

static const cmd_list_t cmd_list[] = {
	{ "plumb", ztdeth_plumb },
	{ "unplumb", ztdeth_unplumb },
	{ NULL, NULL }
};

static void
usage(void)
{
	(void) fprintf(stderr, "usage: zapadm <subcommand> <args...>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	const cmd_list_t *cmdp;

	if (argc < 2)
		usage();
	for (cmdp = cmd_list; cmdp->cmd_name != NULL; cmdp++) {
		if (strcmp(argv[1], cmdp->cmd_name) == 0)
			return ((*cmdp->cmd_func)(argc - 1, argv + 1));
	}
	(void) fprintf(stderr, "zapadm: subcommand '%s' is unknown\n", argv[1]);
	return (1);
}

