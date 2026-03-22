import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Использование: python font2c.py <файл_шрифта>")
        print("Пример: python font2c.py vga8x16.bin > font.c")
        return

    filepath = sys.argv[1]

    if not os.path.exists(filepath):
        print(f"Ошибка: файл '{filepath}' не найден.")
        return

    # Проверка размера
    size = os.path.getsize(filepath)
    if size != 4096:
        print(f"// ВНИМАНИЕ: Размер файла {size} байт.")
        print(f"// Стандартный шрифт VGA 8x16 должен быть ровно 4096 байт.")
        print(f"// Если это Windows .FON (Executable), результат будет некорректным.")
        print(f"// Используйте программу типа Fony для экспорта в 'Raw Binary'.")
        # Мы все равно продолжим, вдруг шрифт нестандартный

    try:
        with open(filepath, "rb") as f:
            data = f.read()
    except Exception as e:
        print(f"Ошибка чтения файла: {e}")
        return

    # Заголовок C-файла
    print("#include <stdint.h>\n")
    print(f"// Сгенерировано из файла: {os.path.basename(filepath)}")
    print("const uint8_t fonttest[256][16] = {")

    # Обработка символов
    # Мы ожидаем 256 символов
    for char_idx in range(256):
        start = char_idx * 16
        end = start + 16
        
        # Получаем 16 байт для текущего символа
        if start < len(data):
            chunk = data[start:end]
        else:
            chunk = bytes([0] * 16) # Если файл короче, добиваем нулями

        # Если кусок меньше 16 байт (конец файла), добиваем нулями
        if len(chunk) < 16:
            chunk = chunk + bytes([0] * (16 - len(chunk)))

        # Формируем строку hex-кодов: 0x00, 0xFF, ...
        hex_values = ", ".join(f"0x{b:02X}" for b in chunk)
        
        # Генерируем комментарий (какой это символ)
        if 32 <= char_idx <= 126:
            char_display = chr(char_idx)
            # Экранирование для C-комментария
            if char_display == '\\': char_display = '\\\\'
            comment = f"'{char_display}'"
        else:
            comment = f"#{char_idx}"

        print(f"    {{ {hex_values} }}, // {comment}")

    print("};")

if __name__ == "__main__":
    main()