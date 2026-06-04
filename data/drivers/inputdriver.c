#include <stdint.h>
#include <aoslib.h>

#define PS2_DATA_PORT   0x60
#define PS2_CMD_PORT    0x64

// Даем драйверу права на работу с портами ввода-вывода
AOS_DECLARE_DRIVER(DT_INPUT, DRV_PERM_IO_PORTS, PS2_CMD_PORT, PS2_DATA_PORT);

// Кто слушает события (наш Оконный Менеджер)
static apid_t subscriber_pid = 0;

// Ожидание готовности PS/2 к записи
void ps2_wait_write() {
    int timeout = 100000;
    while ((hal_inb(PS2_CMD_PORT) & 2) && timeout--);
}

// Ожидание наличия данных для чтения
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

// --- Инициализация мыши ---
void init_mouse() {
    printf("InputDriver: Initializing PS/2 Mouse...\n");

    // 1. Включаем вспомогательное устройство (мышь)
    ps2_write_cmd(0xA8);

    // 2. Читаем байт конфигурации контроллера
    ps2_write_cmd(0x20);
    uint8_t status = ps2_read_data();

    // 3. Включаем прерывания IRQ12 (бит 1) и сбрасываем бит отключения мыши (бит 5)
    status |= (1 << 1); 
    status &= ~(1 << 5); 
    
    // 4. Записываем обновленный байт конфигурации обратно
    ps2_write_cmd(0x60);
    ps2_write_data(status);

    // 5. Говорим мыши использовать настройки по умолчанию
    ps2_write_cmd(0xD4); // 0xD4 = отправить следующую команду прямо мыши
    ps2_write_data(0xF6);
    ps2_read_data(); // Ждем подтверждения (ACK = 0xFA)

    // 6. Включаем отправку пакетов с данными от мыши
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF4);
    ps2_read_data(); // Ждем ACK
    
    printf("InputDriver: Mouse initialized!\n");
}

// --- Состояние мыши ---
uint8_t mouse_packet[3];
int mouse_cycle = 0;

void handle_mouse_byte(uint8_t byte) {
    mouse_packet[mouse_cycle] = byte;
    if (mouse_cycle == 0 && !(byte & 0x08)) return; 
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
			
            message_t msg;
            memset(&msg, 0, sizeof(message_t));
            
            msg.type = MSG_TYPE_INPUT;
            msg.subtype = MSG_SUBTYPE_SEND;
            
            msg.param1 = INPUT_EVENT_MOUSE;
			
            msg.param2 = (delta_x & 0xFFFF) | ((delta_y & 0xFFFF) << 16);
			
            msg.param3 = buttons;
            
            ipc_send(subscriber_pid, &msg);
        }
    }
}

void handle_keyboard_byte(uint8_t scancode) {
    if (subscriber_pid != 0) {
        message_t msg;
        memset(&msg, 0, sizeof(message_t));
        
        msg.type = MSG_TYPE_INPUT;
        msg.subtype = MSG_SUBTYPE_SEND;
        
        msg.param1 = INPUT_EVENT_KEY;
        msg.param2 = scancode;
        
        ipc_send(subscriber_pid, &msg);
    }
}

int driver_main(void* reserved1, void* reserved2) {
    printf("InputDriver: Starting...\n");
    init_mouse();

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
				
				ipc_send(subscriber_pid, &out);
            }
        }

        if (msg.type == MSG_TYPE_HARDWARE && msg.subtype == MSG_SUBTYPE_SEND) {
            if (msg.param1 == HW_EVT_IRQ) {
                uint8_t status = hal_inb(PS2_CMD_PORT);
                while (status & 0x01) {
                    uint8_t data = hal_inb(PS2_DATA_PORT);
                    if (status & 0x20) handle_mouse_byte(data);
                    else handle_keyboard_byte(data);
                    status = hal_inb(PS2_CMD_PORT);
                }
            }
        }
    }
}