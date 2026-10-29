# Стандарт написания удаленных драйверов ФС

Актуально для версии коммита 108.

Удаленный драйвер ФС (Remote FS Daemon) — это самостоятельный процесс пользовательского пространства (Ring 3), который предоставляет виртуальную файловую систему другим процессам через механизм IPCFS. Примером такого драйвера является `pcidriver.c`, который проецирует конфигурационное пространство PCI в папку `/hw/pci/`.

Написание такого драйвера сводится к регистрации в VFS и обработке входящих IPC-сообщений типа `MSG_TYPE_VFS`.

## 1. Регистрация и Монтирование

В функции `driver_main` (или `main`), до входа в главный цикл, драйвер должен зарегистрировать свое имя в VFS и запросить монтирование.

```c
#include <aos/vfs.h>

// 1. Регистрация имени ФС в VFS
fs_context_t ctx = vfs_register_fs("myfs");
if (!ctx) {
    printf("MyFS: Failed to register in VFS!\n");
    return -1;
}

// 2. Асинхронный запрос на монтирование
// Используется async, чтобы не заблокировать процесс инициализации
vfs_mount_fs_async(ctx, "/sys/myfs");
```

## 2. Дизайн удаленных дескрипторов (Remote FD)

Когда VFS просит ваш драйвер открыть файл (`VFS_CMD_OPEN`), вы должны вернуть ему уникальный идентификатор — **Remote FD** (`uint64_t`). VFS будет передавать этот идентификатор во все последующие вызовы (`read`, `stat`, `close`).

**Варианты реализации Remote FD:**
1. **Указатель на структуру:** Выделяете память через `malloc`, заполняете данные о файле и возвращаете указатель, приведенный к `uint64_t`. В `close` делаете `free`.
2. **Индекс в массиве:** Возвращаете индекс из вашего внутреннего массива открытых файлов.
3. **Битовая маска (Stateless):** Если ФС простая (как в `pcidriver.c`), можно закодировать всю информацию прямо в числе. Например: старшие биты — указатель на устройство, младшие 8 бит — тип файла (0 = папка, 1 = файл "vendor", 2 = файл "device"). Это избавляет от необходимости использовать `malloc`/`free`.

## 3. Обработка IPC-сообщений VFS

В главном цикле драйвер должен перехватывать сообщения `MSG_TYPE_VFS` и формировать ответ.

```c
void handle_vfs(message_t* in) {
    message_t out;
    memset(&out, 0, sizeof(message_t));
    out.type = MSG_TYPE_VFS;
    out.subtype = MSG_SUBTYPE_RESPONSE;
    
    switch (in->param1) {
        case VFS_CMD_MOUNT_FS:
            // VFS подтверждает монтирование.
            // Можно вернуть корневой Remote FD в out.param2.
            out.param1 = VFS_ERR_OK;
            out.param2 = (uint64_t)ROOT_FD_VALUE;
            break;
            
        case VFS_CMD_OPEN:
            // Открытие файла.
            // in->data содержит путь (строку). in->param2 содержит флаги.
            out.param1 = myfs_open((char*)in->data, in->param2, &out.param2);
            break;
            
        case VFS_CMD_READ:
            // Чтение данных.
            out.param1 = myfs_read(in, &out.param2);
            break;
            
        case VFS_CMD_LIST:
            // Чтение директории.
            out.param1 = myfs_readdir(in, &out.param2, &out.data);
            break;
            
        case VFS_CMD_STAT:
            // Получение метаданных.
            out.param1 = myfs_stat(in);
            break;
            
        case VFS_CMD_CLOSE:
            // Закрытие файла.
            myfs_close((uint64_t)in->param2);
            out.param1 = VFS_ERR_OK;
            break;
            
        default:
            out.param1 = VFS_ERR_NOCOMM;
            break;
    }
    
    ipc_reply(in, &out);
}
```

## 4. Работа с Shared Memory (SHM)

Команды `READ`, `WRITE`, `LIST` и `STAT` требуют передачи структур или буферов, которые не помещаются в 112 байт IPC-сообщения. VFS передает их через разделяемую память.

**Правила работы с SHM в обработчиках:**
1. ID памяти лежит в `*(uint64_t*)(in->data)`.
2. Смещение файла (offset) лежит в `*(uint64_t*)(in->data + 8)`.
3. Обязательно вызывайте `shm_map`, проверяйте на `NULL`, а перед выходом — `shm_free`.

### Пример реализации `VFS_CMD_READ`
```c
int myfs_read(message_t* in, uint64_t* out_bytes_read) {
    uint64_t fd = in->param2;
    uint64_t size = in->param3;
    uint64_t shm_id = *(uint64_t*)(in->data);
    uint64_t offset = *(uint64_t*)(in->data + 8);
    
    void* buf = shm_map(shm_id);
    if (!buf) return VFS_ERR_UNKNOWN;
    
    // ... Логика чтения данных в buf на основе fd и offset ...
    // Например, копируем строку "Hello"
    const char* text = "Hello\n";
    uint64_t len = strlen(text);
    
    if (offset >= len) {
        *out_bytes_read = 0; // EOF
    } else {
        uint64_t to_copy = size;
        if (offset + to_copy > len) to_copy = len - offset;
        memcpy(buf, text + offset, to_copy);
        *out_bytes_read = to_copy;
    }
    
    shm_free(shm_id); // ОБЯЗАТЕЛЬНО!
    return VFS_ERR_OK;
}
```

### Пример реализации `VFS_CMD_STAT`
```c
int myfs_stat(message_t* in) {
    uint64_t fd = in->param2;
    uint64_t shm_id = *(uint64_t*)(in->data);
    
    fs_stat_t* stat_buf = (fs_stat_t*)shm_map(shm_id);
    if (!stat_buf) return VFS_ERR_UNKNOWN;
    
    memset(stat_buf, 0, sizeof(fs_stat_t));
    
    if (is_directory(fd)) {
        stat_buf->attributes = 0x10; // Флаг директории
        stat_buf->size_bytes = 0;
    } else {
        stat_buf->attributes = 0;
        stat_buf->size_bytes = get_file_size(fd);
    }
    
    // inode_id используется VFS для блокировок (flock). 
    // Должен быть уникальным для каждого файла!
    stat_buf->inode_id = generate_unique_inode(fd);
    
    shm_free(shm_id);
    return VFS_ERR_OK;
}
```

### Пример реализации `VFS_CMD_LIST`
```c
int myfs_readdir(message_t* in, uint64_t* out_entries_read, uint8_t* out_new_offset_data) {
    uint64_t fd = in->param2;
    int max_entries = in->param3;
    uint64_t shm_id = *(uint64_t*)(in->data);
    uint64_t offset = *(uint64_t*)(in->data + 8); // Курсор
    
    if (offset == (uint64_t)-1) {
        *out_entries_read = 0;
        return VFS_ERR_OK;
    }
    
    fs_dirent_t* dirents = (fs_dirent_t*)shm_map(shm_id);
    if (!dirents) return VFS_ERR_UNKNOWN;
    
    int count = 0;
    
    // ... Логика перебора файлов в папке, начиная с индекса 'offset' ...
    while (count < max_entries && has_more_files(fd, offset)) {
        strlcpy(dirents[count].name, get_filename(fd, offset), 256);
        dirents[count].size = get_filesize(fd, offset);
        dirents[count].type = is_dir(fd, offset) ? VFS_FILE_TYPE_DIR : VFS_FILE_TYPE_REGULAR;
        
        count++;
        offset++; // Сдвигаем курсор
    }
    
    if (!has_more_files(fd, offset)) {
        offset = (uint64_t)-1; // Конец директории
    }
    
    // Возвращаем новый offset через поле data ответного сообщения
    *(uint64_t*)(out_new_offset_data) = offset;
    *out_entries_read = count;
    
    shm_free(shm_id);
    return VFS_ERR_OK;
}
```

## 5. Ограничения

1. **Блокировки (Deadlocks):** Если ваш драйвер ФС делает синхронные вызовы к другим драйверам (например, запрашивает данные по сети), убедитесь, что вы не блокируете главный цикл обработки IPC надолго, иначе VFS (и все приложения, ждущие ответа от VFS) зависнут.
2. **Утечки SHM:** Забытый `shm_free` приведет к исчерпанию виртуального адресного пространства вашего драйвера (OOM).
3. **Безопасность:** VFS доверяет вашему драйверу. Если вы вернете некорректный размер в `VFS_CMD_READ`, клиентское приложение может упасть. Тщательно проверяйте границы буферов.