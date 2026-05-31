#ifndef AOSLIB_H
#define AOSLIB_H

#if !defined(AOSKERNEL) && !defined(AOSLIB_START_ONLY) && \
    !defined(AOSLIB_SYSCALLS_ONLY) && !defined(AOSLIB_VFS_ONLY) && \
    !defined(AOSLIB_STRING_ONLY) && !defined(AOSLIB_IO_ONLY) && \
    !defined(AOSLIB_AUTH_ONLY) && !defined(AOSLIB_HAL_ONLY)
    #define AOSLIB_START
    #define AOSLIB_SYSCALLS
    #define AOSLIB_VFS
    #define AOSLIB_STRING
    #define AOSLIB_IO
    #define AOSLIB_AUTH
    #define AOSLIB_HAL
#else
    #if defined(AOSLIB_START_ONLY)
        #define AOSLIB_START
    #endif
    #if defined(AOSLIB_SYSCALLS_ONLY)
        #define AOSLIB_SYSCALLS
    #endif
    #if defined(AOSLIB_VFS_ONLY)
        #define AOSLIB_VFS
    #endif
    #if defined(AOSLIB_STRING_ONLY)
        #define AOSLIB_STRING
    #endif
    #if defined(AOSLIB_IO_ONLY)
        #define AOSLIB_IO
    #endif
    #if defined(AOSLIB_AUTH_ONLY)
        #define AOSLIB_AUTH
    #endif
    #if defined(AOSLIB_HAL_ONLY)
        #define AOSLIB_HAL
    #endif
#endif

#include "aos/types.h"
#include "aos/auth.h"
#include "aos/vfs.h"
#include "aos/sync.h"
#include "aos/driver.h"
#include "aos/process.h"
#include "aos/syscalls.h"
#include "aos/string.h"

#endif // AOSLIB_H