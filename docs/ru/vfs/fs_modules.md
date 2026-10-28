# Стандарт написания драйверов файловых систем

Актуально для версии коммита 108.

Драйверы файловых систем (FS Modules) — это компоненты VFS, отвечающие за трансляцию высокоуровневых файловых операций (открытие, чтение, запись, навигация по директориям) в низкоуровневые запросы к блочному устройству.

Драйверы ФС компилируются **внутри процесса VFS** (`VFSDRIVER.ELF`) и работают в Ring 3. Они взаимодействуют с диском исключительно через абстракцию `block_dev_t` с помощью функций `block_read` и `block_write`.

## 1. Интерфейc

Каждый драйвер ФС должен реализовать структуру `fs_driver_t`, описанную в `data/include/vfs/fs_interface.h`:

```c
typedef void* fs_file_handle_t;
typedef void* fs_context_t; 
typedef void* fs_instance_t;

typedef struct {
    const char name[64]; // Имя драйвера (например, "fat32", "ext2")
    
    // Монтирование ФС. Возвращает инстанс ФС (или NULL при ошибке).
    fs_instance_t (*mount)(block_dev_t* dev, fs_context_t context);
    
    // Размонтирование ФС и освобождение ресурсов.
    void (*umount)(fs_instance_t fs);
    
    // Открытие файла/директории по абсолютному пути от корня ФС.
    fs_file_handle_t (*open)(fs_instance_t fs, const char* path, uint32_t flags);
    
    // Открытие файла/директории относительно уже открытой директории.
    fs_file_handle_t (*openat)(fs_instance_t fs, fs_file_handle_t dir_handle, const char* path, uint32_t flags);
    
    // Чтение и запись данных. Возвращают количество байт или < 0 при ошибке.
    int (*read)(fs_instance_t fs, fs_file_handle_t file, void* buf, uint64_t size, uint64_t offset);
    int (*write)(fs_instance_t fs, fs_file_handle_t file, const void* buf, uint64_t size, uint64_t offset);
    
    // Закрытие файла и освобождение хэндла.
    void (*close)(fs_instance_t fs, fs_file_handle_t file);
    
    // Чтение содержимого директории.
    int (*readdir)(fs_instance_t fs, fs_file_handle_t dir, uint64_t* offset, fs_dirent_t* out_array, int max_entries);
    
    // Получение метаданных файла (размер, атрибуты, inode).
    int (*stat)(fs_instance_t fs, fs_file_handle_t file, fs_stat_t* out_info); 
    
    // Получение метки тома (Volume Label).
    void (*get_label)(fs_instance_t fs, char* out_label);
} fs_driver_t;
```

## 2. Пошаговая реализация модуля

### Шаг 1: Структуры данных

Драйвер должен определить две основные структуры:
1. **Инстанс ФС (`fs_instance_t`)**: Хранит глобальное состояние смонтированного раздела (указатель на блочное устройство, закэшированные суперблоки, корневой кластер/inode).
2. **Хэндл файла (`fs_file_handle_t`)**: Хранит состояние конкретного открытого файла (текущий кластер/inode, размер, атрибуты).

```c
typedef struct {
    block_dev_t* dev;
    uint32_t root_inode;
    uint32_t block_size;
    // ... кэши, мьютексы ...
} myfs_instance_t;

typedef struct {
    uint32_t inode;
    uint64_t size_bytes;
    uint8_t  is_dir;
    // ... закэшированные данные для быстрого чтения ...
} myfs_file_t;
```

### Шаг 2: Монтирование

Функция `mount` вызывается VFS при обнаружении нового раздела. Драйвер должен прочитать суперблок (или Boot Sector), проверить магические сигнатуры и, если это его файловая система, выделить и вернуть `fs_instance_t`.

```c
static fs_instance_t myfs_mount(block_dev_t* dev, fs_context_t context) {
    uint8_t* buf = malloc(dev->sector_size);
    if (block_read(dev, 0, 1, buf) != 0) {
        free(buf); return NULL;
    }

    // Проверка сигнатуры
    if (((myfs_super_t*)buf)->magic != MYFS_MAGIC) {
        free(buf); return NULL;
    }

    myfs_instance_t* inst = malloc(sizeof(myfs_instance_t));
    inst->dev = dev;
    inst->block_size = ((myfs_super_t*)buf)->block_size;
    // ... инициализация ...

    free(buf);
    return (fs_instance_t)inst;
}

static void myfs_umount(fs_instance_t fs) {
    myfs_instance_t* inst = (myfs_instance_t*)fs;
    // ... очистка кэшей ...
    free(inst);
}
```

### Шаг 3: Открытие файлов

Функция `open` должна разобрать переданный путь (например, `folder/file.txt`), найти соответствующий Inode/Кластер на диске и вернуть выделенный `fs_file_handle_t`.

* **Флаги (`flags`)**: Драйвер должен обрабатывать флаги `VFS_FCREATE` (создать файл, если его нет) и `VFS_FTRUNC` (очистить файл при открытии).
* **Путь**: Путь всегда передается без ведущего слеша (или с одним слешем `/` для открытия корня).

```c
static fs_file_handle_t myfs_open(fs_instance_t fs, const char* path, uint32_t flags) {
    myfs_instance_t* inst = (myfs_instance_t*)fs;
    
    uint32_t target_inode = find_inode_by_path(inst, inst->root_inode, path, flags);
    if (target_inode == 0) return NULL; // Файл не найден

    myfs_file_t* handle = malloc(sizeof(myfs_file_t));
    handle->inode = target_inode;
    // ... заполнение метаданных ...
    
    return (fs_file_handle_t)handle;
}
```

### Шаг 4: Чтение и Запись

Функции получают абсолютное смещение (`offset`) от начала файла. Драйвер должен вычислить, в каком логическом блоке/кластере находятся данные, прочитать их с диска и скопировать в буфер пользователя.

```c
static int myfs_read(fs_instance_t fs, fs_file_handle_t f, void* buf, uint64_t size, uint64_t offset) {
    myfs_instance_t* inst = (myfs_instance_t*)fs;
    myfs_file_t* file = (myfs_file_t*)f;
    
    if (file->is_dir) return -1; // Нельзя читать директорию как файл
    if (offset >= file->size_bytes) return 0; // EOF

    if (offset + size > file->size_bytes) {
        size = file->size_bytes - offset;
    }

    // 1. Найти LBA на диске, соответствующий смещению 'offset'
    // 2. Прочитать данные через block_read(inst->dev, ...)
    // 3. Скопировать в 'buf'
    
    return bytes_read;
}
```

### Шаг 5: Чтение директорий

Функция `readdir` используется для получения списка файлов в папке.
* **`offset`**: Это непрозрачный указатель (курсор), которым управляет драйвер ФС. При первом вызове `*offset == 0`. Драйвер должен обновить `*offset` так, чтобы при следующем вызове продолжить чтение с нужного места. Если файлы закончились, драйвер должен записать в `*offset` значение `-1` (`(uint64_t)-1`).
* **Возвращаемое значение**: Количество реально прочитанных записей (от 0 до `max_entries`).

```c
static int myfs_readdir(fs_instance_t fs, fs_file_handle_t dir, uint64_t* offset, fs_dirent_t* out_array, int max_entries) {
    if (*offset == (uint64_t)-1) return 0; // EOF
    
    int entries_read = 0;
    while (entries_read < max_entries) {
        // Читаем запись директории с диска по смещению *offset
        // ...
        
        if (is_end_of_dir) {
            *offset = (uint64_t)-1;
            break;
        }
        
        if (is_valid_file) {
            strcpy(out_array[entries_read].name, filename);
            out_array[entries_read].size = filesize;
            out_array[entries_read].type = is_dir ? VFS_FILE_TYPE_DIR : VFS_FILE_TYPE_REGULAR;
            entries_read++;
        }
        
        *offset += size_of_dir_entry_on_disk;
    }
    return entries_read;
}
```

### Шаг 6: Метаданные

* **`stat`**: Заполняет структуру `fs_stat_t`. Поле `inode_id` критически важно — VFS использует его для механизма блокировок файлов (`flock`). Оно должно быть уникальным для каждого файла в рамках данного инстанса ФС.
* **`get_label`**: Читает метку тома (Volume Label) с диска. VFS использует её для создания удобных симлинков в `/mnt/` (например, `/mnt/SYSTEM`).

## 3. Регистрация модуля в VFS

Чтобы VFS узнал о новой файловой системе, её необходимо добавить в глобальный массив `available_filesystems` в файле `data/initdrivers/vfs/vfsdriver.c`.

1. Экспортируйте структуру драйвера в конце вашего файла:
```c
fs_driver_t myfs_driver = {
    .name = "myfs",
    .mount = myfs_mount,
    .umount = myfs_umount,
    .open = myfs_open,
    .openat = myfs_openat,
    .read = myfs_read,
    .write = myfs_write,
    .close = myfs_close,
    .readdir = myfs_readdir,
    .stat = myfs_stat,
    .get_label = myfs_get_label
};
```

2. Добавьте драйвер в `vfsdriver.c`:
```c
extern fs_driver_t fat32_driver;
extern fs_driver_t myfs_driver; // <-- Объявление

fs_driver_t* available_filesystems[] = {
    &fat32_driver,
    &myfs_driver, // <-- Регистрация
    0 // Нуль-терминатор обязателен!
};
```

## 4. Правила и ограничения

1. **Кэширование:** VFS не предоставляет встроенного кэша страниц (Page Cache). Драйвер ФС должен самостоятельно реализовывать кэширование секторов (как это сделано в `fat32_cache_t`), иначе производительность чтения мелких файлов будет крайне низкой.
2. **Утечки памяти:** Все структуры, выделенные в `mount` и `open`, должны быть строго освобождены в `umount` и `close` соответственно.
3. **Безопасность путей:** Функция `open` должна корректно обрабатывать пути, содержащие `.` и `..`, чтобы не позволить пользователю выйти за пределы корня файловой системы. (Хотя базовая нормализация путей выполняется на уровне VFS, драйвер ФС должен быть устойчив к некорректным путям).