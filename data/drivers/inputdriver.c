#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <aos/types.h>
#include <aos/driver.h>
#include <aos/ipc.h>
#include <aos/input.h>

#define PS2_DATA_PORT   0x60
#define PS2_CMD_PORT    0x64

AOS_DECLARE_DRIVER(DT_INPUT, DRV_PERM_IO_PORTS, PS2_CMD_PORT, PS2_DATA_PORT);

static apid_t subscriber_pid = 0;

void ps2_wait_write() {
    int timeout = 100000;
    while ((hal_inb(PS2_CMD_PORT) & 2) && timeout--);
}
void ps2_wait_read() {
    int timeout = 100000;
    while (!(hal_inb(PS2_CMD_PORT) & 1) && timeout--);
}
void ps2_write_cmd(uint8_t cmd) {
    ps2_wait_write();
    hal_outb(PS2_CMD_PORT, cmd);
}
void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    hal_outb(PS2_DATA_PORT, data);
}
uint8_t ps2_read_data() {
    ps2_wait_read();
    return hal_inb(PS2_DATA_PORT);
}

void init_mouse() {
    printf("InputDriver: Initializing PS/2 Mouse...\n");

    ps2_write_cmd(0xA8);

    ps2_write_cmd(0x20);
    uint8_t status = ps2_read_data();

    status |= (1 << 1); 
    status &= ~(1 << 5); 
    
    ps2_write_cmd(0x60);
    ps2_write_data(status);

    ps2_write_cmd(0xD4);
    ps2_write_data(0xF6);
    ps2_read_data();

    ps2_write_cmd(0xD4);
    ps2_write_data(0xF4);
    ps2_read_data();
    
    printf("InputDriver: Mouse initialized!\n");
}

uint8_t mouse_packet[3];
int mouse_cycle = 0;

int driver_main(void* reserved1, void* reserved2) {
    printf("InputDriver: Starting...\n");
    init_mouse();

	ipc_set_limit(0, 1024);

	set_thread_priority(0, THREAD_PRIO_REALTIME);

    message_t msg;
    while(1) {
        ipc_recv(&msg);
        
        if (msg.type == MSG_TYPE_INPUT && msg.subtype == MSG_SUBTYPE_QUERY) {
            if (msg.param1 == INPUT_CMD_SUBSCRIBE) {
                subscriber_pid = msg.sender_pid;
                printf("InputDriver: Subscribed PID %d\n", subscriber_pid);
                message_t out;
				memset(&out, 0, sizeof(message_t));
				
				out.type = MSG_TYPE_INPUT;
				out.subtype = MSG_SUBTYPE_RESPONSE;
				
				out.param1 = INPUT_ERR_OK;
				
				ipc_reply(&msg, &out);
            }
        }

        if (msg.type == MSG_TYPE_HARDWARE && msg.subtype == MSG_SUBTYPE_SEND) {
            if (msg.param1 == HW_EVT_IRQ) {
				uint8_t status = hal_inb(PS2_CMD_PORT);
				if (status & 0x01) {
					uint8_t data = hal_inb(PS2_DATA_PORT);
					if (status & 0x20) {
						mouse_packet[mouse_cycle] = data;
						if (mouse_cycle == 0 && !(data & 0x08)) continue; 
						mouse_cycle++;

						if (mouse_cycle == 3) { 
							mouse_cycle = 0;

							if (subscriber_pid != 0) {
								uint8_t flags = mouse_packet[0];
								int delta_x = mouse_packet[1];
								int delta_y = mouse_packet[2];

								if (flags & 0x10) delta_x |= 0xFFFFFF00; 
								if (flags & 0x20) delta_y |= 0xFFFFFF00; 
								delta_y = -delta_y;
								uint8_t buttons = flags & 0x07;
								
								message_t out;
								memset(&out, 0, sizeof(message_t));
								
								out.type = MSG_TYPE_INPUT;
								out.subtype = MSG_SUBTYPE_SEND;
								
								out.param1 = INPUT_EVENT_MOUSE;
								
								out.param2 = (delta_x & 0xFFFF) | ((delta_y & 0xFFFF) << 16);
								
								out.param3 = buttons;
								
								ipc_send(subscriber_pid, &out);
							}
						}
					} else {
						 if (subscriber_pid != 0) {
							message_t out;
							memset(&out, 0, sizeof(message_t));
							
							out.type = MSG_TYPE_INPUT;
							out.subtype = MSG_SUBTYPE_SEND;
							
							out.param1 = INPUT_EVENT_KEY;
							out.param2 = data;
							
							ipc_send(subscriber_pid, &out);
						}
					}
				}
			}
        }
    }
}