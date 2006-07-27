#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "zaptel.h"

int main(int argc, char *argv[])
{
	int fd;
	int chan;
	if ((argc < 2) || (sscanf(argv[1], "%d", &chan) != 1)) {
		fprintf(stderr, "Usage: ztdiag <channel>\n");
		exit(1);
	}
	fd = open("/dev/zap/ctl");
	if (fd < 0) {
		perror("open(/dev/zap/ctl");
		exit(1);
	}
	if (ioctl(fd, ZT_CHANDIAG, &chan)) {
		perror("ioctl(ZT_CHANDIAG)");
		exit(1);
	}
	exit(0);
}
