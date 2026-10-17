# Дополнения статической libc для Chromium (2026-10-17)

Старые функции/типы libc не меняются; новых динамически загружаемых библиотек
нет. Новые вызовы ядра — только syscall ABI 1.12, см. time-random.md.
Потоки — libc-threads.md. Стандартные C-типы ниже нужны настоящему
third_party/libc++/src/include/stdlib.h из закреплённого Chromium.

div_t содержит int quot, rem (8 байт, rem offset 4), ldiv_t — long quot, rem
(16 байт, rem offset 8), lldiv_t — long long quot, rem (16 байт, rem offset 8).
div/ldiv/lldiv возвращают частное с округлением к нулю и остаток того же
знака, что числитель. Деление на 0 и непредставимое частное не определены.

assert.h: при NDEBUG выражение не вычисляется; иначе неверное условие вызывает
__charm_assert_fail с текстом, файлом, строкой, функцией. Реализация печатает
ограниченную диагностику в debug console и abort завершает процесс кодом 134.
Повторное включение заголовка учитывает смену NDEBUG. Полное соответствие
ISO C/POSIX для всей libc не заявляется.

Реальный syntax probe getentropy.cc BoringSSL прошёл с bundled libc++,
Generic ELF target, без Linux-заголовков. Следующий thread_pthread.cc пока
не проходит: нет полноценного wchar/mbstate_t и части C stdio интерфейсов.
Это конкретный следующий блок runtime; TLS/HTTPS пока не собраны.
