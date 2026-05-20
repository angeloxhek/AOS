#include <stdint.h>
#include "../include/aoslib.h"

typedef struct kbd_node {
    uint8_t scancode;
    struct kbd_node* next;
} kbd_node_t;

typedef struct waiter_node {
    uint64_t tid;
    struct waiter_node* next;
} waiter_node_t;

kbd_node_t *kbd_head = NULL, *kbd_tail = NULL;
waiter_node_t *waiters_head = NULL, *waiters_tail = NULL;

void push_scancode(uint8_t scancode) {
    kbd_node_t* node = (kbd_node_t*)malloc(sizeof(kbd_node_t));
    node->scancode = scancode;
    node->next = NULL;
    if (kbd_tail) { kbd_tail->next = node; kbd_tail = node; }
    else { kbd_head = kbd_tail = node; }
}

uint8_t pop_scancode() {
    if (!kbd_head) return 0;
    uint8_t scancode = kbd_head->scancode;
    kbd_node_t* temp = kbd_head;
    kbd_head = kbd_head->next;
    if (!kbd_head) kbd_tail = NULL;
    free(temp);
    return scancode;
}

void add_waiter(uint64_t tid) {
    waiter_node_t* node = (waiter_node_t*)malloc(sizeof(waiter_node_t));
    node->tid = tid;
    node->next = NULL;
    if (waiters_tail) { waiters_tail->next = node; waiters_tail = node; }
    else { waiters_head = waiters_tail = node; }
}

uint64_t pop_waiter() {
    if (!waiters_head) return 0;
    uint64_t tid = waiters_head->tid;
    waiter_node_t* temp = waiters_head;
    waiters_head = waiters_head->next;
    if (!waiters_head) waiters_tail = NULL;
    free(temp);
    return tid;
}

int driver_main(void* reserved1, void* reserved2) {
    register_driver(DT_KEYBOARD, 0);
    
    message_t msg;
    while(1) {
        ipc_recv(&msg);
        
        if (msg.type == MSG_TYPE_KEYBOARD) {
            if (msg.subtype == MSG_SUBTYPE_SEND) {
                uint8_t scancode = (uint8_t)msg.param1;
                
                if (waiters_head) {
                    uint64_t app_tid = pop_waiter();
                    message_t response = { .type = MSG_TYPE_KEYBOARD, .subtype = MSG_SUBTYPE_RESPONSE, .param1 = scancode };
                    ipc_send(app_tid, &response);
                } else {
                    push_scancode(scancode);
                }
            }
            else if (msg.subtype == MSG_SUBTYPE_QUERY) {
                if (kbd_head) {
                    uint8_t scancode = pop_scancode();
                    message_t response = { .type = MSG_TYPE_KEYBOARD, .subtype = MSG_SUBTYPE_RESPONSE, .param1 = scancode };
                    ipc_send(msg.sender_tid, &response);
                } else {
                    add_waiter(msg.sender_tid);
                }
            }
        }
    }
}