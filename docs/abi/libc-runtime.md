# Дополнения статической libc для Chromium (2026-10-17)

Новых динамически загружаемых библиотек
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
Generic ELF target, без Linux-заголовков. thread_pthread.cc также проходит
после UTF-8 и pthread v2 дополнений. TLS/HTTPS пока не собраны.

## UTF-8 / wchar дополнение

Кодировка новых multibyte функций фиксирована UTF-8; wchar_t на x86-64 —
32-битный Unicode scalar. mbstate_t: u32 value, u32 minimum, u8 remaining,
u8 total, u16 reserved, 12 байт; нулевая инициализация означает начало.
Суррогаты, overlong, значения выше U+10FFFF и неверные продолжения дают
EILSEQ. Частичный символ сохраняется между вызовами; mbrtowc возвращает
число байтов текущего вызова, -2 при неполном вводе, -1 при ошибке, 0 для NUL.
Неявные состояния принадлежат потоку. Ошибка сбрасывает состояние.
При dst=nullptr строковые преобразования считают длину без изменения src
и состояния; ограниченный вывод не разрывает UTF-8 символ.
Добавляются реальные wide string/memory операции, преобразования и remove;
полная локализация, wide formatted I/O и filesystem rename не заявляются.

Источники: [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629.html),
[mbrtowc](https://man7.org/linux/man-pages/man3/mbrtowc.3.html),
[wcsrtombs](https://man7.org/linux/man-pages/man3/wcsrtombs.3.html).

## Календарь и C runtime

tm содержит девять int полей ISO C (36 байт). gmtime_r переводит signed
Unix seconds в UTC Gregorian, годы 1..9999 включительно; за границами
EOVERFLOW и nullptr, выход не изменяется. gmtime хранит результат в блоке
текущего потока. tm_isdst=0, tm_wday=0 для воскресенья, tm_yday с нуля.
Часовые зоны, localtime, mktime, strftime пока не реализованы.
timeval: time_t seconds + long microseconds, 16 байт. gettimeofday берёт
CLOCK_REALTIME, tv_usec 0..999999; nullptr output — EFAULT, ненулевой
устаревший timezone argument — EINVAL. При ошибке output не меняется.
bsearch выполняет бинарный поиск в сортированном массиве; пустой массив
возвращает nullptr. inttypes.h предоставляет PRI format macros для LP64;
SCN и strtoimax пока не заявляются. remove использует существующий VFS unlink.

## Размер полезной области аллокации

`malloc.h` предоставляет `malloc_usable_size(void*)`: для nullptr — 0,
для живого указателя malloc/calloc/realloc/aligned_alloc/posix_memalign —
размер доступной области без служебного заголовка. Для обычного блока это
ёмкость класса или отображения минус заголовок; для выровненного — размер,
записанный при его создании (posix_memalign округляет до alignment).
Вся возвращённая область доступна для чтения/записи. realloc сохраняет её
при переносе в больший блок. Запрос не меняет errno и не выделяет память.
Чужой, освобождённый или смещённый указатель недопустим. Параллельное
освобождение того же блока требует внешней синхронизации.
Это ёмкость одного блока, а не RSS, размер VMO или статистика всего процесса.

## Выравнивание памяти и сообщения ошибок (дополнение v1.1)

aligned_alloc(alignment, size) принимает ненулевую степень двойки и размер,
кратный alignment; неверные параметры дают nullptr/EINVAL, переполнение и
нехватка памяти — nullptr/ENOMEM. Малые выравнивания 1/2/4 также допустимы.
posix_memalign дополнительно требует alignment >= sizeof(void*), возвращает
код ошибки напрямую, сохраняет errno и не изменяет *memptr при ошибке.
Нулевой размер возвращает освобождаемый уникальный указатель. Обе функции
совместимы с free/realloc; realloc сохраняет данные, но при переносе не
обещает исходного увеличенного выравнивания. Метаданные кучи приватны.

strerror_r имеет только XSI сигнатуру int(int, char*, size_t), без GNU
варианта даже при _GNU_SOURCE. Возвращает 0, EINVAL для неизвестного кода,
ERANGE для недостаточного буфера; errno сохраняет. При size > 0 строка
всегда завершается NUL, при size == 0 буфер не трогается. ERANGE имеет
приоритет над неизвестным кодом; nullptr при ненулевом размере — EINVAL.
strerror сохраняет errno и возвращает неизменяемый текст. Для каждого
определённого в errno.h кода есть сообщение; ELAST — наибольший код (111),
промежутки в нумерации не означают существование ошибок.

Источники: [posix_memalign](https://man7.org/linux/man-pages/man3/posix_memalign.3.html),
[strerror_r](https://man7.org/linux/man-pages/man3/strerror.3.html).
