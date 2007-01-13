/*
 * Dynamic Span Interface for Zaptel (Ethernet Interface via STREAMS module)
 *
 * Written by Joseph Benden <joe@thrallingpenguin.com>
 *
 * Copyright (C) 2006 Thralling Penguin LLC. All rights reserved.
 *
 */

#ifndef _ZTDETH_H
#define _ZTDETH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define ZTDIOC          0x4D504C00
#define ZTDIOC_SETID    (ZTDIOC | 1)
#define ZTDIOC_GETMUXID (ZTDIOC | 2)

#define ZTDETH_MAX_NAME 64

typedef struct ztdeth_strid_s {
    char si_name[ZTDETH_MAX_NAME];
    boolean_t si_ismcast;
} ztdeth_strid_t;

#ifdef __cplusplus
}
#endif

#endif /* _ZTDETH_H */

