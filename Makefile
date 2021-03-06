#
# (C) 2006 Thralling Penguin LLC. All rights reserved.
#
CC	= gcc
LD	= /usr/ccs/bin/ld
VER	= 1.0
REV	= $(shell date +'%Y.%m.%d.%H.%M')
PKGMK	= pkgmk -o
PKGADD	= pkgadd
PKGRM	= pkgrm
PKGTRANS= pkgtrans
MKDIR	= mkdir -p
ISA	= $(shell uname -p)
ISABITS = $(shell isainfo -b)
ARCH	= $(shell pkgparam SUNWcsr ARCH)
PROC	= $(shell uname -m)
VERSION	= $(shell uname -r)
PKGARCHIVE = $(shell pwd)/$(shell uname -s)-$(shell uname -r).$(shell uname -m)
PACKAGES=SVzaptel SVztdummy SVwctdm
MODULES = libtonezone.so libpri zaptel ztdummy wctdm wcte11xp ztcfg zttest ztdiag 
ILOC = /platform/$(PROC)/kernel/drv
ifeq ($(ISABITS),64)
	ifeq ($(ISA),i386)
		ILOCDRV = $(ILOC)/amd64
		PKGARCH=x64
	else
		ILOCDRV = $(ILOC)/sparcv9
		PKGARCH=sparc
	endif
else
	ILOCDRV = $(ILOC)
	ifeq ($(PROC),sun4u)
		PKGARCH=sparc
	else
		PKGARCH=x86
	endif
endif

ifneq ($(wildcard /usr/include/newt.h),)
	MODULES+=zttool
	echo 'f none opt/sbin/zttool=../zttool 0755 root bin' >>SVzaptel/prototype_com
endif
MODULES+= timertest ztmonitor package

export VER REV ISA PKGMK PKGADD PKGRM MKDIR ARCH VERSION PKGARCHIVE PKGTRANS PKGARCH

#Tell gcc to optimize the code
#
OPTIMIZE= # -O6
ifeq ($(PROC),sun4u)
OPTIMIZE+=-mcpu=v9
endif

#Include debug and macro symbols in the executables (-g) and profiling info (-pg)
#
DEBUG=-g  # -g -pg -DCONFIG_ZAPATA_DEBUG

export OPTIMIZE DEBUG

CFLAGS= $(DEBUG) -DDEBUG -DSOLARIS -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -DECHO_CAN_MARK2 -I. $(OPTIMIZE)
ifeq ($(ISABITS),64)
	CFLAGS+=-m64
	LDFLAGS+=-64 
endif
ifeq ($(ISA),i386)
	ifeq ($(ISABITS),64)
		CFLAGS+=-mcmodel=kernel
	endif
	CFLAGS+=-mno-red-zone -ffreestanding -nodefaultlibs
else
	# -fno-pic -mcmodel=medlow
	CFLAGS+=-mcmodel=medlow -mno-fpu -fno-pic -fno-strict-aliasing -ffreestanding -nodefaultlibs -D__BIG_ENDIAN
endif

all:	$(MODULES)

package: zaptel ztdummy
	echo "## Making $(PACKAGES)"; 
	for P in $(PACKAGES); do \
		echo "## Making $$P"; \
		( cd $$P; $(MAKE) $(TARGET) ); \
	done

clean:	
	( cd libpri; $(MAKE) clean )
	rm -f *.o *.so
	rm -f zaptel ztdummy ztcfg zttest timertest
	rm -rf $(PKGARCHIVE)

libpri: zaptel
	( cd libpri; $(MAKE) )

zaptel:	zaptel.o
	$(LD) $(LDFLAGS) -r -o zaptel zaptel.o

ztdynamic: ztdynamic.o
	$(LD) $(LDFLAGS) -r -o ztdynamic ztdynamic.o

ztd-eth: ztd-eth.o
	$(LD) $(LDFLAGS) -r -o ztd-eth ztd-eth.o

zapadm: zapadm.c
	$(CC) -g -o zapadm zapadm.c

ztdummy: ztdummy.o
	$(LD) $(LDFLAGS) -r -o ztdummy ztdummy.o

wcfxo: wcfxo.o
	$(LD) $(LDFLAGS) -r -o wcfxo wcfxo.o

wctdm: wctdm.o
	$(LD) $(LDFLAGS) -r -o wctdm wctdm.o

wcte11xp: wcte11xp.o
	$(LD) $(LDFLAGS) -r -o wcte11xp wcte11xp.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

TZOBJS=tonezone.o zonedata.o

tonezone.o: tonezone.c tonezone.h
	$(CC) $(DEBUG) -DSOLARIS -DECHO_CAN_MARK2 -I. $(OPTIMIZE) -fPIC -c -o tonezone.o -DBUILDING_TONEZONE tonezone.c

zonedata.o: zonedata.c 
	$(CC) $(DEBUG) -DSOLARIS -DECHO_CAN_MARK2 -I. $(OPTIMIZE) -fPIC -c -o zonedata.o -DBUILDING_TONEZONE zonedata.c

libtonezone.a: $(TZOBJS)
	ar rcs libtonezone.a $(TZOBJS)

libtonezone.so: $(TZOBJS)
	$(CC) -shared -fPIC -o $@ $(TZOBJS)

ztcfg.o: ztcfg.c
	$(CC) $(DEBUG) -DSOLARIS -DECHO_CAN_MARK2 -I. $(OPTIMIZE) -c -DBUILDING_TONEZONE -DZAPTEL_CONFIG=\"/opt/etc/zaptel.conf\" ztcfg.c

ztcfg: ztcfg.o
	$(CC) -o ztcfg ztcfg.o -L. -R/opt/lib -L/opt/lib -ltonezone -lm

zttest.o: zttest.c
	$(CC) $(DEBUG) -DSOLARIS -DECHO_CAN_MARK2 -I. $(OPTIMIZE) -c -DBUILDING_TONEZONE zttest.c

zttest: zttest.o
	$(CC) -o zttest zttest.o -L. 

ztdiag.o: ztdiag.c
	$(CC) $(DEBUG) -DSOLARIS -DECHO_CAN_MARK2 -I. $(OPTIMIZE) -c -DBUILDING_TONEZONE ztdiag.c

ztdiag: ztdiag.o
	$(CC) -o ztdiag ztdiag.o -L.

timertest.o: timertest.c
	$(CC) $(DEBUG) -DSOLARIS -I. $(OPTIMIZE) -c timertest.c

timertest: timertest.o
	$(CC) -o timertest timertest.o

zttool.o: zttool.c
	$(CC) $(DEBUG) -DSOLARIS $(OPTIMIZE) -I. -c -I/opt/csw/include -I/usr/include zttool.c

zttool: zttool.o
	$(CC) -o zttool zttool.o -L/opt/csw/lib -R/opt/csw/lib -lnewt

ztmonitor.o: ztmonitor.c
	$(CC) $(DEBUG) -DSOLARIS $(OPTIMIZE) -c -I. ztmonitor.c

ztmonitor: ztmonitor.o
	$(CC) -o ztmonitor ztmonitor.o


# Install zaptel, dynamic, and the ethernet driver

install: zaptel ztdynamic ztd-eth zapadm
	rm -f $(ILOCDRV)/zaptel
	rm -f $(ILOCDRV)/ztdynamic
	cp zaptel $(ILOCDRV)/zaptel
	cp zaptel.conf $(ILOC)
	cp ztdynamic $(ILOCDRV)/ztdynamic
	cp ztdynamic.conf $(ILOC)
	cp ztd-eth $(ILOCDRV)
	cp ztd-eth.conf $(ILOC)
	-rem_drv ztd-eth
	-rem_drv ztdynamic
	-rem_drv zaptel
	add_drv -v -f zaptel
	add_drv -v -f ztdynamic
	add_drv -v -f ztd-eth

installdyn: ztdynamic
	rm -f /usr/kernel/drv/sparcv9/ztdynamic
	ln -s /tmp/ztdynamic /usr/kernel/drv/sparcv9/ztdynamic
	cp ztdynamic /tmp
	-rem_drv ztdynamic
	add_drv -v -f ztdynamic

installeth: ztd-eth
	rm -f /usr/kernel/drv/sparcv9/ztd-eth
	ln -s /tmp/ztd-eth /usr/kernel/drv/sparcv9/ztd-eth
	cp ztd-eth /tmp
	-rem_drv ztd-eth
	add_drv -v -f ztd-eth
	ifconfig eri0 modinsert ztd-eth@2

installfxo: wcfxo
	rm -f /kernel/drv/sparcv9/wcfxo
	ln -s /tmp/wcfxo /kernel/drv/sparcv9/wcfxo
	cp wcfxo /tmp/
	-rem_drv wcfxo
	add_drv -i '"pci8086,3"' -v -f wcfxo

installtdm: wctdm
	rm -f /kernel/drv/sparcv9/wctdm
	ln -s /tmp/wctdm /kernel/drv/sparcv9/wctdm
	cp wctdm /tmp/
	-rem_drv wctdm
	add_drv -i '"pcib100,1" "pcib100,3"' -v -f wctdm

debug: zaptel
	rm -f /kernel/drv/sparcv9/zaptel
	ln -s /tmp/zaptel /kernel/drv/sparcv9/zaptel
	-rem_drv zaptel
	cp zaptel /tmp
	add_drv -f -v zaptel
	
installte11xp: wcte11xp
	rm -f /usr/kernel/drv/sparcv9/wcte11xp
	ln -s /tmp/wcte11xp /usr/kernel/drv/sparcv9/wcte11xp
	cp wcte11xp /tmp/
	-rem_drv wcte11xp
	add_drv -i '"pci6159,1" "pci795e,1" "pci797e,1" "pci79de,1" "pci79fe,1"' -v -f wcte11xp
