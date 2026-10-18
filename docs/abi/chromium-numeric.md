# Числа и локаль для native Chromium (статический runtime v1)

Это расширение M10. Новых syscalls/IPC нет. Own libc сохраняется; необходимая
Chromium числовая зависимость собирается из закреплённого LLVM-libc отдельно
в GN numeric_runtime, с исходными лицензиями. Не используются host libc/libm
или Linux ABI. Пока numeric_runtime не связан, его функции не доступны другим
приложениям автоматически. Пустых реализаций математических функций нет.

math.h объявляет ISO C float/double/long double интерфейс, классификацию через
compiler builtins, IEEE infinity/NaN. LLVM-libc обеспечивает реально выбранные
единицы math, strtof/strtod/strtold и fenv; перечень исходников явный.
errno использует существующий потоковый __errno_location (upstream system-inline
mode), без compiler TLS. EDOM=33, ERANGE=34. strto* сохраняют endptr, проверяют
границы, rounding mode и диапазон; реализация upstream, не приближённый парсер.

fenv_t — opaque 32 байта (align 1), fexcept_t — unsigned short (2 байта).
Состояние включает x87 environment (28 байт) и MXCSR (4 байта). FE_DIVBYZERO=1,
FE_INEXACT=2, FE_INVALID=4, FE_OVERFLOW=8, FE_UNDERFLOW=16, FE_DENORM=32,
FE_ALL_EXCEPT=63; это публичные маски, не необработанные биты hardware status.
FE_TONEAREST=0, FE_DOWNWARD=0x400, FE_UPWARD=0x800, FE_TOWARDZERO=0xc00.
FE_DFL_ENV — sentinel (fenv_t*)-1. Используются реальные upstream x86_64 fenv
операции. Окружение принадлежит потоку и сохраняется прежним FPU context switch.
В связанном numeric профиле pthread_create сохраняет окружение вызывающего
потока и восстанавливает его в trampoline нового, после установки TLS.
Сохранение через optional weak fegetenv/fesetenv; профиль явно линкует оба
символа. Для программ без numeric dependency старое поведение не меняется.
Произвольное изменение opaque saved environment пользователем не поддерживается.

Начальный locale backend ограничен C/POSIX форматированием и фиксированной
UTF-8 кодировкой существующей libc. Принимаются C, POSIX, C.UTF-8, C.utf8 и
пустое имя; другие имена — ENOENT. Это явно ограниченный backend, не полная
локализация POSIX: именованные национальные локали, Unicode collation и
Unicode case folding будут обслуживаться Chromium ICU. C/POSIX — алиасы
этого fixed UTF-8 backend, существующий UTF-8 контракт wchar не меняется.
Классификация C/wctype — ASCII; остальные scalar не становятся буквами по
предположению. Byte/wide collation — сравнение кодовых единиц. Decimal point
".", группировки/денежной единицы нет; недоступные lconv числа — CHAR_MAX.

locale_t — opaque pointer к неизменяемому объекту фиксированной локали.
LC_GLOBAL_LOCALE=(locale_t)-1; uselocale сохраняет выбор в ThreadBlock и
возвращает предыдущий, nullptr только запрашивает его. newlocale проверяет
mask/name/base; freelocale/duplocale работают с этими фиксированными объектами.
Категории LC_CTYPE..LC_MESSAGES=0..5, LC_ALL=6, MASK=(1<<category), ALL_MASK=63.
Ошибки не меняют состояние. Глобальная локаль не требует динамической памяти.
libc++ использует существующий portable fixed-locale support header и штатную
default rune table, без __Fuchsia__/__linux__ identity defines.

Проверки: реальные guest parsing/range/rounding, fenv save/restore/flags,
locale errors/TSD, narrow/wide strings и libc++ runtime. Работа этих функций
сама по себе не является работой Chromium base или браузерного окна.

Профиль libc++ для порта сохраняет localization, Unicode/wchar и pthread,
но явно выключает std::filesystem: существующий VFS не предоставляет его
symlink/metadata/space API. Это не выключение файлов Chromium: их собственный
base/files backend должен работать с native VFS и сообщать неподдерживаемые
операции. Нельзя придумывать inode/atime/statvfs для прохождения компиляции.
hardware_concurrency возвращает разрешённое C++ значение 0 (неизвестно), пока
native CPU-count API не предоставлен; steady_clock использует настоящий
CLOCK_MONOTONIC, system_clock — CLOCK_REALTIME.

Статическая optional stdio engine dependency использует LLVM-libc formatter и
scanner. Собственная libc вызывает её через внутренние weak hooks, только
если приложение явно связано с этим runtime. Другие программы сохраняют
собственную реализацию форматирования. Narrow printf получает реальные
float/long double, rounding, width/precision и snprintf bounds. asprintf /
vasprintf выделяют результат через свою malloc, при ошибке *out=nullptr.
Wide swprintf на данном этапе поддерживает числовые спецификаторы; строковые,
символьные и %n дают явный ENOSYS. Это ограничение не скрывается успешным
результатом. std::to_wstring использует именно числовые спецификаторы.

stdio дополнение: byte/wide orientation, UTF-8 fgetwc/fputwc, pushback до
8 байт (минимум один wchar), fgetpos/fsetpos и 64-bit fseeko/ftello.
fpos_t содержит off_t и mbstate_t (24 байта, state offset 8). Ориентация
фиксируется первой операцией; попытка смешать byte/wide даёт EINVAL/error.
Успешный seek снимает EOF/pushback и сбрасывает conversion state. Ошибки
размера/offset обнаруживаются до переполнения. strftime — C names, UTC,
ISO C/POSIX спецификаторы включая ISO week/year и E/O fallback; неподдержанный
specifier/неверные поля дают 0/EINVAL, тесный буфер даёт 0 с NUL prefix.
Фиксированное форматирование не является поддержкой часовых зон.

Семантика: [strftime](https://man7.org/linux/man-pages/man3/strftime.3.html),
[newlocale](https://man7.org/linux/man-pages/man3/newlocale.3.html),
[bounded multibyte conversion](https://man7.org/linux/man-pages/man3/mbsnrtowcs.3.html).

Спецификация FPU: docs/specs/intel-sdm-vol2.pdf, FLDCW/FLDENV,
FSTCW/FNSTCW, FSTENV/FNSTENV, STMXCSR/LDMXCSR. Алгоритмы numeric — pinned
third_party/llvm-libc/src с сохранением copyright и LLVM license.
