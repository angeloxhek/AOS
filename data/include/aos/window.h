#ifndef AOS_WINDOW_H
#define AOS_WINDOW_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WND_ERR_OK                 DRV_ERR_OK
#define WND_ERR_NOCOMM             DRV_ERR_NOCOMM
#define WND_ERR_NOTFOUND           DRV_ERR_NOTFOUND
#define WND_ERR_UNKNOWN            DRV_ERR_UNKNOWN

#define WND_FLAG_NORMAL      0
#define WND_FLAG_BACKGROUND  (1<<0)
#define WND_FLAG_NO_TITLEBAR (1<<1)
#define WND_FLAG_TOPMOST     (1<<2)
#define WND_FLAG_TRANSPARENT (1<<3)

typedef enum {
    WND_CMD_CREATE = 1,
	WND_CMD_FLUSH,
	WND_CMD_GET_SCREEN_INFO
} window_cmd_t;

typedef struct {
    int win_id;
    int w, h;
    uint64_t shm_id;
    uint32_t* buffer;
} window_t;

typedef struct {
    uint16_t width;
    uint16_t height;
} screen_info_t;

#ifdef AOSLIB_WINDOW
window_t* window_create(int w, int h, uint32_t flags);
void window_flush(window_t* win);
int get_screen_info(screen_info_t* info);
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_WINDOW_H