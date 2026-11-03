# Актуальное продолжение (2026-10-29, WM v3 + Ozone canvas)

ПРОВЕРЕНО: WM v3, user SDK create_managed/request_bounds/title/visibility/focus,
асинхронная замена двух VMO с поколениями/откатом и FIFO64 с writable watch.
Спека docs/abi/wm-v3.md. v2 сохранён. Window::create по умолчанию v2.
Полная OS сборка managed-os-build.log, затем m10/window_managed PASS:
resize cycles, short/no-map rollback, invalid generation disconnect,
stalled consumer FIFO overflow isolation, input/repaint after resize.
Первая проверка отказа ошиблась в тесте: VmoCreate требует page-aligned size;
исправлен тест, НЕ ослаблены проверки сервера. managed-fix-build.log.
managed-regression.log: PASS window_pixels, graphics_runtime, runtime_cpp,
base_runtime, windows; graphics_runtime пересобрал только2 C++ и LINK.
Снимок native-managed-window.png просмотрен: реальное окно400x220 после resize.

НОВЫЙ ПРОВЕРЕННЫЙ ПАКЕТ: SurfaceOzoneCanvasCharmOS наследует настоящий
upstream SurfaceOzoneCanvas. NativeWindowPresenterCharmOS владеет Window
и FD watcher на UI sequence; immutable SkImage snapshot идёт с render thread,
реальный FrameDone возвращает posted callback на render thread. Один pending
swap, ошибочный viewport/damage/конкурентныйswap возвращает empty gfx::Size.
RefCountedDeleteOnSequence удерживает UI владение. VSyncProvider nullptr:
не выдаём estimated timer за scanout. Спека docs/abi/chromium-canvas.md.
Новые source_set native_canvas + executable canvas_runtime в platform/BUILD.gn.
Граф geometry — настоящий upstream target, не локальные копии реализации.
Тест m10/canvas_runtime PASS, actual ELF strip+audit, resized480x280,
inputA и Esc. Native pixel QA green24a148 x180/230,y270; goldf5c518 x430,y270;
background111927 x140,y200. Снимок native-ozone-canvas.png просмотрен.

tools/chromium/verify-canvas-block.sh воспроизводит prepare, GN, graph audit,
ninja canvas_runtime, ELF audit, guest test. Первая попытка GN потребовала
--root; исправлено. Затем38 компиляций прошли, но лог был перезаписан no-work:
НЕ редактировать исполняемый shellscript во время его запуска — bash может
перечитать смещённые строки и повторить команды. Результат гостевого теста
подлинный, screenshot pixel QA зелёный. audit canvas closure session55545
завершён0: no Rust,934 deps/4161 commands. Compiler/QEMU НЕ запущены.

ДАЛЬШЕ: полного Ozone backend НЕТ. Canvas работает напрямую с presenter
внутри процесса, это НЕ браузер и не готовый SurfaceFactory/PlatformWindow.
Нужны PlatformWindow/delegate/inputtranslation, widget registry (uint32 IDs,
не pointers), SurfaceFactory и OzonePlatform registration. WMv3 ещё не имеет
capture/fullscreen/borderless/customcursor; не подменять их успехом/noop.
Затем Mojo/engine IPC consumers, Blink/V8/сеть и браузер UI. Пользователь
попросил довести до конца, тратить мало текста, НЕ компилировать мелкие
правки: собирать завершёнными пакетами/при критической ошибке.

# Предыдущее продолжение (2026-10-29, графический пакет)

ГРАФИЧЕСКИЙ ПАКЕТ ПРОВЕРЕН: session49326 завершена0. Все ELF реально связаны,
strip+audit, затем PASS m10/window_pixels, m10/graphics_runtime,
browser/runtime_cpp, browser/base_runtime. Логи build/chromium/graphics-guest-*.log.
Снимок настоящего окна build/chromium/native-skia.png (1280x800) просмотрен:
реальные шрифты/фигуры/Native key A в WM, не макет и НЕ браузер/HTML.
Конвертация через bundled Windows Python/Pillow (WSL Pillow не установлен).
Компиляторов и QEMU после этих четырёх тестов нет. Следом отдельно m10/windows
как регрессия изменения общего Window SDK; см. graphics-windows-regression.log.

Последние исправления: native allocator_charmos UncheckedMalloc/Calloc/Free,
шесть настоящих double hyperbolic через pinned third_party/fdlibm (LLVM units
отсутствуют), errno/fenv фасад и гостевые проверки, свой process-local semaphore
runtime (signal post, timeout, overflow, 1000 token handoffs, 8 workers),
sig_atomic_t в signal.h. Все эти проверки реально прошли runtime_cpp.
Изменение signal.h обоснованно пересобрало1904 зависимых единицы, НЕ повторять
компилятор без изменений. Проверка ninja -n -d explain показала: изменение
native libc/WM archives требует лишь3 LINK (без перекомпиляции C++). Все3
ELF перелинкованы после последнего OS build. graphics-runtime-link.log,
graphics-os-final.log, graphics-relink.log, graphics-native-archive-explain.log.

VMO bugfix WM: проверяет размер обоих буферов до использования, освобождает
частичные maps/ring при отказе, чистит окно при ошибке передачи Created.
window_pixels реально тестирует короткий/неотображаемый буфер и нормальное
окно после отказов. SDK резервирует buffer1 до первого FRAME_DONE1, иначе
старый initial ACK мог бы повторно открыть уже отправленный второй буфер.
Это правило v2 уточнено в docs/abi/wm.md, baseline clients начинают с0.
WindowEventSourceCharmOS на настоящем Chromium UI pump проверен с restart
из собственного callback, вводом A -> отображённый кадр и Stop из Esc callback.

tools/chromium/verify-graphics-block.sh теперь воспроизводит проверку5 fixtures
(последний windows добавлен после первого прохода). Не гонять всё без причины.
WSL commands из PowerShell с вложенными $переменными в bash -lc могут терять
значения (так первая попытка relink получила /out/charmos-platform). Для
нетривиальных shell команд писать .sh и запускать напрямую: это уже исправлено
в verify script. Ошибочные curl loops тоже могли терять $name; не считать их
404/403 доказательством отсутствия спецификации. sem_wait.html сохранён подлинно.

ДАЛЬШЕ: настоящий Ozone backend ещё НЕ написан. Остались PlatformWindow,
SurfaceOzoneCanvas/SurfaceFactory с реальными frame ACK и thread ownership,
UI input translation, затем Mojo/engine IPC consumers, Blink/V8 и браузер UI.
WMv2 пока не имеет resize/title/show-hide/capture команд. Потребуется честное
расширение протокола или явно ограниченный начальный native profile; не выдавать
void setters/no-op за работающие функции. gfx AcceleratedWidget uint32 — не
передавать native pointers через процессы. Для software/in-process GPU можно
использовать защищённый registry; separate GPU process требует настоящего IPC.
Текущий kernel/libc также не объявляют готовую executable/JIT reservation API;
проверить перед включением V8. Полностью готового браузера по-прежнему НЕТ.

Текущий пакет: реальный Chromium Skia CPU + C++ PNG/BMP/ICO/JPEG/WebP,
FreeType, C++ HarfBuzz, Skottie/PDF/XMP без Rust. graphics-patches.py подключён
к hash-checked prepare; изменения должны добавляться staged replacements,
не редактировать уже применённые replacement-тексты: иначе защита vendor
правильно отвергнет старую подготовленную версию. Лицензии сохранены/проверяются.
Граф graphics_runtime: 932 зависимости, 4123 команды, без Rust; graphics-graph2.log.

Сейчас активна session17351: graphics-build3.log, второй фактический проход
компилятора (graphics-build2 остановился на GN до компиляции). НЕ запускать
другую сборку/QEMU/prepare параллельно. Первый проход1429 шагов завершён,
выявил malloc.h, ошибочный default macOS Skia для неизвестной ОС, mac utils
и отсутствующий прямой //base у probe. Исправления: свой malloc_usable_size,
SK_BUILD_FOR_UNIX (native libc POSIX, IS_LINUX/IS_APPLE всё ещё false), удаление
mac/win utils, прямой deps//base. graphics-os2.log уже успешно.

Добавлен настоящий setjmp/longjmp в libc arch/x86_64 по сохранённому C11 N1570
и psABI (docs/abi/nonlocal-jumps.md), C11 PDF/источник/хеш в docs/specs.
runtime_cpp новые проверки nonlocal_jump_checks.c и usable capacity пока
НЕ запускались; expected обновлён. После graphics собрать full_runtime
на той же args.gn напрямую ninja (не toggling graphics flag), strip/audit и
tools/test.sh browser 150 runtime_cpp. Не забыть это: сейчас --graphics
сам full_runtime не собирает.

graphics_runtime создаёт настоящее WM окно, Skia raster, FreeType glyphs,
PNG roundtrip+truncated stream, WindowEventSourceCharmOS на Chromium UI pump,
проверяет key A -> новый кадр -> FRAME_DONE, потом Esc. Fixture m10/graphics_runtime
ставит реальные шрифты и notices, сохраняет build/chromium/native-skia.ppm.
EventSource owns readiness FD, unregister before close, callback может Stop/
restart/delete source через WeakPtr. Это glue для будущего Ozone, НЕ Ozone backend.
После успешной сборки tools/test.sh m10 150 window_pixels и graphics_runtime,
получить/посмотреть снимок. Ozone/движок/браузерного окна по-прежнему НЕТ.
Меньше вывода пользователю. Не повторять сборки по мелким изменениям.

ПЕРВАЯ УСПЕШНАЯ БАЗА CHROMIUM: emutls-base.log 2204/2204 + link/audit,
emutls-base-guest.log browser/base_runtime PASS. session1934 завершена0.
В госте реально прошли strings/JSON/files/tasks/native IO pump, VMO mappings/
restricted rights/child switch transfer, native process launch/query/waits,
file allocation preserving contents/offset, metrics, SyncSocket partial reads/
backpressure/shutdown и NativeChannel child payload+read-only VMO transfer.
Теперь base_runtime ELF действительно есть в build/user/user/base_runtime.elf.
Это доказанная база, НЕ браузер и НЕ отрисованный HTML.
Полный runtime с compiler TLS/WatchFD тоже PASS (emutls-runtime-guest.log).
Активных compiler/QEMU после пакета нет. Следующий большой пакет — GUI/Skia/
Ozone и затем engine consumers; present_pixels и m10/window_pixels ещё не
проверены. Не запускать компилятор ради этих ~100 строк в одиночку.

НОВЕЙШЕЕ: emutls-os.log успешно. Первая GN ошибка удаления отсутствующего
cxa_thread_atexit.cpp исправлена вторым staged patch (native target его и так
не выбирает). В TLS тесте локально разрешён intentional -Wexit-time-destructors,
иначе эти обязательные destructor probes отвергал Chromium -Werror. Это не
подавление production ошибок. emutls-runtime.log/full ELF audit успешно;
emutls-runtime-guest.log/runtime_cpp PASS, включая новый compiler TLS тест,
одноразовые startup caps, все каналы и readiness FD. Теперь эти части ПРОВЕРЕНЫ.

Активна session1934: emutls-base.log полный rebuild (2204 шагов), затем
emutls-base-guest.log. Не запускать второй compiler/QEMU, не prepare vendor
во время ninja. Сессии38679/64118 завершены, старые статусы ниже устарели.

Пока base собирается, написан следующий GUI block без компиляции:
Window::present_pixels с source size/stride/damage/format/alias validation,
копированием полного кадра в owned backbuffer и RGBX/BGRX conversion,
WouldBlock когда оба буфера заняты. SDK/docs обновлены. user/tests/window_pixels
и m10/window_pixels fixture проверяют padded rows, conversion, invalid inputs,
double buffer ownership, frame ACK через poll readiness и повторный present.
Этот helper/GUI test ЕЩЁ НЕ СОБРАН и НЕ ПРОВЕРЕН; не перезапускать compiler
после этих ~100 строк, продолжать связанный графический пакет.
Исследованы native Ozone interfaces; CurrentUIThread уже поддерживает fd
watcher на IS_OZONE (нет нужды патчить). SurfaceOzoneCanvas применим для
software output. Но полноценный Ozone backend НЕ НАПИСАН. WM v2 пока без
resize/title/capture протокола. Skia155 содержит обязательные Rust PNG и
Fontations/прочиеcodec branches: для C++-only нужны реальные C++ backend,
не просто выключение кодеков/шрифтов. В архиве есть C++ JPEG и FreeType,
C++ libpng decoder на самом деле ЕСТЬ: third_party/skia/src/codec/SkPngCodec.cpp,
include/codec/SkPngDecoder.h, src/encode/SkPngEncoderImpl.cpp. Предыдущий
вывод об отсутствии ошибочен: поиск был обрезан. Также есть C++ BMP/ICO/JPEG
и third_party/libpng. Нужно выбрать эти реальные backend в native GN вместо
Rust branches, сохраняя лицензии. Браузер всё ещё далеко от готовности.

Последующие регрессии emutls-threads-guest.log (m8/threads),
emutls-signals-guest.log (m8/signals), emutls-windows-guest.log (m10/windows)
прошли, session50687 завершена0. Активных сборок и QEMU нет.
Для libpng ещё нужен настоящий setjmp/longjmp: реализации пока нет.
Нормативный C11 N1570 найден: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
Перед реализацией сохранить в docs/specs и изучить 7.13 вместе с x86-64 psABI.

ПОСЛЕДНЕЕ УКАЗАНИЕ: владелец просит МЕНЬШЕ ТЕКСТА/ТОКЕНОВ в выводе. Статусы
короткие, логи целиком не печатать. На вопрос «почти готов?» прямо ответили НЕТ:
окна/веб-страниц нет, остаются графика/ввод, IPC потребители движка, веб-движок/UI.

channel-base.log: все native backends и libbase.a собрались. ipc_checks int->
strict size исправлен (first(3u), first(1u)). channel-base-link.log дошёл до
линковки: duplicate GetIdleWakeupsPerSecond; исправлен hash-checked patch
process_metrics.cc (native исключён из upstream NOTIMPLEMENTED fallback).
base-final-link.log повторил линковку и выявил НЕПОДДЕРЖИВАЕМЫЙ ELF TLS у
Chromium base/Abseil. Браузер/base ELF всё ещё НЕТ. Старые сессии закончились.

Решение без фальшивого PT_TLS/Linux TCB: LLVM -femulated-tls + собственный
native runtime (docs/abi/compiler-tls.md написан перед кодом, ABI из pinned
compiler-rt emutls.c). user/libc/src/emutls.cpp хранит aligned/per-thread
instances по control pointer в приватном ThreadBlock, без общего TLS-слота на
переменную; __cxa_thread_atexit[_impl] LIFO, C++ destructors до pthread keys,
новые регистрации из key destructors тоже обрабатываются, storage очищается
после всего. main TLS destructors до atexit/global; _Exit/kill без cleanup.
CMake c INTERFACE и Chromium toolchain.py включают -femulated-tls (rt-only
programs без libc не тронуты). libc++abi generic cxa_thread_atexit.cpp исключён,
необходимо одно владение TLS lifetime. audit по-прежнему запрещает ELF TLS,
его диагностическая строка теперь говорит ELF TLS, не compiler TLS.

Новые compiler_tls_checks.cc + other TU: 8 concurrent threads, distinct
addresses, initial values/zero/align256/cross-unit access, dynamic ctor once,
LIFO 2->late3->1, key destructor creates4, cleanup 32 repeated pthread workers
без live heap leaks; child main TLS teardown before atexit via exit42.
ЕЩЁ НЕ ПРОВЕРЕНО. Native WatchFD/runtime test тоже ещё не был проверен.
build-builtins.py кэширует 3 arithmetic helper объекта по inputs/headers и
compiler metadata, чтобы build-platform больше не компилировал их всегда.

СЕЙЧАС выполняется ОДИН критический пакет, session38679:
emutls-os.log -> emutls-runtime.log -> emutls-runtime-guest.log ->
emutls-base.log -> emutls-base-guest.log. with-cache.sh 1000 1000, root cwd.
Замена toolchain revision из-за -femulated-tls требует один полный Chromium
rebuild, не перезапускать compiler мелкими порциями. Следить за фазой и чинить
всю группу ошибок, QEMU строго последовательно. НЕ править vendor cache
через prepare пока ninja работает. Новые патчи до старта успешно подготовлены
tls-platform-prepare.log. До этого доказаны runtime/channel/spawn/windows,
но НЕ новый compiler-TLS профиль. Никаких заявлений о готовом браузере.

Текущий пакет56545: runtime_cpp guest УСПЕШНО, m8/spawn УСПЕШНО,
m10/windows УСПЕШНО. channel-base.log ещё собирается (1338 шагов после
изменившихся общих заголовков), не запускать второй compiler/QEMU параллельно.
Гость подтвердил heap/FD counters, concurrent startup take, ChannelQuery/
shutdown/writable watches, stream300000 bytes/backpressure/partial reads,
закрытие queued capabilities и blocked readers. Это уже проверено.

ПОСЛЕ подготовки данного guest ELF, ещё БЕЗ сборки, написан следующий мост:
charm_channel_watch_fd(endpoint,mask) -> native poll-only FD с owned narrow
duplicate. Kind::ChannelWatch в libc, read/write/stat ENOTSUP, poll переводит
настоящие channel readable/writable/closed packets. Это нужно для интеграции
оконного event_handle в основной MessagePump/Ozone без отдельного потока.
Новый runtime test/expected "channel readiness descriptors ..." ЕЩЁ НЕ
скомпилирован и не запущен; текущий PASS относится к состоянию ДО этой строки.
Следующую сборку не запускать ради этого одного моста; довести связанный GUI
блок. Текущее base не использует новый API и может собраться с предыдущей libc.

САМЫЙ НОВЫЙ блок: ABI1.18 ChannelQuery81/ChannelShutdown82, Endpoint read/write
shutdown и Writable ObjectWatch (packet11), bound_mask, cleanup queued caps,
wake readers/writers. docs/abi/channel-control.md написан перед кодом.
Kernel startup send/recv тоже соблюдает закрытие и очередь64. Общий SDK
charm/stream.hpp: borrowed bounded stream, частичные reads, native write waits
с отменой подписок, абсолютные deadlines. Chromium sync_socket_charmos и
NativeChannel с передачей реальных capabilities вместо BSD/SCM_RIGHTS/FD.
Старый include UnixDomainSocket выбирает ТИПИЗИРОВАННЫЙ native интерфейс,
поэтому непренесённые FD-потребители не смогут тихо трактовать FD как caps.
PID credentials честно ENOTSUP; отправка копирует actual rights и сохраняет
оригиналы. Есть base ipc_checks с shutdown/full queue/partial reads/child caps.

Все новые GN/vendor патчи ДО malloc_dump_provider.h tracing macro успешно
применены channel-platform-prepare.log. Macro native tracing добавлен позднее
и применится при следующем prepare (без изменения native counter semantics).
Активен ОДИН критический пакет, exec session56545:
with-cache.sh 1000 1000 bash -c 'OS && host && full-runtime && runtime_cpp guest
&& m8 spawn && m10 windows && --base && base_runtime guest'.
Логи build/chromium/channel-{os,host,runtime,runtime-guest,spawn-guest,
windows-guest,base,base-guest}.log. ОС успешно собрана, host31/31,
full-runtime194/194+ELF audit успешно; в момент записи QEMU ещё выполняется.
Не стартовать параллельный QEMU, не править vendor cache во время ninja.
Не перезапускать компилятор по мелочам — дождаться всего пакета и чинить
полные группы найденных ошибок. Полного браузера/окна пока нет.

НОВЕЕ: base-sdk-os.log успешно, base-sdk-integration.log завершён exit1:
1748/1750 шагов, 10 failed units. Native shared_memory/mapper, sys_info,
native_library, process, launch, kill, file allocation и file_util_posix
скомпилировались! Shared-memory switch тоже компилируется. Линковки ещё нет.
Остались unix_domain_socket, sync_socket, process_metrics_posix, module_cache,
stack_base_address/stack_copier/thread_delegate/stack_trace, malloc_dump_provider,
плюс shared_memory_checks: redefinition writable. Последняя ошибка уже
исправлена (новое имя writable_region), без повторной сборки.

После этого накоплен следующий блок БЕЗ компиляции: libc атомарный счётчик
живых allocation capacities и locked FD count; native process_metrics,
module_cache (реальные linker text bounds), stack_trace (только настоящий
один return address; нет ложного полного unwind), malloc_dump_provider
(только live_block_capacity без фальшивых RSS). prepare-platform GN исключает
POSIX-only diagnostic sources; StackSampler upstream и так возвращает nullptr
для non-ChromeOS, так что signal copier не нужен. user.ld экспортирует границы
text. Добавлены file_checks для preallocation/preserved bytes/offsets/accounting.
Новый диагностический блок ещё НЕ применён в vendor cache и НЕ проверен.
После него НЕ запускать новую сборку только ради проверки мелких изменений;
сначала закрывать целую оставшуюся группу IPC (Unix domain/sync socket).

Указание владельца: продолжать браузер до конца; НЕ компилировать мелкие
пакеты, только при критической необходимости. Подагентов не запускать.
Браузерного ELF/окна пока НЕТ; не утверждать готовность Chromium.

Проверка SDK 1.0/ABI 1.16 ЗАВЕРШЕНА успешно: sdk-process-os.log,
sdk-process-runtime.log, sdk-process-guest.log; в QEMU подтверждены права,
ownership VMO, отмена ожиданий Process, приоритеты, лимиты argv/env/extras.
Предупреждения ниже «SDK ещё не прогнан» относятся к более ранней записи.

Следующий большой файловый блок тоже проверен одним критическим пакетом:
fs-directory-os.log, fs-directory-host.log (31/31), fs-directory-runtime.log,
fs-directory-guest.log (browser/runtime_cpp PASS). ABI 1.17: FsUnlinkTyped79,
FsRename80, CrossDevice23. Настоящее ext2 regular rename (в том числе замена),
inode identity, корректный порядок публикации directory records, fault-injection
тесты сохранности данных, отсутствие утечек обычных операций. Directory rename
и FAT rename явно Unsupported. НЕ journal/crash-atomic. libc openat/fstatat/
unlinkat/renameat/fdopendir/dirfd, realpath с проверкой каждого компонента,
limits.h, pathconf и честные неподдерживаемые Unix permission/symlink операции.
Directory-relative '..' запрещён ENOTSUP. QEMU проверил урезанные права,
open destination Busy, сохранность открытого source, владение DIR, realpath.

prepare-platform.py успешно применяет native file allocation и file_util
патчи (fs-platform-prepare.log), но сами Chromium base backends пока НЕ
скомпилированы. Флаги build-platform теперь одинаковые для всех режимов,
чтобы переключение диагностической цели не инвалидировало allocator buildflags.

ПОСЛЕ этой проверки, ещё БЕЗ компиляции, написан SDK 1.1 startup capabilities:
charm_take_start_handle(role>=0x10000), mutex/одноразовое владение, сохранение
ролей и стабильные extra slots. Chromium shared_memory_switch сериализует
полученную роль, а не parent handle/FD; LaunchOptions хранит shared_ptr owners
и передаёт narrowed rights. Патчи hash-checked применились успешно в
startup-platform-prepare.log. Добавлены guest concurrent-take child и base
child tests readonly/unsafe mappings/owner copies/repeated take. ЕЩЁ НЕ ПРОГНАНЫ.

Добавлен harness base_runtime (kernel test, QEMU fixture/args/expected) и
копирование/audit ELF после успешной --base. До успешной линковки/гостевого
запуска это только подготовленный harness, не доказательство готовности.
Последний --base по-прежнему base-native-clock.log: старые ошибки Unix domain,
sync_socket, optional profiling, process_metrics; новых native memory/process/
file backend ещё не проверяли компилятором. Следующий --base должен быть одним
проходом всего накопленного блока, затем править ошибки большими группами.

# Последние указания и пакет SDK (2026-10-28, предыдущая запись)

Владелец просит довести браузер ПОЛНОСТЬЮ до рабочего состояния в CharmOS,
а общий API сделать пригодным для другого браузерного движка. Отдельно дважды
потребовал НЕ запускать компилятор после мелких пакетов: пропускать обычные
промежуточные сборки, запускать только при критической необходимости после
большого связного блока. Последние ~2000 строк SDK/процессных адаптеров ниже
ещё НЕ проверены компилятором; не утверждать, что браузер готов. Окна нет.

До этого исправлен fnmatch, 31/31 host зелёные (vector-path-host.log), большой
дифференциальный тест path-differential.log. Есть исключение для особенности
glibc со '*\\/' и FNM_PATHNAME: native случай проверен отдельно. browser/runtime_cpp
в vector-path-runtime-guest.log прошёл с readv/writev (включая атомарность pipe
record из нескольких iovec), basename/dirname/fnmatch и старым проверенным
пакетом stat/FsIdentity/NOFILE. Не путать с новыми SDK/ProcessView тестами!

Новый связный пакет, без повторных сборок по просьбе владельца:
- user/rt/include/charm/{handle,memory,process}.hpp: UniqueHandle, Mapping,
  SharedMemory, ProcessView. README с примерами и явным владением/правами.
- docs/abi/application-sdk.md: ABI 1.16, VmoSize=76, ThreadPriority=77,
  HandleRights=78, RuntimeQuery(8)=Job ceiling, отмена Process/Thread/Job binds
  через существующий ObjectUnbind. Документация написана перед реализацией.
- Kernel/read-only metadata и current-thread priority реализованы, но последнее
  sdk-os.log доказывает только более ранний промежуточный пакет без HandleRights
  и без новых отмен Process/Thread/Job! Пересобрать и проверить перед заявлением.
- ВАЖНО: приоритеты в ядре УЖЕ существовали (ThreadSetPriority=58, 1..31),
  исходная новая native thread-заготовка ошибочно считала их отсутствующими.
  Исправлено: background/utility/default/presentation/audio/realtime =
  4/8/16/20/24/28 с реальным Job ceiling; главный поток теперь тоже поддержан.
- Perfetto PagedMemory использует общий SDK. Новые SDK-тесты в runtime_checks
  проверяют ownership/real sizes/readonly rights/moves/process deadline cleanup,
  priority inheritance и ошибки лимитов запуска. ЕЩЁ НЕ ПРОГНАНЫ.
- Chromium shared_memory_{mapper,region} native: реальные VMO, точные права и
  размер при TakeOrFail, readonly conversion, registry полных mappings для
  offset views. Новые base-тесты shared_memory_checks.cc ЕЩЁ НЕ ПРОГНАНЫ.
- process_charmos/kill_charmos/launch_charmos: native capabilities вместо PID,
  int64 ProcessHandle (0 invalid, -1 current pseudo, >0 uint32 kernel handle),
  явные native LaunchOptions, duplicate before transfer, deadline waits. Никаких
  fork/exec/UID/waitpid. Не экспортировать неподдерживаемый CloseSuperfluousFds.
- sys_info_charmos/native_library_charmos: реальные CPU/RAM/ABI identity;
  disk/residency unknown возвращает nullopt, dynamic loading — ENOTSUP.
  Необязательный Purge native возвращает false до изменения shared state.
- libc charm_spawn исправлен от молчаливого усечения argv/env/extras и underflow
  own-handle count при overflow; ошибки копирования authority теперь возвращаются.
  charm_wait использует ProcessView. Эти изменения ЕЩЁ НЕ ПРОГНАНЫ.
- Window добавлены width/height/event_handle (borrowed readiness handle);
  close обнуляет размеры. Сетевой/оконный ABI не менялся.

Следующий критический check после этого большого блока: OS + full-runtime и
browser/runtime_cpp для нового ABI/SDK/спавна. Затем один --base проход всего
накопленного пакета; он по-прежнему имеет ещё не перенесённые file_util,
Unix-domain/sync-socket, профилировщик/stack trace и process_metrics зависимости.
Последний реально завершённый --base = base-native-clock.log, до SDK backend:
clock/rand/thread + native IO pump успешно компилировались, но линковки нет.
Новые base backend и тесты пока только написаны. Нельзя отключать core функции
или добавлять фиктивно успешные заглушки ради линковки. Полный браузер всё ещё
требует Ozone/Skia, renderer/IPC/network интеграции и последующих компонентов.

# Актуальное состояние браузера 2026-10-28

Запрос владельца: довести нативный Chromium до появления браузера в CharmOS.
Полного браузерного ELF и окна пока нет; M10/порт Chromium не закрыты.
Продолжать связными пакетами. Ниже старые записи сохраняются как история,
их перечень отсутствующих интерфейсов уже частично устарел.

За последний пакет реализованы и проверены в QEMU: настоящие channel-backed
pipe/pipe2 с backpressure/ACK и разделяемыми описаниями, poll через отменяемые
kernel port watches, частные anonymous/file mmap с реальными read-only fault
проверками, mkstemp/mkostemp/mkdtemp, UTC strptime/timegm, uname/sysconf,
Perfetto guarded PagedMemory, память/число CPU через RuntimeQuery. ABI 1.14:
ObjectUnbind=73, ObjectWatch=74; RuntimeQuery 5/6 managed total/free frames,
7 actual CPU count (селектор 4 остаётся периодом учёта планировщика!).
Потоки libc v3: pthread attributes/custom stacks, join/detach, reaper по
настоящему ThreadExited, monotonic condition attributes, pthread_equal.
Detached stacks освобождаются после выхода kernel thread, не заранее.

Доказательства: build/chromium/native-pthread-guest.log (browser/runtime_cpp,
включая 128 detach и ограничение утечки физических страниц, custom 512KiB
stack с 320KiB touch, monotonic cond); native-mapping-guest.log;
native-watch-https.log (TLS 1.2/1.3, HTTP, отрицательные сертификаты и poll);
native-watch-{threads,signals,spawn,libc,preempt,windows}.log; 30/30 host в
native-query-host.log. Регрессии m8/m10 были ДО pthread v3/FsIdentity, повторить.

Последний собранный пакет ОС: native-fs-identity-os.log, успешно. ABI 1.15:
FsIdentity=75 возвращает реальный mount/node ID. libc stat v3=56 байт,
dev/ino реальны, atime/ctime=0 unavailable; lstat эквивалентен stat (symlinks
отсутствуют), futimes честно ENOTSUP. Реальные get/setrlimit NOFILE с soft/hard,
ограничения fd_alloc/dup/dup2; STACK=64KiB bootstrap, CORE=0. Новые guest-тесты
идентичности и NOFILE добавлены в runtime_checks.cc, ЕЩЁ НЕ ПРОГНАНЫ.

Новые Chromium native sources message_pump_charmos.{h,cc}, files_comparison.cc,
memory_metrics.cc интегрированы exact/hash-checked prepare-platform.py.
Нативный IO/UI pump использует pipe/poll, shared watch state для уничтожения
controller в callback, nested Run и delayed work. Пока не проверен в госте.
Последний запуск --base: build/chromium/base-native-io.log (в работе при этой
записи); предыдущий base-native-mapping.log не дошёл до линковки. Дальше:
разобрать новый build log, прогнать full runtime/guest для FsIdentity/rlimit,
реализовать настоящие платформенные backend //base, затем оставшиеся browser
IPC/process/Ozone/Skia/network компоненты. Не подменять Linux и не глушить
недостающие операции успешными заглушками. Только C++, без Rust.

WSL cache доступен через tools/chromium/with-cache.sh 1000 1000 bash -c '...'
(запуск wsl -u root): монтирование живёт в namespace, Windows напрямую source
не видит. В WSL нет rg, использовать grep/sed. Не запускать prepare-platform
одновременно с ninja, не запускать несколько QEMU одновременно.

# Актуальное уточнение 2026-10-17

## Уточнение владельца 2026-10-17: только C++

Новый код CharmOS и адаптация браузера — только C++. Эксперимент с native
Rust отменён: собственные runtime.rs, сборщик и проба удалены. Не включать
Rust ради обязательных зависимостей Chromium: JSONReader заменить настоящим
C++ парсером с контрактом upstream (Unicode, глубина, расширения и ошибки),
Rust logging bridge исключить вместе с Rust-кодом, сохранив C++ logging.
Для следующих компонентов выбирать C++ backend и проверять фактический
граф зависимостей. Нельзя заменять JSON или другие функции заглушками.
Полный C++ runtime уже прошёл browser/runtime_cpp в QEMU, включая locale,
math/fenv, потоки и UTF-8; полный браузер пока не собран, окна браузера нет.
Текущий незакоммиченный пакет содержит эти runtime-изменения и работу над base.

# Передача работы (2026-10-17): состояние CharmOS для следующего агента

## Текущий пакет: Chromium GN bootstrap (2026-10-17)

Завершён связный пакет tools/chromium/platform, prepare-platform.py, toolchain.py,
build-platform.sh, audit-elf.py, лицензии и дополнения libc v1.1:
aligned_alloc/posix_memalign, free/realloc для aligned allocations,
strerror_r (XSI), сообщения для всех errno и ELAST. Контракты описаны.
В runtime_threads добавлены настоящие guest проверки границ/переполнений,
сохранности realloc, ошибки буфера, errno; они уже прошли.

GN использует настоящий upstream .gn/BUILDCONFIG, target_os=charmos и свой
Generic ELF wrapper. Пять SHA-256 checked patches; оригиналы в ext4 cache
platform-originals. Новые точные лицензии в tools/chromium/licenses проверяются
по manifest, .gitattributes сохраняет байты. Compiler stamp в архиве пустой;
реальный bundled clang24 регистрируется custom через clang_base_path.
Не подделывать expected official stamp. IS_CHARMOS/IS_POSIX true, Linux false.

platform_smoke с двумя upstream units (algorithm/verbose_abort) уже собран
через GN и прошёл в QEMU: vector 4096, sort/copy, unique_ptr, atomics, UTF-8
bytes. НЕ полный libc++ и НЕ браузер. --full-runtime сохраняет настоящую
попытку сборки обычных upstream runtime deps. Все no-exception ABI units
компилируются; полный runtime пока падает на locale.h, fenv/math и числовых
преобразованиях. build/chromium/platform-build.log (ожидаемый exit 1).
Исключены exception storage/personality и дубли своих guard/virtual handlers;
ABI internals сохраняют RTTI. Не маскировать недостающие функции заглушками.
Audit отвергает ET_DYN, dynamic/interpreter/compiler TLS segments/sections.

Пакет проверен, активных прогонов нет. build/platform-os-build.log — общая
сборка; platform-tls-build.log — повторная native TLS link с новой libc.
build/platform-host.log: 29/29 host. build/platform-regression.log: exit 0,
14/14 QEMU — runtime (4), browser (3), m8/libc, calc_profiles, control,
editor, files_nav, universal, windows. Лишь после завершения QEMU проверены
финальные GN tooling изменения: build/chromium/platform-final-build.log,
platform-audit.log и platform-incremental.log. No-op ninja подтверждён;
platform-header-deps.log / platform-link-deps.log подтверждают свои заголовки
и libc.a среди зависимостей. platform-final-guest.log: повторный platform
на финальном ELF зелёный, exit 0. build/platform-final-audit.sh завершился 0.
Два промежуточных запуска audit fixture исправлены: родной linker script
сам отвергает compiler TLS, поэтому отрицательный образ создаётся штатным
linker script; shared fixture требует driver -shared, чтобы убрать default PIE.
Проверяются настоящие native/shared/TLS ELF, не придуманные заглушки.
Новый GN toolchain revision входит в имена wrappers, libc.a в явные link deps,
system headers попадают в depfiles. Одинаковые подготовленные файлы не трогаются.
Финальная проверка завершена. Следующий шаг — реальные locale/
math/fenv/stdio runtime функции и затем //base. M10 остаётся открытым.
Свежие снимки опубликованы: build/screenshots/platform-calc-engineering.png,
platform-editor.png; дополнительно сохранён platform-control.png.
Итоговый native GN ELF — 62368 байт; receipt platform-report.json.

## Пакет C++ / Unicode / native HTTPS (2026-10-17)

Unicode/wchar, Gregorian UTC, gettimeofday, pthread v2, C++ new/delete,
init/fini arrays, guard и LIFO destructors реализованы. docs/abi/cxx-runtime.md,
libc-runtime.md, libc-threads.md. Все scalar Unicode перебираются в госте.
Реальные BoringSSL crypto/ssl полностью собраны в ext4-кеше; runtime_tls.elf
связан без host libc/libstdc++/libgcc. Используются две upstream libc++ units
и три compiler-rt units из того же архива. tools/chromium/build-tls*.sh.
Гостевой browser/tls прошёл: TLS1.2/1.3 HTTP через memory BIO, SHA-256,
аппаратная энтропия, отказы неверному имени, issuer, expired/future.
Runtime C++ ctor/guard/dtor/fini и UTF-8 также прошли. Логи cxx-final-*.
После перезапуска ПК cache image смонтирован заново, исходники и код целы.
HTTPS через настоящий guest TCP/virtio-net также прошёл: runtime_https.cpp,
kernel test с optional Net cap, host fixture. Два HTTP-ответа (TLS1.3/1.2),
hostname/trust/expired/future отклоняются. Хост независимо проверил SNI,
версии протокола, два запроса и четыре отказа. Публичные сайты не проверены.
Fini reentrancy guard и C++ constructors/guard/aligned array new/delete,
destructors/atexit/fini, Unicode также прошли на финальной сборке.

Финальная сборка: build/https-final-build.log. 29/29 host зелёные:
build/https-final-host.log. После перезапуска все 15 QEMU сценариев зелёные,
код 0: build/https-final-regression.log — browser (2), runtime (4), m8/libc,
universal, usb_combo, usb_hid, usb_store1/2/3, windows, xhci.
Первые 12 из 20 M10 до перезапуска прошли в build/cxx-final-regression.log;
оставшиеся восемь завершены после перезапуска выше. До добавления C++ startup
полный проход M10 сохранён также в build/unicode-regression.log.
Не собирать параллельно с QEMU; активных прогонов больше нет.

Debug ELF остаются в ext4-кеше, в initrd — копии без debug sections:
runtime_tls.elf 11490472 → 2247224 байт; HTTPS ELF 2252400 байт.
Общий test.sh не редактировался во время исполнения. Работать крупными
пакетами, не отчитываться о каждом подтесте; минимум текста, снимки сразу.
PNG unicode-calc-engineering/control/editor опубликованы в чат.
Следующее: Chromium GN platform/toolchain, полный libc++/base, публичные
trust roots/DNS, процессы/IPC и software Ozone для настоящего окна браузера.
M10 открыт; Chromium окна пока нет. Редкий TCP дефект не объявлен исправленным.
Ниже — предыдущий завершённый пакет.

## Дополнение 2026-10-17: пакет рабочего стола

Обновление: официальный архив получен и полностью импортирован: 1342990
записей, XZ integrity passed, chrome/VERSION 155.0.8059.84. Итоговый отчёт
build/chromium/archive-import.json. Исходники внутри ext4-кеша на A:;
доступ через tools/chromium/with-cache.sh (см. plans/CHROMIUM-PORT.md).
На C: мало места: туда исходники и сборку не размещать. Git sync не нужен.
Реализованы boot ABI 1.1 / syscall ABI 1.12: UTC из UEFI и аппаратная
энтропия RDRAND/TPM; docs/abi/time-random.md. Добавлены libc rwlock/TSD с
поколениями ключей, очисткой при return/pthread_exit, div/ldiv/lldiv и assert.
TPM runtime mutex общий с sysarea unlock: драйверы уже работают при mount.
Новый page cache объединяет чтение/writeback до 6 страниц старого block ABI.
Первичное шифрование 15 МиБ: 156099 → 22066 мс в TCG (около 7,1×).
Проверен 200-КиБ файл с записью через границы страниц после перезагрузки,
SATA/virtio/NVMe/USB, fsck и аварийный перезапуск NVMe.

Финальная сборка прошла: build/browser-cache-build.log. 29/29 host зелёные
(build/browser-cache-host.log). Финальные 39 QEMU сценариев зелёные, код 0:
build/browser-cache-regression.log — runtime (4), m6 (10), m8.5/store,
m8/libc, m9/sysarea1+sysarea2_reboot+sysarea3_notpm, весь m10 (20).
tools/test.sh теперь принимает [phase] [timeout] [scenario] для адресного
повтора. Не редактировать исполняемый test.sh во время прогона: shell может
прочитать новые байты со старого смещения (один промежуточный прогон так
оборвался; финальный полностью повторён). Не собирать параллельно с QEMU.
Ранние прогоны M0–M9 сохранены в build/browser-runtime-*.log; первый TCP
и store упирались в 60 с. Бюджеты tcp/sock теперь 180 с, store 300 с;
внутренние проверки байтов/подписи/сроков не ослаблены. Весь m7 повторён
успешно за 144 с. Редкий дефект TCP НЕ объявлен исправленным, TCP-код не менялся.
Снимки свежего финального прогона опубликованы сразу: build/screenshots/
runtime-calc-engineering.png, runtime-control.png, runtime-editor-markdown.png.
Windows input→pixel 35426/32251 мкс, compose около 4,2–6,0 мс; это не
утверждение об устранении всех задержек виртуалки. Реальное железо не проверялось.

Chromium ещё не собран, реального окна браузера пока нет.
Реальный getentropy.cc BoringSSL прошёл freestanding syntax probe с bundled
libc++, без Linux-заголовков. thread_pthread.cc пока заблокирован wchar/
mbstate_t и неполным stdio. Логи build/chromium/tls-probe. Не маскировать
это пустыми объявлениями: следующий пакет — Unicode/C++ runtime, затем
полная сборка/линковка crypto+ssl и HTTPS с проверкой сертификатов.
Порядок владельца остаётся Chromium → ИИ → магазин. M10 НЕ закрывать.
Ниже сохранена предыстория; при расхождениях это обновление актуальнее.

6107d39 и f3df793 отправлены в origin/main по явному разрешению владельца.
Для Chromium закреплены официальный Stable 155.0.8059.84 и depot_tools.
Git fetch срывался, поэтому владелец передал официальный исходный архив
4390800948 байт; импорт завершён (см. обновление выше). Git sync остановлен,
не перезапускать. Прежний лог build/chromium/fetch.log. Весь сторонний код
и кеш остаются в игнорируемом build/chromium с исходными лицензиями.
tools/chromium и plans/CHROMIUM-PORT.md — инфраструктура порта, не браузер.
Исходный baseline имел time()=uptime и недоступный пользователю TPM random;
новый пакет boot 1.1 / syscall 1.12 исправляет это и проверен выше.
Следующие реальные зависимости — libc++ и TLS, затем Chromium base/Ozone.

Первый пакет ниже сохранён коммитом 6107d39. Следом реализованы шесть
профилей калькулятора: инженерный (включая ln/log/exp/tan и 1e-6), графики,
64-bit программист, молярная масса 24 элементов CIAAW, редактируемые формулы
a/x с параметрами, две резисторные топологии и груз на пружине.
Это ограниченные первые модели, не свободный конструктор цепей/механики.
Заключительные проверки этого пакета: 28 host, 9 случаев ASan/UBSan,
QEMU calc_profiles и universal зелёные (build/calc-profiles-qemu.log).
Снимки build/screenshots/calc-*.png отправлены сразу в чат.

Последний порядок владельца: Chromium → ИИ → магазин. HTTPS входит в
необходимые зависимости браузера. Владелец просит минимум текста и проверки
крупными пакетами. Следующая работа — нативный порт Chromium; браузера пока
нет, не показывать хостовый Chromium как приложение CharmOS.

Главное расширение требований — `plans/DESKTOP-APPS.md`. M10 НЕ закрыт.
Последнее пожелание владельца: ИИ в универсальном доступе, собственные ключи
OpenAI/Anthropic/Яндекс, действия в системе. Требования, порядок зависимостей
HTTPS/ключей/диспетчера и официальные API-источники записаны там. Подключение
к провайдерам ещё не реализовано; не выдавать план за работающий ИИ.

В рабочем дереве: quick panel справа снизу, выражения, единицы, SHA-512,
русский ввод и адреса Files, Диск A, калькулятор/график, настройки в двух
слотах с fsync, ограниченные damage regions и -O2 для графических приложений,
новый UTF-8 редактор с автосохранением, подсветкой и простым Markdown.
Редактор: 16 КиБ, один уровень undo/redo; LaTeX только подсветка, не typeset.
Chromium, медиапроигрыватели, графический магазин и прочие профили калькулятора
ещё впереди. Нельзя объявлять все требования выполненными.

28 хостовых тестов прошли; 5 desktop-тестов прошли GCC ASan/UBSan.
Редактор в QEMU проверен: русский файл сохранён, undo/redo меняют реальные
байты 20→21→20→21, Files F6 повторно открывает файл, cat читает «Привет!».
Повтор адресных 10 сценариев завершён успешно: `build/test-desktop-final.sh`, лог
`build/desktop-apps-qemu-final.log`. Проверить окончательный результат перед
коммитом. Windows: input→pixel 36456/34057 мкс; compose в этом прогоне около
4,4–6,4 мс. Предыдущий прогон: первые 8 сценариев прошли; windows не стартовал
из-за ещё не собранного edit.elf. После сборки прогон перезапущен полностью.
Не собирать и не запускать вторую QEMU, пока этот прогон активен.
Снимки показывать сразу в чате; новые — `build/screenshots/editor-*.png`,
`universal-*.png`. Ключи API не просить в чате, не писать в логи/снимки.

Читать сначала `CLAUDE.md` (правила проекта: спека перед кодом, ABI — в
`docs/abi` и согласуется с автором, «готово = tools/test.sh зелёный», весь код
свой, архитектурные решения — у автора и в журнале `plans/os-gui-design.md`,
при неуверенности — спросить). Главный план — `plans/ROADMAP.md`.
Общение с автором — по-русски.

## Где мы

- M0–M8.5 закрыты. **M9a закрыт** (всё, что проверяется в QEMU/VMware):
  AML-интерпретатор, `_PRT`/INTx, ACPI S5/перезагрузка (`system_power`, хендл
  Power у init), e1000 и RTL8139 на прерываниях, закрытая область
  AES-256-XTS + TPM 2.0 (PCR 0–7). **M9b** (реальный ПК, RTL8169) — когда у автора
  появится ПК. Подробности — ROADMAP, раздел M9.
- **M10 в работе** (экран, ввод, компоновщик). Закоммичено:
  - `f4c259f` — ABI v1.8 (display.md, input.md), PAT write-combining, wm с
    damage tracking, тест m10/screen;
  - `f1097e7` — PS/2 клавиатура+мышь в ядре (`kernel/input`, `lib/input`
    набор 2 → HID Usage), кольцо ввода, исправление гонки убийства процесса
    (`kernel/proc/signal.cpp`, `take()`).
- **M10, часть 3 — протокол окон:** `docs/abi/wm.md` v2
  (зафиксирован автором), ABI v1.9 (только роль `Wm` = 11), `include/abi/wm.hpp`,
  libc `charm_wm()`/`CHARM_GIVE_WM`, дверь в `user/init/init.cpp`, новый
  `user/bin/wm/wm.cpp` (окна, перетаскивание за заголовок, фокус, ≤60 Гц,
  FRAME_DONE), клиент `user/bin/wdemo`, тест `tests/qemu/m10/windows`
  (+ `tools/qemu/windows.{args,sh}`), обновлены m10/screen и m10/input.
  Тест windows проходит (задержка «ввод → пиксель» ~26–33 мс в QEMU без KVM).
  Первая полная регрессия завершилась провалом проверки пикселя windows:
  точка попала в движущийся белый квадрат. Точки перенесены за пределы
  анимации. Повторная полная регрессия зелёная: 62 сценария m0–m10,
  21 хостовый тест. После пересборки с v2 все три сценария M10 тоже зелёные.
  Часть 3 готова к коммиту «M10 (part 3): …» и push; текущее состояние
  коммита и ветки проверять через git log/status.
  **Уточнение автора при передаче Codex:** FRAME_DONE возвращает предыдущий
  освобождённый буфер; показанный остаётся у компоновщика. Для первого кадра
  возвращается второй буфер. Это протокол v2; syscall ABI остаётся 1.9.

## Следующее по M10 (решения автора уже есть)

Часть 3 отправлена в origin/main (`c1b2ec4`). Часть 4 реализована, все
4 сценария M10 зелёные, 23 хостовых теста тоже. Полная регрессия
63 сценариев зелёная (`build/regress-font-ui.log`). Часть 4 отправлена в
origin/main (`73c0242`; затем f1c6cb7 сохраняет LF defaults и байты OFL).
Часть 5: начальный xHCI (handoff/reset, DMA rings, MSI-X, No Op) проходит
`m10/xhci` (отдельный прогон tmp-xhci зелёный), 24 хостовых теста зелёные.
Часть 5 отправлена в origin/main (`4cd2ba4`). Часть 6: перечисление USB и
HID keyboard/mouse работает, `m10/usb_hid` прошёл отдельно, ABI 1.10
(`docs/abi/input-driver.md`, device_input=68). M5 и все 6 сценариев M10
зелёные, включая запреты инъекции ввода; далее Mass Storage и HDA. Пока только корневые порты и подключение
при старте, без внешних хабов и обработки переподключения.
Часть 6 отправлена (`93f7ebf`). Часть 7 (USB BOT/SCSI) реализована:
usb_store1/2/3 и usb_combo зелёные; GUI загружен с USB-корня и клиент
совместил 2 МиБ write/sync/readback с HID. Полная регрессия: все 69 сценариев
и 24 хостовых теста зелёные (`build/regress-usb-storage.log`).
Справочник SCSI скачан с Seagate, источник/хеш — docs/specs/scsi-source.md.
Часть 7 отправлена (`e85909e`). Часть 8 — HDA: код реализован, ABI 1.11
описан в docs/abi/audio.md. Изолированный сценарий hda и 26 хостовых тестов
зелёные; WAV-захват QEMU подтвердил 1325 стереокадров 48 кГц s16,
2002 ненулевых сэмпла, диапазон -2048..1984. Полная регрессия зелёная:
70 сценариев и 26 хостовых тестов (`build/regress-hda-v2.log`). Первый полный
прогон выявил переполнение восьми ролей при System+Audio: добавлен str2
(16 ролей) с сохранением strt/flags=0, тест v1/v2 с 16 хендлами и отказом
legacy без потери хендлов. Изолированные boot/spawn/store/hda зелёные.
В boot.expected убрана зависимость порядка ответа «ok» и отчёта об убийстве
httpd. В hda тест добавлена проверка ненулевого WAV после выхода QEMU.
После ограничения индекса input amp четырьмя битами HDA и все 26 хостовых
тестов повторно зелёные (`build/test-tmp-hda-hda.serial`).
M10 на реальном железе не проверен. Владелец выбрал стандартную MIT:
корневой LICENSE, Copyright (c) 2026 DarovskoyID. Прежний проект запрета
изменений магазина отменён; магазин тоже можно менять и заменять.
Политика — plans/LICENSING.md. Сторонние материалы сохраняют свои лицензии;
будущие GPL-драйверы не перелицензировать в MIT.
Автор поручил продолжать дорожную карту без запросов на каждый этап
(записано в CLAUDE.md). M10 целиком пока не закрыт.

Продолжение после интерактивной проверки владельцем: Files получил состояния
наведения, число совпадений и снятие фокуса поиска при клике вне поля.
Предпросмотр первых 4 КиБ теперь прокручивается (колесо/стрелки/PgUp/PgDn,
Home/End), переносы по UTF-8. Все 13 прежних сценариев M10 повторно зелёные;
новый files_preview прошёл адресно после исправления exitcode S5, включая
проверку фокуса (`build/ui-preview-final.log`). Хостовые тесты: 27/27.
Полная регрессия m0–m10 при этой доработке не запускалась.

Следом доработан лаунчер: единый список для выбора стрелками/Tab, Enter,
наведения и клика; рамка выбора, поиск заголовков открытых окон и сетка,
учитывающая размер экрана. Оконный ABI прежний. Расширенный launcher-тест
проверяет Files/Colors и возврат фокуса в amber; адресный прогон
launcher/control/input/windows — `build/ui-launcher-final.log`, все 4 зелёные.

Следующая доработка M10: сворачивание кнопкой «−» справа в заголовке и
Alt+F9; восстановление из лаунчера (пометка «Свёрнуто») и через Alt+Tab.
После сворачивания/закрытия фокус получает верхнее видимое окно; когда
все скрыты, ввода клиентам нет. Размеры, координаты, буферы и ABI v2
сохранены. Pending-кадры принимаются до compose для всех окон, включая
скрытые/вне экрана, FRAME_DONE возвращает прежний буфер после compose.
Новый minimize проверяет клавиши/мышь, отсутствие скрытых окон на снимке,
обмен кадрами обоих скрытых клиентов и возврат фокуса после закрытия Colors.
В профиле windows amber/ember — службы с перезапуском, закрывать их для
проверки фокуса нельзя: новое окно может перехватить фокус. Обычная Colors
из лаунчера в этом сценарии не перезапускается.
После сворачивания весь M10 повторно зелёный: 15/15, хостовые 27/27.
Лог — `build/ui-minimize-m10.log`, снимок восстановленного окна проверен.
Полная m0–m10 при этой доработке не запускалась.

Следом — состояния наведения в доке, верхней панели, кнопках окна и
переключателях Control; Alt+Space в панели стал рабочей кнопкой. Подсветка
учитывает перекрытие окнами и открытые панели; damage обновляет прежнюю и
новую области, не весь экран на каждом движении мыши. Новый hover использует
чистый desktop-профиль и сохраняет реальные снимки рабочего стола, лаунчера,
Files, Control и подсвеченных кнопок. PNG для владельца — build/screenshots,
суффикс -20261016.png; первичные PPM — build/ui-*.ppm. Лог адресного прогона
control/hover/launcher/minimize/windows — build/ui-hover-final.log.
Все 5 адресных сценариев зелёные; весь M10 в этом проходе не повторялся.

Пакет 2026-10-17 после просьбы не останавливаться на каждом шаге:
- Логотип исправлен по SVG Main.dc.html: центр окружности, угловая отсечка
  и круглые концы; hover.check.py ловит прежний выступ и проверяет цвета концов.
- Различимые значки папки/приложений/настроек; Files больше не использует Colors.
- Лаунчер: Shift+Tab назад, цифры/пробелы в поиске; Alt+F4 закрывает панель,
  сохраняя окно под ней. Расширенный launcher зелёный.
- Files: Home/End/PgUp/PgDn в списке, F5, Escape сбрасывает поиск; история
  до 16 каталогов с отдельными стрелками назад/вперёд и кнопкой «Вверх».
  Новый переход после возврата назад отбрасывает ветку вперёд.
- Общий M10 после изменения wm: 16/16 (`build/ui-logo-m10.log`). После
  расширения Files — 4/4 адресных files/files_nav/files_preview/hover
  (`build/ui-files-nav-final.log`). Один прежний hover завершился таймаутом,
  изолированный повтор и следующий прогон зелёные; причина таймаута не доказана.
  Теперь в M10 17 сценариев; весь набор из 17 одним прогоном пока не запускался.
  Полная регрессия m0–m10 в этом пакете не запускалась.
- Реальные актуальные PNG — build/screenshots/*-logo-fixed.png;
  files-history-20261017.png показывает историю в /etc. CLAUDE.md дополнен
  указанием продолжать пакетами и показывать реальные снимки.

После замечания владельца интерфейс продолжен в M10: фон/логотип,
типографика, док, скруглённые окна с заголовком 52 px, сетка лаунчера,
нативный files и центр управления (тени/фон). Полного соответствия макету
нет: актуальный список — plans/UI-PROGRESS.md. Files пока только просмотр
каталогов и до 4 КиБ текста, не редактор и не полный файловый менеджер.
Графического терминала пока нет. Проверки и текущие результаты UI —
build/ui-m10-final.log, build/ui-interactions.log, build/ui-files-final.log;
27 хостовых тестов и Canvas+TrueType под GCC ASan/UBSan зелёные.
Все 13 сценариев M10 зелёные с адресными повторами control/files после
доработки ввода; последняя общая регрессия m0–m10 до UI — 70/70 (часть 8).
Большие HMP mouse_move в тестах разбивать на небольшие шаги: PS/2 может
доставить клик до завершения серии пакетов движения (обнаружено x=426
вместо ожидаемого 340 при одном сдвиге -848).

1. **Шрифты готовы в базовом виде:** свой `lib/font`, статические TrueType,
   Unicode cmap 4/12, составные glyf и сглаживание 4×4. Все глифы трёх файлов
   проверены; font прошёл GCC ASan/UBSan. Источники OpenType —
   `docs/specs/opentype`; файлы Space Grotesk и JetBrains Mono с OFL и
   закреплёнными URL/хешами — `assets/fonts` (LAYOUT.md). Без hinting VM,
   shaping и переменных осей; кириллица — резервный JetBrains Mono.
2. UI по макету: панель, лаунчер Alt+Space, Enter запускает Colors,
   Escape закрывает лаунчер, Alt+Tab переключает окна, Alt+F4/рамка закрывают.
   Лаунчер показывает доступную Colors и открытые окна, вымышленных приложений нет.
3. Клиентская библиотека — `user/libwm`, `wdemo` переведён на неё.
4. **USB и HDA реализованы в QEMU:** xHCI → HID → Mass Storage; Intel HDA. Спецификация Intel
   xHCI 1.2 скачана в `docs/specs/xhci-1.2.pdf` (источник — xhci-source.md).
   `USB PD3 CTS…` и `docs/specs/ECNs` — тесты USB Power Delivery;
   удалять — только автору. HDA 1.0a есть.

Исправлены две обнаруженные гонки: потерянный INPUT (SC handshake в
`lib/input/ring.hpp`) и повреждение ready-очереди при прерывании ожидания
(новый `Thread::wait_next`; WaitList больше не трогает ready/zombie next).
GDB в зависшем QEMU показал ready=2 при пустой очереди. После исправления
выключение и оконные тесты проходят; добавлены прямой тест WaitList и
конкурентный тест 100000 событий. Старые неудачные прогоны — build/*.fail.

## Открытые проблемы

- **Редкий провал TCP с потерями** (m7/tcp_e1000, m9/tcp_rtl8139, ~1 из
  20–30). Улика: после нарочно потерянного сегмента приём встаёт целиком (ни
  повторов, ни ARP за 60 с) — ниже TCP. При провале `tcp_dump` печатает журнал
  входящих сегментов, а `net::dump()` — состояние колец ядро↔драйвер (добавлено,
  ещё не ловилось). Ловить циклом одного сценария (`tests/qemu/tmp*`), лог
  провала сохраняется как `build/test-*.serial.fail`.
- Риск PCR 4: обновление загрузчика запирает закрытую область (журнал).

## Окружение и приёмы

- Всё в WSL Ubuntu: `wsl -d Ubuntu --cd 'A:\CharmOS' -- bash …`. Длинные
  команды — скриптом-файлом (кавычки PowerShell → wsl ломаются).
- `/tmp` в WSL периодически очищается: venv с pypdf — `~/pv`; тексты спек —
  `~/spectxt`. Сканы PDF смотреть через `pdftoppm` (установлен).
- swtpm (сокет только в `/tmp`, не на /mnt), монитор QEMU `/tmp/charmos-mon`;
  в `.keys`: `#wait`, `#sleep`, `#pixel X Y`, `#monitor <команда>`;
  `<сценарий>.timeout`, `.exitcode`.
- Регрессия: `tools/regress.sh`, прогресс — `tools/regress-status.sh`.
  Во время регрессии ничего не собирать и не запускать QEMU.
- Поиграться автору: `wsl -d Ubuntu --cd /mnt/a/CharmOS -- tools/run.sh
  --config desktop --cmdline "init=bin/init root=disk0p2"` (окно через WSLg,
  Ctrl+Alt+G — отпустить мышь).
- Запреты автора: без force-push и amend. Скачивать спеки и пушить — можно.
  Продолжать по дорожной карте самостоятельно (CLAUDE.md, «Автономность»);
  ABI сначала описывать, затем реализовывать новой версией. Лицензию выбирает автор.
- Коммиты завершать строкой `Co-Authored-By: …` по правилам вашего окружения.

## Проверенный пакет 2026-10-18 (C++-only)
Свой Rust runtime/сборщик/проба удалены по указанию владельца. Написан
C++ JSON parser + adapter upstream JSONReader; общий корпус тестов проходит
на хосте и в настоящем native ELF в QEMU. cpp-only-runtime-guest.log и
cpp-only-json-guest.log: оба exit 0. Новые UTC localtime/mktime/difftime,
pthread_sigmask/sigfillset проверены в госте, tm layout остаётся 36 байт.
Полная ОС пересобрана, активных QEMU/сборок после validate-cpp-only.sh нет.
Новый kernel test runtime_json; новый режим build-platform.sh --json-runtime.

GN --base-graph наконец прошёл без Rust. audit-cpp-graph.py подтвердил
909 зависимостей/2996 команд без Rust. Профиль отключает optional Rust bindings,
Rust logging bridge, зависящие от них upstream test suites и пока недоступный
ICU4X //base:i18n. Это НЕ stub i18n: цель отсутствует и её потребители должны
получить C++ ICU4C backend перед включением. Native //base до линковки не дошёл.
Первый полный native build -k0 выполнил 2210/2824 доступных шагов и закончил 1
из-за платформенных ошибок, оставшиеся шаги заблокированы зависимостями.
build/chromium/base-cpp-build.log содержит полный список. При окончании WSL
штатно размонтировала cache и позже перезапустила дистрибутив; это не OOM.

Следующие исправления уже записаны в prepare-platform.py, но полный base после
них НЕ пересобран: честные Perfetto OS_CHARMOS flags; PA_IS_CHARMOS/PA_IS_POSIX;
ICU U_HAVE_MMAP=0 (upstream stdio backend); BoringSSL OPENSSL_NO_SOCK (свой
ранее проверенный transport). --base теперь отключает Perfetto system IPC и
платформенные daemons, запускает audit-cpp-graph.py. Исходные лицензии/уведомления
ещё 12 base-зависимостей сохранены, проверяются через licenses/manifest.json.

Оставшиеся группы ошибок: Perfetto uid_t и CPU/boot/raw clocks, файловые fd flags,
Abseil native TID (pthread_t здесь указатель), ELF dynamic-symbolize/signal APIs,
native base files/threads/process backend. getpid() в старой libc пока возвращает
1 — исправить реальным readonly self identity API, не выдавать его за настоящий
идентификатор. Учёт CPU сейчас есть только в Job, личные thread/process counters
нужно добавить в scheduler charge hook, не подменять wall time.
Документ docs/abi/chromium-platform.md описывает ограничения/следующий backend.
M10 открыт, браузерного окна нет, не присылать старые GUI снимки как результат.

## Проверено 2026-10-18 (вечер): RuntimeQuery + libc fd v2
Полная пересборка и build/validate-fd-v2.sh: 30/30 host; QEMU зелёные —
browser runtime_cpp/runtime_json/tls/https, runtime (4), m3/preempt,
m8 libc/threads/signals/spawn, m10 editor/files_nav/windows (libc общая).
Логи build/chromium/native-platform-*.log, fd-v2-m10.log. Пакет закоммичен.
Следующее: пересобрать //base (--base) с уже записанными исправлениями
prepare-platform.py и закрывать оставшиеся группы ошибок настоящим кодом.

## Продолжение Chromium 2026-10-28
Повторный --base: build/chromium/base-resume.log; сборка завершилась ошибками
платформы (112 выполненных шагов из 700 доступных), браузерного ELF пока нет.
Добавлены timegm с нормализацией UTC и проверками границ/ошибок и native
Perfetto PlatformThreadId=pid_t / GetThreadId=gettid вместо pthread_t-указателя.
Патч Perfetto воспроизводимый, проверяет SHA-256 upstream. Full runtime явно
зависит от штатной генерации флагов Perfetto; Abseil tests отключены и для
этого профиля, поскольку они требуют Rust. Исходная незакоммиченная настройка
enable_perfetto_trace_processor_httpd=false сохранена.

Проверены: пересборка ОС; полный native C++ runtime и ELF audit; QEMU
browser/runtime_cpp (включая UTC DST hint, null/overflow и Perfetto ID главного
и четырёх рабочих потоков); m8/libc; 30/30 host. Логи utc-os-build.log,
utc-runtime-build.log, utc-runtime-guest.log, utc-libc-guest.log в build/chromium.
Повторный --base после изменений пишет build/chromium/base-after-utc.log.
Он завершился exit 1 после 1449/2037 шагов: 16 failed compile units,
оставшиеся цели заблокированы зависимостями. Ошибки timegm и преобразования
Perfetto pthread_t в число устранены; полный base до линковки не дошёл.
Оставшиеся интерфейсы: mmap, pipe/poll, временные файлы, uname, access,
signal/dynamic-symbolize, CCTZ и собственный backend base. Окна браузера нет;
M10 и порт Chromium не закрыты.
