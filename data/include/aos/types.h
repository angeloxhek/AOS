#ifndef AOS_TYPES_H
#define AOS_TYPES_H

#include <stdint.h>
#include <limits.h>

#ifndef offsetof
    #define offsetof(TYPE, MEMBER)  __builtin_offsetof(TYPE, MEMBER)
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
    #ifdef __SIZE_TYPE__
        typedef __SIZE_TYPE__ size_t;
    #else
        typedef unsigned long size_t;
    #endif
#endif

#ifndef UNUSED
    #define UNUSED(x) (void)(x)
#endif

#undef NULL
#ifdef __cplusplus
    #define NULL __null
#else
    #define NULL ((void *)0)
#endif

typedef uint32_t apid_t;

#define SYS_RES_OK                    0
#define SYS_RES_INVALID              -1
#define SYS_RES_NO_PERM              -2
#define SYS_RES_ALREADY              -3
#define SYS_RES_DRV_ERR              -4
#define SYS_RES_QUEUE_EMPTY          -5
#define SYS_RES_RESERVED1            -6
#define SYS_RES_RANGE                -7
#define SYS_RES_NOTFOUND             -8
#define SYS_RES_KERNEL_ERR          -99

#define DRV_ERR_OK                    0
#define DRV_ERR_FOUND              -259
#define DRV_ERR_NOCOMM             -258
#define DRV_ERR_NOTFOUND           -257
#define DRV_ERR_UNKNOWN            -256

#define STAT_OK                       0
#define STAT_STACK_SMASHING        -256
#define STAT_NO_ENTRY              -257
#define STAT_OOM                   -258

#endif // AOS_TYPES_H