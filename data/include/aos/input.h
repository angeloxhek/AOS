#ifndef AOS_INPUT_H
#define AOS_INPUT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_ERR_OK                 DRV_ERR_OK
#define INPUT_ERR_NOCOMM             DRV_ERR_NOCOMM
#define INPUT_ERR_NOTFOUND           DRV_ERR_NOTFOUND
#define INPUT_ERR_UNKNOWN            DRV_ERR_UNKNOWN

typedef enum {
    INPUT_CMD_SUBSCRIBE = 1,
    INPUT_EVENT_KEY,
    INPUT_EVENT_MOUSE
} input_cmd_t;

#ifdef AOSLIB_VIDEODRIVER
int input_subscribe();
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_VIDEODRIVER_H