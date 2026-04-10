#include <sys/time.h>
#include "../include/aoslib.h"

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tv == NULL) return -1;

    time_info_t info;
    if (get_time_info(&info) != 0) {
        return -1;
    }
    
    tv->tv_sec = info.boot_time + (info.uptime / info.frequency);
	
    uint64_t remaining_ticks = info.uptime % info.frequency;
    tv->tv_usec = (remaining_ticks * 1000000) / info.frequency;

    if (tz != NULL) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }

    return 0;
}