#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_WND, DRV_PERM_GET_SPEC_INFO, 0);

sys_video_t vinfo;
uint32_t* backbuffer = 0;

// Вспомогательная функция для генерации цвета
uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << vinfo.red_mask_shift) |
           ((uint32_t)g << vinfo.green_mask_shift) |
           ((uint32_t)b << vinfo.blue_mask_shift);
}

// Отрисовка прямоугольника в ТЕНЕВОЙ БУФЕР (в оперативную память)
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x >= vinfo.width || y >= vinfo.height) return;
    if (x + w > vinfo.width) w = vinfo.width - x;
    if (y + h > vinfo.height) h = vinfo.height - y;

    for (uint32_t cy = y; cy < y + h; cy++) {
        uint32_t* row = (uint32_t*)((uint8_t*)backbuffer + (cy * vinfo.pitch));
        for (uint32_t cx = x; cx < x + w; cx++) {
            row[cx] = color;
        }
    }
}

// Функция для отрисовки "статичного" интерфейса (обоев и окон)
void draw_desktop_ui(uint32_t clip_x, uint32_t clip_y, uint32_t clip_w, uint32_t clip_h) {
    // 1. Фон (Темно-синий)
    draw_rect(clip_x, clip_y, clip_w, clip_h, make_color(0, 64, 128));

    // 2. Главное окно (Серое с белым заголовком)
    // Чтобы не перерисовывать всё окно целиком (оно большое), мы рисуем его
    // поверх нашего квадратика только в тех границах (clip), где была мышка.
    // Пока упрощаем: рисуем всё, так как draw_rect сама сделает отсечение по краям clip.
    // В будущем тут будет нормальный Clipping.
    
    // Но чтобы окно не пропадало под мышкой, просто рисуем его заново 
    // в области затирки (упрощенный вариант):
    
    uint32_t win_x = 100, win_y = 100, win_w = 500, win_h = 350;
    
    // Проверка, попадает ли область перерисовки (clip) на окно (базовый AABB collision)
    if (clip_x < win_x + win_w && clip_x + clip_w > win_x &&
        clip_y < win_y + win_h && clip_y + clip_h > win_y) {
        
        draw_rect(win_x, win_y, win_w, win_h, make_color(200, 200, 200)); // Тело
        draw_rect(win_x, win_y, win_w, 30,  make_color(255, 255, 255)); // Заголовок
        draw_rect(win_x + win_w - 30, win_y + 5, 20, 20, make_color(220, 50, 50)); // Кнопка Х
    }
}

int driver_main(void* reserved1, void* reserved2) {
    printf("WindowManager: Starting...\n");

    // 1. Инициализируем библиотеку видеодрайвера
    video_init();

    // 2. Получаем параметры экрана
    if (sysget_spec_info(SPEC_INFO_VIDEO, &vinfo) != SYS_RES_OK) {
        printf("WindowManager: Failed to get video info!\n");
        return -1;
    }

    // 3. Выделяем память под Backbuffer
    uint64_t size_bytes = vinfo.height * vinfo.pitch;
    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(size_bytes, &shm_vaddr);
    
    if (!shm_id) return -1;
    backbuffer = (uint32_t*)shm_vaddr;

    // 4. Передаем Backbuffer видеодрайверу
    if (video_set_backbuffer(shm_id) != 0) {
        printf("WindowManager: Failed to set backbuffer in driver!\n");
        return -1;
    }

    // 5. Отрисовываем ПЕРВЫЙ КАДР (весь экран)
    draw_rect(0, 0, vinfo.width, vinfo.height, make_color(0, 64, 128)); // Фон
    draw_desktop_ui(0, 0, vinfo.width, vinfo.height); // Окна
    
    video_rect_t full_screen = {0, 0, vinfo.width, vinfo.height};
    video_flush_rects(&full_screen, 1);

    // =========================================================
    // ШАГ 4: ПОДПИСКА НА СОБЫТИЯ ВВОДА
    // =========================================================
    
    apid_t input_pid = get_driver_pid(DT_INPUT);
    if (input_pid == 0) {
        printf("WindowManager: ERROR - InputDriver not found!\n");
        return -1;
    }

    message_t sub_msg;
    memset(&sub_msg, 0, sizeof(message_t));
    sub_msg.type = MSG_TYPE_INPUT;
    sub_msg.subtype = INPUT_CMD_SUBSCRIBE;
    ipc_send(input_pid, &sub_msg);
    
    printf("WindowManager: Subscribed to Input events.\n");

    // Начальная позиция мыши (по центру экрана)
    int mouse_x = vinfo.width / 2;
    int mouse_y = vinfo.height / 2;
    int mouse_w = 12;
    int mouse_h = 18;
    uint32_t cursor_color = make_color(255, 100, 0); // Оранжевый курсор

    // Отрисуем мышку при старте
    draw_rect(mouse_x, mouse_y, mouse_w, mouse_h, cursor_color);
    video_flush_rects(&full_screen, 1);

    // Главный цикл обработки событий
    message_t msg;
    while (1) {
        // Процессор засыпает здесь (0% CPU), пока не придет сообщение!
        ipc_recv(&msg);

        if (msg.type == MSG_TYPE_INPUT && msg.subtype == INPUT_EVENT_MOUSE) {
            // ВАЖНО: Приводим к int16_t, чтобы отрицательные числа (движение влево/вверх) 
            // правильно распаковались и восстановили свой знак.
            int16_t dx = (int16_t)(msg.param1 & 0xFFFF);
            int16_t dy = (int16_t)((msg.param1 >> 16) & 0xFFFF);
            // uint8_t buttons = msg.param2; // Левая/Правая кнопка (понадобится позже)

            // Если мышь не сдвинулась (просто клик), ничего не перерисовываем
            if (dx == 0 && dy == 0) continue;

            int old_x = mouse_x;
            int old_y = mouse_y;

            // Обновляем координаты
            mouse_x += dx;
            mouse_y += dy;

            // Отсечение (чтобы курсор не улетел за пределы экрана)
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > vinfo.width - mouse_w) mouse_x = vinfo.width - mouse_w;
            if (mouse_y > vinfo.height - mouse_h) mouse_y = vinfo.height - mouse_h;

            // --- ОТРИСОВКА (Damage Tracking) ---
            
            // 1. ЗАЧИЩАЕМ старое место (рисуем фон и кусок окна поверх старой мыши)
            draw_rect(old_x, old_y, mouse_w, mouse_h, make_color(0, 64, 128)); // Фон
            draw_desktop_ui(old_x, old_y, mouse_w, mouse_h); // Окна восстанавливаем

            // 2. РИСУЕМ курсор на новом месте
            draw_rect(mouse_x, mouse_y, mouse_w, mouse_h, cursor_color);

            // 3. ОТПРАВЛЯЕМ "DAMAGE" (изменения) в видеодрайвер
            video_rect_t damage[2];
            
            damage[0].x = old_x; 
            damage[0].y = old_y;
            damage[0].w = mouse_w; 
            damage[0].h = mouse_h;

            damage[1].x = mouse_x; 
            damage[1].y = mouse_y;
            damage[1].w = mouse_w; 
            damage[1].h = mouse_h;

            // Вызываем нашу крутую библиотеку! Драйвер перерисует только эти две зоны.
            video_flush_rects(damage, 2);
        }
        else if (msg.type == MSG_TYPE_INPUT && msg.subtype == INPUT_EVENT_KEY) {
            uint8_t scancode = msg.param1;
            printf("WindowManager: Key pressed: 0x%X\n", scancode);
            // Здесь в будущем будем обрабатывать клавиатуру (Alt+Tab, ввод текста)
        }
    }

    return 0;
}