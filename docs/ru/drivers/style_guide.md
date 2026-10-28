# Стандарт и стиль написания аппаратных драйверов

Актуально для версии коммита 108.

Написание аппаратных драйверов в AOS строго регламентировано. Драйвер должен корректно декларировать свои права, инициализировать оборудование, настраивать приоритеты потока и безопасно обрабатывать IPC-сообщения в бесконечном цикле.

## 1. Декларация драйвера

Каждый аппаратный драйвер обязан содержать макрос `AOS_DECLARE_DRIVER` в глобальной области видимости. Это помещает структуру `aos_driver_info_t` в секцию `.driver_info` ELF-файла.

**Синтаксис:**
```c
AOS_DECLARE_DRIVER(тип_драйвера, привилегии, порт1, порт2, ...);
```
* **`тип_драйвера`**: Значение из `driver_type_t` (например, `DT_VIDEO`, `DT_INPUT`, `DT_PCI`).
* **`привилегии`**: Битовая маска прав (`DRV_PERM_IO_PORTS`, `DRV_PERM_PHYS_MAP`, `DRV_PERM_GET_SPEC_INFO`, `DRV_PERM_EDIT_SYSTEM_FLAGS`). Если права не нужны, передается `0`.
* **`порты`**: Список портов ввода-вывода (до 8 штук). Если порты не используются, передается `0`. Если необходимо получить доступ ко всем портам, передается `0xFFFF`.

*Пример (из `inputdriver.c`):*
```c
AOS_DECLARE_DRIVER(DT_INPUT, DRV_PERM_IO_PORTS, 0x64, 0x60);
```

## 2. Точка входа

Точкой входа для любого драйвера является функция `driver_main`. Стандартная функция `main` не используется. Аргументы зарезервированы ядром и должны игнорироваться макросом `UNUSED`.

```c
int driver_main(void* reserved1, void* reserved2) {
    UNUSED(reserved1); 
    UNUSED(reserved2);
    // ...
}
```

## 3. Инициализация и настройка окружения

До входа в главный цикл обработки сообщений драйвер должен выполнить следующие шаги:

1. **Инициализация оборудования:** Чтение конфигурации PCI, маппинг физической памяти (`sysmap_phys`), получение загрузочной информации (`sysget_spec_info`).
2. **Увеличение лимита IPC:** Аппаратные драйверы (особенно устройства ввода или оконные менеджеры) могут получать шквал сообщений. Необходимо увеличить лимит очереди:
   ```c
   ipc_set_limit(0, 1024); // 0 означает текущий процесс
   ```
3. **Повышение приоритета:** Для обеспечения минимальных задержек драйвер обязан перевести свой главный поток в класс реального времени:
   ```c
   set_thread_priority(0, THREAD_PRIO_REALTIME);
   ```

## 4. Главный цикл и фильтрация IPC

Драйвер работает в бесконечном цикле `while(1)`. Критически важным стандартом AOS является **фильтрация старых или чужих сообщений** (подробнее см. [Межпроцессное взаимодействие](../kernel/ipc.md)). 

Драйвер должен обрабатывать только новые запросы, игнорируя ответы на свои же сообщения или сообщения, которые были возвращены в очередь. Для этого используется флаг `MSG_FLAG_NEW_ID`.

**Вариант А: Использование `ipc_recv_ex` (Рекомендуемый)**
Функция сама отфильтрует сообщения по типу и флагу.
```c
message_t msg;
while(1) {
    // Ждем только сообщения типа DT_MYDEV и только новые (MSG_FLAG_NEW_ID)
    ipc_recv_ex(0, MSG_TYPE_MYDEV, MSG_SUBTYPE_NONE, 0, MSG_FLAG_NEW_ID, &msg);
    handle_request(&msg);
}
```

**Вариант Б: Ручная фильтрация (Используется при прослушивании нескольких типов)**
```c
message_t msg;
while (1) {
    ipc_recv(&msg);
    
    // Если это не новый запрос - возвращаем в очередь и пропускаем
    if (!(msg.flags & MSG_FLAG_NEW_ID)) {
        ipc_requeue(&msg);
        continue;
    }
    
    handle_request(&msg);
}
```

## 5. Обработка запросов и Shared Memory

При обработке запроса драйвер формирует ответное сообщение и отправляет его через `ipc_reply`.

### Правила работы с SHM:
Если клиент передал данные через разделяемую память (обычно ID передается в `msg.data`), драйвер обязан:
1. Смаппить память в свое адресное пространство: `void* buf = shm_map(shm_id);`
2. Проверить указатель на `NULL`.
3. Выполнить чтение/запись.
4. **Обязательно** освободить память в своем контексте: `shm_free(shm_id);`. Невыполнение этого шага приведет к утечке виртуального адресного пространства драйвера.

### Стандартный паттерн ответа:
```c
void handle_request(message_t* in) {
    message_t out;
    memset(&out, 0, sizeof(message_t));
    out.type = in->type;
    out.subtype = MSG_SUBTYPE_RESPONSE;
    
    switch (in->param1) { // param1 обычно содержит ID команды
        case CMD_DO_SOMETHING: {
            uint64_t shm_id = *(uint64_t*)(in->data);
            void* buf = shm_map(shm_id);
            
            if (buf) {
                // ... работа с буфером ...
                out.param1 = DRV_ERR_OK;
            } else {
                out.param1 = DRV_ERR_UNKNOWN;
            }
            
            shm_free(shm_id); // Строго обязательно!
            break;
        }
        default:
            out.param1 = DRV_ERR_NOCOMM; // Команда не поддерживается
            break;
    }
    
    ipc_reply(in, &out);
}
```

## 6. Полный шаблон аппаратного драйвера

```c
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <aos/types.h>
#include <aos/driver.h>
#include <aos/ipc.h>
#include <aos/syscalls.h>

// 1. Декларация
#define MY_DEV_PORT 0x3F8
AOS_DECLARE_DRIVER(DT_USER, DRV_PERM_IO_PORTS, MY_DEV_PORT);

// Обработчик
void handle_msg(message_t* in) {
    message_t out;
    memset(&out, 0, sizeof(message_t));
    out.type = in->type;
    out.subtype = MSG_SUBTYPE_RESPONSE;

    if (in->param1 == 1) { // Пример команды
        out.param1 = DRV_ERR_OK;
    } else {
        out.param1 = DRV_ERR_NOCOMM;
    }

    // Отправка ответа клиенту
    ipc_reply(in, &out);
}

// 2. Точка входа
int driver_main(void* reserved1, void* reserved2) {
    UNUSED(reserved1); 
    UNUSED(reserved2);
    
    printf("MyDriver: Initializing...\n");

    // 3. Инициализация оборудования
    hal_outb(MY_DEV_PORT, 0x00);

    // Настройка окружения процесса
    ipc_set_limit(0, 1024);
    set_thread_priority(0, THREAD_PRIO_REALTIME);

    printf("MyDriver: Ready.\n");

    // 4. Главный цикл
    message_t msg;
    while (1) {
        // Получаем только новые сообщения, адресованные этому драйверу
        ipc_recv_ex(0, MSG_TYPE_DATA, MSG_SUBTYPE_NONE, 0, MSG_FLAG_NEW_ID, &msg);
        
        // 5. Обработка
        handle_msg(&msg);
    }

    return 0;
}
```