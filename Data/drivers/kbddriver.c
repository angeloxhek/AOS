#include <stdint.h>
#define AOSLIB_SYSCALLS_ONLY
#include "../include/aoslib.h"

#define KBD_BUF_SIZE 128
uint8_t key_buffer[KBD_BUF_SIZE];
int buf_head = 0, buf_tail = 0;

#define MAX_WAITERS 32
uint64_t waiting_apps[MAX_WAITERS];
int waiters_count = 0;

int driver_main(void* reserved1, void* reserved2) {
    register_driver(DT_KEYBOARD, 0);
    
    message_t msg;
    while(1) {
        ipc_recv(&msg);
        
        if (msg.type == MSG_TYPE_KEYBOARD) {
            if (msg.subtype == MSG_SUBTYPE_SEND) {
                uint8_t scancode = (uint8_t)msg.param1;
                if (scancode != 0) {
                    if (waiters_count > 0) {
                        uint64_t app_tid = waiting_apps[0];
                        for(int i=0; i < waiters_count-1; i++) 
                            waiting_apps[i] = waiting_apps[i+1];
                        waiters_count--;
                        message_t response;
                        response.type = MSG_TYPE_KEYBOARD;
                        response.subtype = MSG_SUBTYPE_RESPONSE;
                        response.param1 = scancode;
                        ipc_send(app_tid, &response);
                    } 
                    else {
                        int next_head = (buf_head + 1) % KBD_BUF_SIZE;
                        if (next_head != buf_tail) {
                            key_buffer[buf_head] = scancode;
                            buf_head = next_head;
                        }
                    }
                }
            }
            else if (msg.subtype == MSG_SUBTYPE_QUERY) {
                if (buf_head != buf_tail) {
                    uint8_t scancode = key_buffer[buf_tail];
                    buf_tail = (buf_tail + 1) % KBD_BUF_SIZE;
                    
                    message_t response;
                    response.type = MSG_TYPE_KEYBOARD;
                    response.subtype = MSG_SUBTYPE_RESPONSE;
                    response.param1 = scancode;
                    ipc_send(msg.sender_tid, &response);
                } 
                else {
                    if (waiters_count < MAX_WAITERS) {
                        waiting_apps[waiters_count++] = msg.sender_tid;
                    } else {
                        message_t response;
                        response.type = MSG_TYPE_KEYBOARD;
                        response.subtype = MSG_SUBTYPE_RESPONSE;
                        response.param1 = 0;
                        ipc_send(msg.sender_tid, &response);
                    }
                }
            }
        }
    }
}