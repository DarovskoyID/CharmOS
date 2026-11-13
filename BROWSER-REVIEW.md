# Нативный Chromium: изменения для ревью

## Аудит рабочего браузера (2026-11-13)

- Подтверждённый исполняемый продукт — native `content_shell`, а не `chrome`:
  `content_port` зависит от `//content/shell:content_shell` в
  `tools/chromium/platform/BUILD.gn`. Поэтому правки desktop `chrome/browser`
  сами по себе не добавляют функции в запускаемый браузер.
- Адресная строка, назад/вперёд, reload/stop, поиск и in-memory закладки
  реализованы через `tools/chromium/shell-toolbar-patches.py`. Меню намеренно
  отключено до поддержки popup-окон. В этом UI нет вкладок, истории, настроек,
  команды incognito, установки расширений или PDF viewer.
- Отдельный off-the-record `ShellBrowserContext` существует в engine, но не
  считается доступным пользователю incognito без команды создания окна и проверки
  отсутствия записи истории.
- Настоящий Google/example.com rendering, Unicode, HTTPS foundation и регрессии
  уже подтверждены в `docs/browser-startup-review.md`; cold 668,571 мс не
  соответствует цели <500 мс (`passed=false`).
- Исправлена конфигурационная ошибка в `tools/chromium/build-platform.sh`:
  graphics/Ozone включают Blink, поэтому теперь получают тот же обязательный V8
  profile (без Temporal, pointer compression, WASM и cppgc cage), что и Blink.
  По требованию владельца сборка и QEMU не запускались; это изменение пока
  проверено только чтением конфигурации.
- Следующая реализация должна либо подключить настоящий desktop Chrome target,
  либо явно реализовать эквивалентные profile/history/tabs/extensions/PDFium
  сервисы в shell. Пустые пункты меню и desktop-only GN правки не засчитываются.

## Повторное ревью V8 (2026-11-05)

**Финальная проверка пакета 2026-11-06:** m4/ipc также PASS. Исправлена
скрытая зависимость native ELF от внешних libc.a и user.ld через LLD depfile.
Условие использует invoker_toolchain_args.current_os, чтобы не менять host
генераторы. Тест сохранённых зависимостей Ninja доказывает LINK без CXX
после изменения только mtime libc.a; время восстанавливается finally.
Тест PASS, новый аудит графа PASS (1313/5854 без Rust). Перелинкованный
ELF побайтово совпадает с проверенным гостевым ELF. Временный .d потребляет
Ninja; проверять нужно базу `ninja -t deps`, а не наличие этого файла.

**Дополнение 2026-11-06, ABI v1.20:** реальный PROT_NONE и обнуляющая
anonymous/private MAP_FIXED|PROT_NONE замена внутри существующего mmap
реализованы и проверены runtime_none/runtime_cpu, host test_paging и
строгим V8 gate. Неприсутствующие PTE сохраняют принадлежность фреймов,
не допускают повторного map поверх резервации и поддерживают защиту/unmap.
Это ещё не lazy backing или частичный munmap. Старые оговорки об advisory
PROT_NONE ниже относятся к исходному пакету до этого исправления.

**Итог 2026-11-06:** строгий verify-v8.sh --resume PASS: реальные JS/GC,
штатные Isolate::Dispose/V8::Dispose/DisposePlatform и pthread_join без
_exit, исходные weak-context проверки. ELF audit и host-проверка поздних
fatal ошибок PASS; runtime_none/runtime_cpu PASS со всеми исправлениями
прав памяти, копирования kExec, сигналов/rusage. Host test_paging PASS.
Описанные ниже неподдерживаемые сигнальные API теперь возвращают ENOTSUP;
обходы проверки списка контекстов отменены. Виртуальная память и JIT остаются
следующим блоком; браузер не объявляется готовым.

Исходный PASS проверяет выполнение трёх скриптов в interpreter-only режиме.
Он не подтверждает полноценный жизненный цикл: `_exit` обходит
`Isolate::Dispose`, завершение платформы и возврат рабочего потока.
Новый сценарий требует отдельную строку `Native V8: clean shutdown ok`.
До её гостевого подтверждения завершение V8 не считается проверенным.

При ревью найдены дефекты нового слоя исполняемой памяти:

- `VmoProtect` не проверял права capability исходного отображения. Это
  позволяло повысить read-only отображение до RW или RX. Добавлен предел
  исходных прав, действующий также после закрытия хендла, и проверка
  переполнения диапазона. Первый гостевой `runtime_none` с запретом
  повышения прав прошёл; расширенный пакет проверок выполняется отдельно.
- Защита части mmap обновляла одну общую запись защиты всей области.
  Следующее восстановление RW могло пропускаться и приводить к #PF.
  Учёт теперь постраничный; добавлен гостевой сценарий восстановления.
- `HandleDuplicate` не принимал новый бит `kExec`, хотя VMO им владел.
  Добавлена проверка законного копирования и RX-отображения такого хендла.
- MMIO не отклонял запрос исполнения вопреки документированному ограничению.
  Запрос теперь явно отклоняется до создания отображения.

Открытые проблемы исходного пакета: advisory `PROT_NONE`, фиктивные
`MAP_FIXED`/частичный `munmap`, успешные no-op `sigaction`, `sigaltstack`,
`pthread_kill`, обходы проверки списка контекстов GC и `SetDataReadOnly`.
Они требуют реализации либо явного отказа с адаптацией потребителя.
Выключенные JIT/профайлер сами по себе не означают, что эти libc-контракты
можно считать реализованными. Переход к RWX/PKU не принят: существующая
политика W^X сохраняется. Blink/content и браузерный UI ещё отсутствуют.

Этот файл ведётся по ходу завершения нативного браузера CharmOS. Здесь фиксируются рабочие пакеты, архитектурные решения, проверки и коммиты.

Текущий список задач: [plans/BROWSER-TODO.md](plans/BROWSER-TODO.md).
По запросу владельца добавлена кнопка «Сохранить DNS сайта».

## Исходная точка

- Последний существующий коммит: `9b836c7` (`M10: C++-only Chromium runtime, RuntimeQuery ABI 1.13 and libc fd v2`).
- Ветка `main` на этом коммите отправлена в новый remote `bottomweb`: `ssh://git@git.bottomweb.ru:2222/Darovskoy/CharmOS.git`.
- До начала этого продолжения рабочее дерево уже содержало большой незакоммиченный пакет M10: WM v2/v3, libc/SDK, Chromium base/runtime, Skia и native Ozone canvas. Он сохраняется и проверяется как исходный пакет, а не смешивается незаметно с новыми изменениями.
- По сохранённым логам исходного пакета пройдены `m10/window_managed`, `m10/canvas_runtime`, `m10/window_pixels`, `m10/graphics_runtime`, `browser/runtime_cpp`, `browser/base_runtime` и `m10/windows`.

## Журнал новых изменений

### Настройка продолжения

- Добавлен remote `bottomweb`; существующая история `main` отправлена на него.
- Создан этот файл ревью.

### Нативный Ozone backend

- Добавлен контракт `docs/abi/chromium-ozone.md`.
- Зарегистрирована штатная Ozone-платформа `charmos` через external-platform GN списки.
- Реализованы process-local registry числовых `AcceleratedWidget`, `PlatformWindow`, реальный display/work area, `SurfaceFactory` и `PlatformScreen`.
- Добавлены асинхронные delegate-уведомления о геометрии, фокусе, видимости и закрытии; native окно и canvas разделяют одного presenter-владельца.
- Добавлен перевод HID в `DomCode`/`KeyboardCode`, текстовые события, модификаторы, мышь и wheel.
- Добавлен `ozone_runtime` с QEMU-проверками кадров, resize, ввода и пикселей. Unsupported capture/fullscreen/custom cursor явно не выдаются за работающие и остаются следующим расширением WM.

### Текущий незавершённый пакет

- Ozone вынесен в отдельный GN package `tools/chromium/ozone`, чтобы исключить цикл с объявлением toolchain.
- Добавлены `docs/abi/chromium-mojo.md` и контракт native capability transport для следующего этапа.
- Настроены Aura/Views, C++ PDFium, BoringSSL без NSS и отсутствие host-зависимостей на GLib/GIO/XKB/Pango/CUPS.
- Добавлены C++ ICU4C реализации `tag_converters_charmos.cc`, `language_tag_matcher_charmos.cc`, `normalizer_charmos.cc`; ICU4X/Rust не подменяется успешными заглушками.
- Исправлено размещение upstream test-support guards: production `generate_ozone_platform_list` больше не исключён вместе с тестами. C++ graph audit сохраняет обязательную проверку Ninja-команд и отдельно отмечает `gn_desc_fallback`.
- `BROWSER-REVIEW.md` ведётся до завершения пакета, включая непроверенное состояние. Текущая сборка Ozone ещё не завершилась успешно; логи находятся в `build/chromium/ozone-block-driver.log` и `build/chromium/ozone-build.log` (последний создаётся после прохождения GN).

## Проверки новых изменений

### Ревью продолжения, 2026-11-03

Обнаружены и исправляются реальные дефекты незавершённого пакета:

- P1: HID usage напрямую приводился к DomCode без USB page 0x07; обычные
  клавиши не распознавались. Перевод теперь использует KeycodeConverter;
  пунктуация берётся из штатной таблицы Chromium. Левый и правый модификатор
  учитываются независимо, на потере фокуса сбрасываются удерживаемые клавиши.
- P1: повторный DispatchEvent для текста мог обращаться к уже уничтоженным
  окну и delegate. Перевод создаёт независимый пакет событий до dispatch;
  после каждого callback проверяется weak pointer владельца.
- P1: CreateCanvasForWidget читал UI-only реестр/PlatformWindow из render
  thread. Добавлена защищённая таблица сильных ссылок на presenter; raw window
  остаётся только на UI. Закрытие удаляет widget из обеих таблиц.
- P2: у фабрики наследовалось CanCreateNativePixmapForFormat=true при
  отсутствии реализации pixmap. Теперь возвращается false. Поиск окна под
  указателем учитывает подтверждённый порядок подъёма, а не первый ID.
- P1: RequestBounds игнорировал WouldBlock и терял последние размеры. Теперь
  запросы объединяются до последнего, исполняются после подтверждения,
  состояния геометрии и видимости учитываются раздельно.
- P1: verify-ozone-block использовал старый charmos-platform вместо новой
  сборки charmos-native. Исправлен путь, в том числе в provenance-отчёте.
- P1: patch context breakpad не был уникальным, puffin имел неверный SHA256
  и повторяющийся контекст. Подготовка исходников не могла завершиться.
  Исправлены точные контексты и хеш реального pinned source.
- P2: screen pixel проверки имели неправильные аргументы и порядок expected;
  Fail(int) был передан таймеру вместо void callback; исправлено.
- Аудит графа снова прекращает работу при ошибке GN: пустой список deps
  больше не может выдаваться за успешно проверенный граф.

Новые регрессии: HID/кавычки/оба Shift/Control/русский текст, фабрика canvas
на render thread, два последовательных resize до ACK, lookup после Close.
WM capture был лишь частично начат: реализованы обработчик, capability,
маршрутизация вне содержимого и Ozone SetCapture/ReleaseCapture; добавлены
проверки отказа другому окну, потери захвата при скрытии/смене фокуса и
реальной мыши вне границ. Этот пакет ещё ожидает общей сборки и гостевых тестов.

Дополнительно исправлены ошибки ICU4C-адаптеров: конструктор LocaleMatcher
был приватным, default fallback возвращал первый язык вместо отсутствия
совпадения, namespace icu конфликтовал с ICU namespace alias, отсутствовало
определение Impl, использовались неверные имена типов и функции нормализации.

Промежуточная проверка: полная сборка WM/SDK успешна; m10/window_managed
PASS с настоящими движением/кликом вне окна и потерей захвата. Логи
review-os-build.log и review-managed-guest.log. GN сформировал38119 целей,
включая холодные upstream test declarations; это не38119 компиляций.
Строгий audit Ozone затем выявил реальную транзитивную Rust-зависимость sfv.
Выбран уже существующий upstream C++ fallback QUICHE для HTTP structured
headers, сохранена лицензия. Аудит не ослаблен и заглушки парсера не добавлены.

Спецификация Mojo дополнительно помечена как ещё не реализованный контракт.
Уточнены настоящие пределы64KiB/16capabilities: крупные сообщения требуют
framing/reassembly, одной очереди недостаточно. Успех SDK IPC не считается
проверкой ещё не написанного Mojo transport.

### Проверка исходного пакета

- Полная сборка CharmOS: успешно; лог `build/browser-completion-baseline.log`.
- Host: **31/31 PASS**.
- QEMU: `m10/window_managed`, `m10/canvas_runtime`, `m10/window_pixels`, `m10/graphics_runtime`, `browser/runtime_cpp`, `browser/base_runtime`, `m10/windows` — **7/7 PASS**; лог `build/browser-completion-baseline-qemu.log`.

## Коммиты

- `1158560` — создан файл ревью, добавлен remote `bottomweb`, существующая история отправлена на оба сервера.
- `ddffba0` — проверенный унаследованный пакет WM v3, runtime и native canvas зафиксирован как baseline и отправлен на оба сервера.

## 2026-11-04: review of subsequent compatibility additions

The new package advances Ozone compilation, but ozone-build5.log still ends
with certificate backend, UDP types, variations platform, pixmap, IPC and
clipboard-provider errors. It is not a verified native browser build.

Confirmed defects repaired in the local libc adapter:
- FIONREAD invented zero bytes and accepted invalid descriptors. It now
  validates the descriptor and reports ENOTSUP without touching the output;
  FIONBIO changes the actual retained description and preserves other flags.
- pthread_setname_np reported success without storing any name; now ENOTSUP.
- Interface names lo/eth0 were invented. Interface enumeration and resolver
  configuration now use the existing native NetInfo syscall and actual DNS.
  The single native interface is net0/index1; absent hardware is not invented.
- select retained stale output bits on timeout, rounded down short waits,
  accepted invalid timevals, missed EBADF and contaminated requested sets.
- sendmsg/recvmsg used separate writev/readv calls, losing datagram boundaries
  and ignoring receive flags. Until a real atomic message adapter exists they
  fail explicitly before doing any I/O and validate socket descriptors.
- Original IPv4-only inet functions were still linked; the native strict
  IPv4/IPv6 conversion and real-kernel DNS adapters are now wired into libc.

Validation: complete OS build succeeds (review2-os-build.log). Real QEMU
m10/network_review passes (review2-network-guest.log), covering parsing,
canonicalization, random address round trips, output bounds, readiness,
invalid descriptors/timevals, FIONBIO, unsupported operations and numeric
resolver formatting. This does not verify Chromium networking or TLS.

### Ozone backend: зелёный (2026-11-05)

Проверено каноническим `tools/chromium/verify-ozone-block.sh`: `ozone_runtime`
собирается, ELF-аудит проходит, `m10/ozone_runtime` PASS (регистрация платформы,
фабрика на render-потоке, resize с подтверждением, клавиатура+текст, мышь/колесо,
закрытие по Esc, пиксели 24a148/f5c518). Регрессия `m10/window_managed` PASS.
`tools/qemu-screen.py` использует уникальное имя скриншота на процесс (устойчиво
к устаревшему root-овому файлу в sticky `/tmp`).

Исправлено:
- **GPU-стек исключён** из software-only графа (`ui/ozone/common/BUILD.gn`,
  `ui/gl/BUILD.gn`, `angle_shared_libvulkan`, `ozone_platform_headless=false`);
  ANGLE/SwiftShader/Vulkan/dawn/glslang/LLVM больше не компилируются.
- **libc**: сетевые заголовки/функции и multicast/BSD-типы; `select` поверх poll;
  `pthread_setname_np`; errno `EDQUOT/EUSERS/ENXIO`.
- **Mojo**: typed `PlatformHandle` kCapability, shared memory/каналы через VMO,
  `IPC::ParamTraits<long>` для `int64_t==long`.
- **Сертификаты**: встроенный Chrome Root Store (`chrome_root_store_only=true`),
  без Rust-TPM и системного хранилища.
- **PS/2 колесо** в ядре (`kernel/input/ps2.cpp`): IntelliMouse 4-байтный пакет,
  `kInputWheel`; исходный драйвер колесо не генерировал, из-за чего тест не
  мог получить wheel. Временная диагностика `wm.cpp` удалена.

Это не браузер: сетевой адаптер end-to-end, Blink/content, JIT-профиль V8 и UI
впереди.

### Mojo runtime: зелёный (2026-11-05)

`tools/chromium/verify-mojo-block.sh`: `mojo_runtime` собирается, C++-граф без Rust
(1444 deps / 6455 команд), ELF-аудит проходит, `m10/mojo_runtime` PASS. Проверено
в госте: capability endpoints, one-shot bootstrap по роли, read-only VMO,
перезапуск дочернего процесса; normal/ipcz каналы, сообщение 5 MiB, батчи
капабилити, backpressure и явный отказ; public invitation, message pipe,
передача shared-buffer и повторный запуск.

Исправлено: FD-based shared-memory broker (`mojo/core/broker_posix.cc`) поднимался
на CharmOS, потому что `SHARED_MEMORY_SERVICE_REQUIRED()` считала её POSIX, и падал
на `fcntl(F_GETFL)` по капабилити. Для CharmOS брокер отключён: память — прямые
VMO, капабилити — по нативному каналу.

Регрессия: `m10/ozone_runtime` PASS (перелинкован с новым Mojo-core),
`m10/network_review` PASS, `m10/window_managed` PASS.

### Executable memory / W^X: ABI v1.19 (2026-11-05)

Разблокировка V8/JIT. Добавлено право `rights::kExec` (bit 5, в `kVmoDefault`),
`VmoMap` с `kExec` (NX снят; `Write|Exec` одновременно отклоняется), новый
syscall `VmoProtect=83` (смена прав существующего отображения с TLB-сбросом на
всех CPU), libc `mmap(PROT_EXEC)` и `mprotect` (mmap RW → запись кода → RX →
исполнение; файловые exec-маппинги грузятся во временный RW-буфер и
переотображаются RX). Проверено `runtime/runtime_none` и `runtime/runtime_cpu`:
записан реальный код `mov eax,42;ret`, переведён в RX, выполнен (42), а запрос
`PROT_READ|PROT_WRITE|PROT_EXEC` отклонён с `EINVAL`. Документ —
`docs/abi/executable-memory.md`. Это не порт V8/Blink; убран только блокер
исполняемой памяти.

### V8: JavaScript выполняется (2026-11-05)

`m10/v8_runtime` PASS. В госте: `Native V8: JavaScript evaluation ok (arithmetic,
closures, loops, objects)`, затем `svc poweroff` → ACPI S5. Это настоящий V8
(isolate + context + parser + interpreter), а не заглушка: проверяются
`1 + 2 * 3`, `for..of` по массиву, замыкание, рекурсивный факториал и объект.

Граф V8 Rust-free: 1313 deps / 5854 команды (`--v8-graph`). Аргументы:
`v8_enable_temporal_support=false` (убирает Rust `temporal_capi`),
`v8_enable_pointer_compression=false`, `v8_enable_webassembly=false`,
`v8_use_external_startup_data=false`, `cppgc_enable_caged_heap=false`
(`cppgc_enable_larger_cage=false`). Цель `v8_runtime` запускает движок на потоке
с 8 МиБ стеком (bootstrap-стек ядра — 64 КиБ); бинарь пакуется как
`build/user/user/v8_runtime.bin`, чтобы 60 МиБ не попадали в initrd.

Что закрыто по пути:
- libc++ `<fstream>`/`ofstream`/`ifstream` без `std::filesystem`: `<fstream>` и
  `ios.instantiations.cpp` используют `_LIBCPP_HAS_LOCALIZATION` вместо
  `_LIBCPP_HAS_FILESYSTEM`; `fstream.cpp` в этой libc++ — только Windows-helper.
- libc: `ucontext.h`/`stack_t`/`sigaltstack`/`mcontext_t`/`sigaction`,
  `pthread_getattr_np`/`pthread_attr_getstack`/`pthread_attr_getguardsize`/
  `pthread_kill`, `getrusage`/`RLIMIT_DATA`, `tmpfile`, `MAP_NORESERVE`,
  `tm_gmtoff`/`tm_zone`, `sys/syscall.h`.
- libc mmap: `PROT_NONE`-резервации; `MAP_FIXED|PROT_NONE` внутри уже
  существующего маппинга (decommit/red zones V8); частичный `munmap` принимается
  как успех без освобождения (ядро не умеет дробить VMO-маппинг — утечка
  ограничена выравниванием); `madvise(MADV_DONTNEED)` — успешная подсказка.
- V8 платформа (`tools/chromium/platform/v8_platform_charmos.cc`): UTC timezone
  cache, пустой список shared libraries, no-op `SignalCodeMovingGC`/
  `AdjustSchedulingParams`, stack-trace stubs, `powl`.
- `OS::CanReserveAddressSpace()=false` для CharmOS → emulated
  `VirtualAddressSubspace`; `SetDataReadOnly` (заморозка `v8_flags`)
  пропускается; `--no-unmap-holes`.

**Обновление 2026-11-06 (ABI v1.20/v1.21):** PROT_NONE стал реально
недоступным (non-present листья), а `VmoReserve` сделал анонимные резервации
ленивыми. V8 больше не ограничивает CodeRange 64 МиБ и не требует
`-m 2048M`: строгий gate проходит на штатных 512 МиБ, включая полный GC,
`Isolate::Dispose`, `V8::Dispose`, `DisposePlatform`, `pthread_join` и clean
shutdown; host-check отклоняет поздние fatal-сообщения. Подробности —
`docs/abi/lazy-vmo.md`, `inaccessible-memory.md`.

**Ограничения:** JIT выключен (`--jitless`): V8 x64 pre-commit'ит code range как
RWX, а CharmOS держит W^X; для JIT нужна политика PKU/RWX в ядре. SIGPROF и
`getrusage` честно возвращают ENOTSUP; sampling/stack traces не реализованы.
Частичный `munmap` и освобождение committed-страниц (MADV_DONTNEED) остаются
будущей работой. Blink/content/UI не начаты.

### Lazy VMO: ABI v1.21 (2026-11-06)

`VmoReserve=84` создаёт VMO с нулевыми frame-индексами: `mmap(PROT_NONE)` больше
не выделяет физические кадры. `VmoMap`/`VmoProtect` с доступом коммитят
недостающие страницы атомарно (временный массив, OOM-rollback), `vmo_frames`
коммитит перед выдачей DMA-адресов, уничтожение VMO освобождает только
ненулевые записи. `VmoProtect` перечитывает физический адрес из VMO: alias,
стоявший non-present маркером с phys=0, корректно видит страницу, выделенную
другим alias. libc использует reserve для anonymous PROT_NONE и ведёт
постраничный known-zero бит, поэтому `MAP_FIXED|PROT_NONE` обнуляет только
dirty-runs, не коммитя нетронутую резервацию.

Проверено: host `test_paging` (11 тестов), `runtime_none`/`runtime_cpu`,
`m4/ipc`, строгий V8 gate на 512 МиБ и регрессия Ozone/Mojo/network — все `ok`.
Документ — `docs/abi/lazy-vmo.md`.

**ABI v1.22 (partial munmap):** `VmoUnmapRange=85` снимает только листья
выбранного диапазона; соседние страницы остаются доступны, `mprotect`/MAP_FIXED
поверх дыры дают `ENOMEM`, повторный unmap дыры — no-op, полный unmap
освобождает VMO. Фреймы остаются за VMO (физический discard — отдельная
задача). Проверено `runtime_none`/`runtime_cpu` (новый маркер
`runtime: partial munmap contract ok`), строгим V8 gate и повторной
регрессией Ozone/Mojo/network (08:06).
