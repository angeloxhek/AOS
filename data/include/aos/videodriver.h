#ifndef AOS_VIDEODRIVER_H
#define AOS_VIDEODRIVER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_ERR_OK                 DRV_ERR_OK
#define VIDEO_ERR_NOCOMM             DRV_ERR_NOCOMM
#define VIDEO_ERR_NOTFOUND           DRV_ERR_NOTFOUND
#define VIDEO_ERR_UNKNOWN            DRV_ERR_UNKNOWN

typedef enum {
    VIDEO_CMD_FILL_RECT = 1,
	VIDEO_CMD_BLIT,
	VIDEO_CMD_SET_BACKBUFFER,
	VIDEO_CMD_FLUSH_RECTS
} video_cmd_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} video_rect_t;

#ifdef AOSLIB_VIDEODRIVER
void video_init(void);
int video_set_backbuffer(uint64_t shm_id);
int video_flush_rects(video_rect_t* rects, uint32_t count);
int video_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_VIDEODRIVER_H