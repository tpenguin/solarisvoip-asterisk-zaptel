#ifndef __ZT_COMPAT_H
#define __ZT_COMPAT_H

#ifdef SOLARIS
#include <sys/varargs.h>

#ifndef max
#define max(x, y)	(((x) > (y)) ? (x) : (y))
#endif
#define abs(a)		max(a, 0-a)
typedef	kmutex_t	spinlock_t;
typedef unsigned long	dma_addr_t;

#define HZ 		100
#define jiffies		(gethrtime())

#define PCI_ANY_ID 	-1

typedef	void *		kernel_ulong_t;

struct pci_device_id {
        unsigned int vendor, device;
        unsigned int subvendor, subdevice;
        unsigned int class, class_mask;
        kernel_ulong_t driver_data;
};

#define inb(a)          ddi_io_get8(wc->devhandle, a)
#define outb(b,a)       ddi_io_put8(wc->devhandle, (uint8_t *)a, b)
#define inl(a)          ddi_io_get32(wc->devhandle, (unsigned int *)a)

#ifdef __BIG_ENDIAN
#define outl(b,a)       { ddi_io_put8(wc->devhandle, a+0, (b & 255)); \
                          ddi_io_put8(wc->devhandle, a+1, ((b>>8) & 255)); \
                          ddi_io_put8(wc->devhandle, a+2, ((b>>16) & 255)); \
                          ddi_io_put8(wc->devhandle, a+3, ((b>>24) & 255)); }
#else
#define outl(b,a)       ddi_io_put32(wc->devhandle, (uint32_t)a, b)
#endif

#define copy_from_user(dst,src,c) ddi_copyin(src,dst,c,mode)
#define copy_to_user(src,dst,c) ddi_copyout(src,dst,c,mode)
#define get_user(dst,src) ddi_copyin(src, &dst, sizeof(int), mode) 

#define spin_lock_init(a) mutex_init(a, NULL, MUTEX_DRIVER, (void *)&wc->iblock)
#define spin_lock_irqsave(a, b) mutex_enter(a)
#define spin_unlock_irqrestore(a, b) mutex_exit(a)
#define init_waitqueue_head(a) cv_init(a, NULL, CV_DRIVER, NULL)

#define set_current_state(a)
#define schedule_timeout(t) drv_usecwait(t * 20000)

#define KERN_INFO
static inline void printk(char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vcmn_err(CE_CONT, fmt, ap);
  va_end(ap);
}

#define memset(a,b,c) compat_memset(a,b,c)
static inline void compat_memset(void *addr, unsigned char c, size_t s)
{
  int i;
  unsigned char *caddr = addr;

  if (c == 0)
  {
    bzero(addr, s);
    return;
  }

  for(i = 0; i<s; i++)
  {
    caddr[i] = c;
  }
}

#define memcpy(dst, src, s) bcopy(src, dst, s)

#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT


#endif

#endif
