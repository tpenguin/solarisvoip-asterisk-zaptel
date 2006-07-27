#include <sys/stream.h>
#include <sys/dlpi.h>
#include <sys/bufmod.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <net/if.h>
#include <errno.h>

#define		MAXDLBUF	32768
long databuf[MAXDLBUF];

static int ifopen(const char *ifname)
{
	int ifd;

	if (ifname[0] == '/') {
		ifd = open(ifname, O_RDWR);
		if (ifd > 0 || errno != ENOENT)
			return (ifd);
	}
	return -1;
}

strgetmsg(fd, ctlp, datap, flagsp, caller)
int	fd;
struct	strbuf	*ctlp, *datap;
int	*flagsp;
char	*caller;
{
	int	rc;
	static	char	errmsg[80];

	/*
	 * Set flags argument and issue getmsg().
	 */
	*flagsp = 0;
	if ((rc = getmsg(fd, ctlp, datap, flagsp)) < 0) {
		fprintf(stderr, "%s:  getmsg", caller);
	}

}

expecting(prim, dlp)
int	prim;
union	DL_primitives	*dlp;
{
	if (dlp->dl_primitive != (u_long)prim) {
		fprintf(stderr, "unexpected dlprim error\n");
		exit(1);
	}
}

strioctl(fd, cmd, timout, len, dp)
int	fd;
int	cmd;
int	timout;
int	len;
char	*dp;
{
	struct	strioctl	sioc;
	int	rc;

	sioc.ic_cmd = cmd;
	sioc.ic_timout = timout;
	sioc.ic_len = len;
	sioc.ic_dp = dp;
	rc = ioctl(fd, I_STR, &sioc);

	if (rc < 0)
		return (rc);
	else
		return (sioc.ic_len);
}

dlattachreq(fd, ppa)
int	fd;
u_long	ppa;
{
	dl_attach_req_t	attach_req;
	struct	strbuf	ctl;
	int	flags;

	attach_req.dl_primitive = DL_ATTACH_REQ;
	attach_req.dl_ppa = ppa;

	ctl.maxlen = 0;
	ctl.len = sizeof (attach_req);
	ctl.buf = (char *) &attach_req;

	flags = 0;

	if (putmsg(fd, &ctl, (struct strbuf*) NULL, flags) < 0)
		fprintf(stderr, "dlattachreq:  putmsg");
}

dlokack(fd, bufp)
int	fd;
char	*bufp;
{
	union	DL_primitives	*dlp;
	struct	strbuf	ctl;
	int	flags;

	ctl.maxlen = MAXDLBUF;
	ctl.len = 0;
	ctl.buf = bufp;

	strgetmsg(fd, &ctl, (struct strbuf*)NULL, &flags, "dlokack");

	dlp = (union DL_primitives *) ctl.buf;

	expecting(DL_OK_ACK, dlp);

	if (ctl.len < sizeof (dl_ok_ack_t))
		fprintf(stderr, "dlokack:  response ctl.len too short:  %d", ctl.len);

	if (flags != RS_HIPRI)
		fprintf(stderr, "dlokack:  DL_OK_ACK was not M_PCPROTO");

	if (ctl.len < sizeof (dl_ok_ack_t))
		fprintf(stderr, "dlokack:  short response ctl.len:  %d", ctl.len);
}

dlbindreq(fd, sap, max_conind, service_mode, conn_mgmt, xidtest)
int	fd;
u_long	sap;
u_long	max_conind;
u_long	service_mode;
u_long	conn_mgmt;
u_long	xidtest;
{
	dl_bind_req_t	bind_req;
	struct	strbuf	ctl;
	int	flags;

	bind_req.dl_primitive = DL_BIND_REQ;
	bind_req.dl_sap = sap;
	bind_req.dl_max_conind = max_conind;
	bind_req.dl_service_mode = service_mode;
	bind_req.dl_conn_mgmt = conn_mgmt;
	bind_req.dl_xidtest_flg = xidtest;

	ctl.maxlen = 0;
	ctl.len = sizeof (bind_req);
	ctl.buf = (char *) &bind_req;

	flags = 0;

	if (putmsg(fd, &ctl, (struct strbuf*) NULL, flags) < 0)
		fprintf(stderr, "dlbindreq:  putmsg");
}

dlbindack(fd, bufp)
int	fd;
char	*bufp;
{
	union	DL_primitives	*dlp;
	struct	strbuf	ctl;
	int	flags;

	ctl.maxlen = MAXDLBUF;
	ctl.len = 0;
	ctl.buf = bufp;

	strgetmsg(fd, &ctl, (struct strbuf*)NULL, &flags, "dlbindack");

	dlp = (union DL_primitives *) ctl.buf;

	expecting(DL_BIND_ACK, dlp);

	if (flags != RS_HIPRI)
		fprintf(stderr, "dlbindack:  DL_OK_ACK was not M_PCPROTO");

	if (ctl.len < sizeof (dl_bind_ack_t))
		fprintf(stderr, "dlbindack:  short response ctl.len:  %d", ctl.len);
}


int main(int argc, char *argv[])
{
	long buf[MAXDLBUF];
	int flags, mrwtmp, i;
	char *p, *limp;
	struct strbuf data;
	
	if (argc < 2) {
		fprintf(stderr, "%s [interface] [ppa]\n", argv[0]);
		return 1;
	}
	
	int ppa = (int)atol(argv[2]);
	int ifd = ifopen(argv[1]);
	if (ifd < 0)
		return 1;

	/* attach */
	dlattachreq(ifd, ppa);
	dlokack(ifd, buf);
	
	dlbindreq(ifd, 0xd00d, 0, DL_CLDLS, 0, 0);
	dlbindack(ifd, buf);
	
	if (ioctl(ifd, I_PUSH, "ztd-eth") < 0) {
		fprintf(stderr, "push ztd-eth failed.\n");
	}
	
	if (ioctl(ifd, I_FLUSH, FLUSHR) < 0) {
		fprintf(stderr, "flush failed.\n");
	}

	/*
	 * Read packets.
	 */

	data.buf = (char *) databuf;
	data.maxlen = MAXDLBUF;
	data.len = 0;

	/* Here's the deal:  I had some problems with the bufmod code, but
	   I think it's working now.  I don't know a whole lot about the
	   whole DLPI interface, so I can't be sure there aren't any
	   oversights here.  It seems to be working now, but I have not had
	   the time to do extensive testing.
	   I know for certain that packets will be dropped on a busy network
	   if I don't use bufmod.  That problem should not occur when using
	   bufmod, but like I said, I may have overlooked something. */

	while (((mrwtmp=getmsg(ifd, NULL, &data, &flags))==0) ||
		 (mrwtmp==MOREDATA) || (mrwtmp=MORECTL)) {
		p = data.buf;
		limp = p + data.len;

	/* This is the ugliest piece of commented out crap that I've ever
	   done.  Ignore it.  Someday it will go away. */
		if (data.len > 0) {
				/* Display hex data if we want */
 				for (i = 0; i < data.len; i++)
					printf("%02x ", data.buf[i]);
				printf("\n"); 
		 }
		data.len = 0;
	} /* while */
	
	close(ifd);
	return 0;
}

