# Стандарт написания дисковых модулей

Актуально для версии коммита 108.

Дисковые модули в AOS отвечают за низкоуровневое взаимодействие с аппаратными контроллерами накопителей (IDE, AHCI, NVMe, USB). Они транслируют запросы на чтение и запись секторов от VFS в команды для конкретного оборудования.

Дисковые модули компилируются **внутри процесса VFS** (демона `VFSDRIVER.ELF`) и работают в Ring 3. Доступ к оборудованию осуществляется через функции `hal_inb`, `hal_outb`, `hal_insw` и т.д., так как процесс VFS обладает привилегией `DRV_PERM_IO_PORTS`.

## 1. Интерфейс `disk_driver_t`

Каждый дисковый модуль должен реализовать структуру `disk_driver_t`, описанную в `data/include/vfs/disk_interface.h`:

```c
typedef void* disk_instance_t;

typedef struct disk_driver {
    const char name[64]; // Имя драйвера (например, "ide", "ahci")
    
    // Инициализация контроллера. Возвращает количество найденных дисков.
    int (*init)(void); 
    
    // Получение хэндла диска и его геометрии по индексу (от 0 до count-1).
    disk_instance_t (*get_disk)(int index, uint64_t* out_total_sectors, uint32_t* out_sector_size);
    
    // Чтение и запись секторов. Возвращают 0 при успехе, < 0 при ошибке.
    int (*read)(disk_instance_t disk, uint64_t lba, uint64_t count, void* buf);
    int (*write)(disk_instance_t disk, uint64_t lba, uint64_t count, const void* buf);
} disk_driver_t;
```

### Важное архитектурное правило: Абсолютная адресация
Дисковый модуль **ничего не знает о разделах (Partitions)**. VFS самостоятельно вычисляет смещения разделов. В функции `read` и `write` всегда передается **абсолютный физический LBA** (Logical Block Address) от начала диска.

## 2. Пошаговая реализация модуля

### Шаг 1: Внутреннее состояние (State)
Модуль должен хранить информацию о найденных дисках. `disk_instance_t` — это непрозрачный указатель (`void*`), который VFS будет передавать в функции `read` и `write`. Обычно это указатель на внутреннюю структуру модуля.

```c
typedef struct {
    uint16_t base_port;
    uint8_t  is_slave;
    uint64_t total_sectors;
} my_drive_t;

static my_drive_t drives[4];
static int drive_count = 0;
```

### Шаг 2: Инициализация (`init`)
Функция вызывается один раз при старте VFS. Она должна просканировать шину, сбросить контроллер, идентифицировать подключенные диски и сохранить их параметры.
**Возвращаемое значение:** Количество найденных и готовых к работе дисков.

```c
static int my_init(void) {
    drive_count = 0;
    // ... сканирование портов, отправка команд IDENTIFY ...
    if (disk_found) {
        drives[drive_count].base_port = 0x1F0;
        drives[drive_count].total_sectors = read_capacity();
        drive_count++;
    }
    return drive_count;
}
```

### Шаг 3: Экспорт геометрии (`get_disk`)
VFS вызовет эту функцию в цикле от `0` до `drive_count - 1`.
Модуль должен вернуть указатель на структуру конкретного диска и заполнить выходные параметры геометрии.

```c
static disk_instance_t my_get_disk(int index, uint64_t* out_sectors, uint32_t* out_sec_size) {
    if (index < 0 || index >= drive_count) return NULL;
    
    *out_sectors = drives[index].total_sectors;
    *out_sec_size = 512; // Стандартный размер сектора
    
    return &drives[index]; // Этот указатель VFS будет передавать в read/write
}
```

### Шаг 4: Чтение и Запись (`read` / `write`)
Функции должны быть **синхронными (блокирующими)**. Они обязаны дождаться завершения операции оборудованием (через поллинг статуса или ожидание прерывания) перед возвратом управления.

```c
static int my_read(disk_instance_t disk, uint64_t lba, uint64_t count, void* buf) {
    my_drive_t* d = (my_drive_t*)disk;
    uint8_t* ptr = (uint8_t*)buf;
    
    for (uint64_t i = 0; i < count; i++, lba++) {
        // 1. Ожидание готовности контроллера (Polling BSY/DRQ)
        if (wait_ready(d->base_port) < 0) return -1;
        
        // 2. Отправка LBA и команды чтения
        send_lba_and_cmd(d->base_port, lba, CMD_READ);
        
        // 3. Чтение данных из порта в буфер
        hal_insw(d->base_port, ptr, 256); // 256 слов = 512 байт
        ptr += 512;
    }
    return 0; // Успех
}
```

## 3. Регистрация модуля в VFS

Чтобы VFS узнал о новом модуле, его необходимо добавить в глобальный массив `available_disks` в файле `data/initdrivers/vfs/vfsdriver.c`.

1. Экспортируйте структуру драйвера в конце вашего файла:
```c
disk_driver_t my_disk_driver = {
    .name = "mydev", 
    .init = my_init, 
    .get_disk = my_get_disk, 
    .read = my_read, 
    .write = my_write
};
```

2. Добавьте драйвер в `vfsdriver.c`:
```c
extern disk_driver_t ide_disk_driver;
extern disk_driver_t my_disk_driver; // <-- Объявление

disk_driver_t* available_disks[] = {
    &ide_disk_driver,
    &my_disk_driver, // <-- Регистрация
    0 // Нуль-терминатор обязателен!
};
```

При загрузке VFS автоматически вызовет `my_init()`. Если диски будут найдены, VFS создаст для них узлы в виртуальной файловой системе (например, `/hw/mydev0/raw`, `/hw/mydev1/raw`) и попытается найти на них таблицы разделов.

## 4. Правила и ограничения

1. **Запрет на использование прерываний напрямую:** Так как драйвер работает в Ring 3, он не может напрямую регистрировать обработчики прерываний (IDT). Если контроллер требует прерываний, используйте IPC-сообщения типа `MSG_TYPE_HARDWARE` от ядра (аналогично `inputdriver.c`). Для простых дисковых драйверов (как IDE) рекомендуется использовать поллинг (Polling).
2. **Таймауты:** При поллинге статуса оборудования (например, ожидание снятия бита BSY) обязательно используйте счетчики таймаута. Зависание в функции `read` приведет к зависанию всего демона VFS.
3. **Размер сектора:** VFS поддерживает размеры секторов 512, 1024, 2048 и 4096 байт. Убедитесь, что `out_sec_size` в `get_disk` строго соответствует реальному размеру сектора устройства.