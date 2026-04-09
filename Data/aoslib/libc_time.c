#include <time.h>
#include "../include/aoslib.h"

time_t time(time_t *tloc) {
    time_info_t info;
    if (get_time_info(&info) != 0) {
        return (time_t)-1;
    }
    time_t current = (time_t)(info.boot_time + (info.uptime / info.frequency));
    if (tloc != NULL) {
        *tloc = current;
    }
    return current;
}

clock_t clock(void) {
    time_info_t info;
    if (get_time_info(&info) != 0) {
        return (clock_t)-1;
    }
    return (clock_t)((info.uptime * 1000000) / info.frequency);
}