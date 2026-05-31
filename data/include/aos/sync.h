#ifndef AOS_SYNC_H
#define AOS_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile int locked;
} mutex_t;

void mutex_init(mutex_t* m);
void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);

#ifdef __cplusplus
}
#endif
#endif // AOS_SYNC_H