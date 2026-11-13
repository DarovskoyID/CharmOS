# Актуальное состояние браузера (2026-11-13)

Собранный образ уже запускает native `content_shell`: Blink/V8, адресная строка,
навигация, HTTPS и реальные страницы работают. Просмотренные снимки Google и
example.com, результаты регрессий и незакрытая цель cold <500 мс описаны в
`docs/browser-startup-review.md`. Это не полный Chromium UI: вкладки, история,
настройки, доступный пользователю incognito, расширения и PDFium в браузере ещё
не подтверждены. История ниже содержит старые этапы; её нельзя читать как текущий
статус. Последние правки GN graphics/Blink профиля не собирались по требованию
владельца.

# Предыдущее продолжение (2026-11-06): lazy VMO зелёный

**Итог пакета 2026-11-06 (ABI v1.21, lazy VMO):** новый syscall
`VmoReserve=84` создаёт VMO с нулевыми frame-индексами; физические кадры
выделяются только при commit (map с доступом / mprotect / `vmo_frames`).
Commit атомарный: кадры сначала аллоцируются и обнуляются во временный массив,
при OOM VMO не меняется. `VmoProtect` перечитывает физический адрес из VMO
(`PageTables::remap_4k`), поэтому alias, стоявший маркером с phys=0, видит
страницу, выделенную другим alias. Уничтожение VMO освобождает только
ненулевые записи. `vmo_frames` коммитит до выдачи адресов оборудованию.

libc: anonymous/private `mmap(PROT_NONE)` использует `VmoReserve`; постраничный
`zeroed`-бит позволяет `MAP_FIXED|PROT_NONE` не трогать нетронутую резервацию,
а обнулять только dirty-runs. Документ — `docs/abi/lazy-vmo.md`.

**ABI v1.22 (partial munmap):** `VmoUnmapRange=85` снимает листья выбранного
диапазона внутри существующего VMO-маппинга, не удаляя запись Mapping и не
освобождая фреймы VMO. libc ведёт постраничный `live`-бит: соседние страницы
остаются доступны, `mprotect`/`MAP_FIXED` поверх дыры дают `ENOMEM`, повторный
`munmap` дыры — no-op, полный `munmap` освобождает VMO. Физический discard
(освобождение committed-страниц) остаётся будущей работой.

**ПРОВЕРЕНО:** host `test_paging` (11 тестов, включая remap inaccessible
marker → real frame → второй commit с другим frame); `runtime/runtime_none` и
`runtime/runtime_cpu` (`lazy VMO reservation and alias commit ok`, 32 МиБ
sparse reserve + alias; `partial munmap contract ok`); `m4/ipc`; строгий V8 gate
(`m10/v8_runtime`) — полный GC, `Isolate::Dispose`, `V8::Dispose`,
`DisposePlatform`, `pthread_join`, clean shutdown, host-check поздних fatal —
на **штатных 512 МиБ** QEMU RAM (без `-m 2048M` и без ограничения CodeRange
64 МиБ). Регрессия фундамента после lazy VMO: `m10/network_review`,
`m10/ozone_runtime`, `m10/mojo_runtime` — все `ok` (07:27); повторно после
ABI v1.22 — все `ok` (08:06).

**Следующее:** освобождение committed-страниц (MADV_DONTNEED/discard) и
произвольные fixed-резервации; `SetDataReadOnly`/JIT (PKU или scoped RWX),
затем Blink/content, Chromium net adapter и UI.

**Blink C++-профиль (2026-11-06):** граф Blink теперь **без Rust** —
`//third_party/blink/public:blink` = 7206 зависимостей / 32607 команд, ни одной
Rust-зависимости и ни одной Rust-команды (`build-platform.sh --blink-graph`).
Для этого: XML переключён на C++/libxml2 `XMLDocumentParser`; `jxl`,
`symphonia` и `dav1d/crabbyavif` отключены; `base32` и WebAudio FFT переписаны
на C++ в `fft_frame`/`base32.cc`; `rust_crash` исключён; шаблон `rust_target`
при `enable_rust=false` создаёт пустую C++-группу без Rust-кода. Дальше идёт
`blink_port` compile и закрытие платформенных ошибок; content shell ещё не
собран.

**Blink compile 2026-11-07:** Rust parsers replaced by C++ in
`build/charmos/{mpeg_audio_ffi,jpeg_parser_ffi}.{h,cc}`; WebGPU/TFLite/SwiftShader
and host ALSA/Pulse are disabled for CharmOS; the libc gained the portable POSIX
surface the media/GPU closure needs (stat fields, `sys/file.h`, `sys/auxv.h`,
`utime.h`, `uchar.h`, `malloc`/futex constants, 64-bit stdio aliases). The
`blink_port` group has passed >12k/20.5k compile edges; remaining errors are
being closed iteratively.

**No-Rust feature status 2026-11-07:** the XML path is the C++/libxml2 parser;
font format detection is plain C++ SFNT scanning; Ed25519 signature checks use
BoringSSL. IFT patching, ECDSA verification and signed-web-bundle parsing are
compiled out/rejected rather than ported (they were Rust bridges). TFLite/WebNN,
SwiftShader, ANGLE-WGPU/Dawn-Vulkan and host ALSA/Pulse are disabled for the
software-only profile.

**Verified 2026-11-07:** `blink_port` compiles to completion (5780/5780, 0
errors); `--blink-graph` reports **7082 dependencies / 31236 commands, no Rust
dependencies and no Rust compile commands**. Guest regressions after the libc
ABI additions: `runtime` (cpu/none/threads/tpm) and `browser`
(base_runtime/https/platform/runtime_cpp/runtime_json/tls) all `ok`. A
`//content/shell:content_shell` build target (`content_port`) has been added for
the next phase.

**Blink-рекогносцировка 2026-11-06** (`tools/chromium/blink-recon.sh`):
`//third_party/blink/public:blink` — 7223 зависимости, `//content/public/app:app`
— 12171; тянут ui/gpu/media/device/cc/services. Rust: `temporal_capi`
(отключается `v8_enable_temporal_support=false`), `jxl_rs`
(`enable_jxl_decoder=false`) и безусловный `//third_party/rust/xml/v1` через
`blink/renderer/core:xml_ffi`. Перед `--blink-graph` нужно выбрать политику:
документированное исключение для upstream-Rust в Blink либо отключение/порт
XML-парсера. Blink пока не начат.

# Предыдущее продолжение (2026-11-06): ABI v1.20 и link dependencies

**Итог последнего пакета 2026-11-06 — проверки зелёные.**
ABI v1.20 PROT_NONE/zeroing replacement подтверждены runtime_none,
runtime_cpu, host test_paging, строгим V8 GC/Dispose/join gate и m4/ipc.
Исправление зависимостей линковки завершено: правило только у native
toolchain (у host флага нет), Ninja сохраняет LLD depfile во внутренней
базе и удаляет временный .d. test-link-dependencies.py читает `ninja -t deps`
и реально доказывает: новая mtime только libc.a планирует LINK без CXX;
исходная mtime восстанавливается finally. Этот тест и новый полный аудит
графа PASS (1313 deps / 5854 commands, без Rust). После одноразовой
пересборки ELF совпал побайтово с уже проверенным в госте, новый guest не
требовался. Последний m4/ipc PASS после всех изменений; фоновых сборок нет.
Финальный тест зависимостей и аудит выполнены последовательно после
исправления теста чтения временного depfile; новую компиляцию не запускали.
Для дальнейшего повтора есть verify-link-dependencies.sh --resume.

Следующий большой шаг: lazy VMO backing. Начальный дизайн записан в
docs/abi/lazy-vmo-draft.md (НЕ реализован): отдельный VmoReserve, backing
при явном commit/mprotect, обновление физических адресов alias PTE,
rollback OOM и known-zero tracking в libc, затем настоящий V8 gate на
512 МиБ. Сейчас физическое выделение ещё eager, QEMU V8 всё ещё 2 ГиБ.
Не объявлять этот draft реализацией. Partial munmap/общий MAP_FIXED,
JIT/SetDataReadOnly, Blink/content, Chromium net adapter и UI впереди.

**Следующий пакет 2026-11-06: ABI v1.20, настоящий PROT_NONE.**
Неприсутствующие PTE с software-битом 9 сохраняют владельца фрейма; защита
восстанавливается на месте, unmap удаляет и такие листья. Внутри приватной
libc-области anonymous/private MAP_FIXED|PROT_NONE теперь обнуляет страницы
и закрывает доступ, не меняя соседние страницы. Документ:
docs/abi/inaccessible-memory.md. Физические кадры пока выделяются заранее.
Проверены OS build, host test_paging, runtime_none/runtime_cpu и строгий
V8 gate с новой libc: inaccessible-{os-build,host,none,cpu,v8-driver}.log
в build/chromium; все PASS. Проверки подтверждают BadAddress при реальном
user-copy из PROT_NONE, сохранение данных при обычном mprotect и нули при
фиксированной замене. Права capability после None не повышаются.

Обнаружен отдельный дефект: внешний libc.a не указан в зависимостях Ninja
для native ELF. Текущий V8 был принудительно перелинкован перед гостем.
Добавлен pinned-патч gcc_toolchain.gni: LLD --dependency-file + depfile для
native link, а также verify-link-dependencies.sh/test-link-dependencies.py.
Первый вариант ошибочно проверял current_os и задел host-генераторы;
сборка остановлена, условие исправлено на invoker_toolchain_args.current_os.
Промежуточная стадия сохранена для безопасного prepare уже изменённого дерева.
Последняя проверка этого исправления выполняется отдельно; её PASS ещё
нужно подтвердить. Логи link-dependencies-{gn,build,guest}.log.
Скрипт требует LLD depfile с libc.a/user.ld, проверяет отсутствие флага у
host toolchain и доказывает через ninja -n, что изменение только libc.a
вызывает LINK без CXX. Если ELF изменился после правил, повторяет гостя.

**2026-11-06, после перезагрузки:** строгий V8 gate прошёл с исходными
проверками weak-context list: ArrayBuffer/исключения/JSON, полный GC,
Dispose платформы и join без _exit; host-проверка поздних fatal ошибок PASS.
Также прошли runtime/runtime_none и runtime/runtime_cpu со всеми правками
capability/protection/signals/rusage; host test_paging PASS до перезагрузки.
Команда verify-v8.sh --resume дала подтверждённый cache hit графа,
пересобрала 436 шагов с CHARMOS_BUILD_JOBS=12 и прошла ELF audit.
Логи: build/chromium/v8-build.log, build/test-m10-v8_runtime.serial,
build/test-runtime-runtime_none.serial, build/test-runtime-runtime_cpu.serial.
Следующий блок — реальные виртуальные резервации/освобождение/lazy VMO.

**Последняя остановка: 2026-11-06, перезагрузка по просьбе пользователя.**
Точные изменения, проверенное состояние и команда продолжения — в разделе
«Остановка перед перезагрузкой — 2026-11-06» в конце этого файла.
Строгий GC/Dispose gate ещё не пройден; ниже сохранён исторический JS smoke.

## Предыдущий результат (2026-11-05): V8 выполняет JavaScript

**ПРОВЕРЕНО 2026-11-05:** гость прошёл `m10/v8_runtime` — `Native V8: JavaScript
evaluation ok (arithmetic, closures, loops, objects)`, затем `svc poweroff` и
ACPI S5 (`ok m10/v8_runtime`). Это реальный V8 на CharmOS: изолятор, контекст,
парсинг и исполнение JS (арифметика, `for..of` по массиву, замыкания, рекурсия,
объекты). Профиль — **interpreter-only (`--jitless`)**, см. ограничения ниже.
Канонический гейт — `tools/chromium/verify-v8.sh` — воспроизводит это целиком.

**ПРОВЕРЕНО 2026-11-05:** канонический `tools/chromium/verify-browser-foundation.sh`
зелёный целиком: OS-сборка, GN 38109 целей, графы Ozone (2450 deps/9425 команд) и
Mojo (1444/6455) без Rust, оба ELF-аудита (static x86-64, без loader/TLS), и тесты
`m10/network_review`, `m10/ozone_runtime`, `m10/mojo_runtime` — все `ok`.
Повторный прогон после libc/V8-изменений — снова все три `ok`.

**Плюс ABI v1.19 — W^X исполняемая память** (разблокировка V8/JIT): право
`rights::kExec` (bit 5, включено в `kVmoDefault`), `VmoMap` с `kExec` (NX снят,
W^X: `Write|Exec` в одном запросе отклоняется), новый syscall `VmoProtect=83`
(смена прав существующего отображения с TLB-сбросом на всех CPU), libc
`mmap(PROT_EXEC)` и `mprotect` (mmap RW → код → mprotect RX → исполнение).
Проверено `runtime/runtime_none` и `runtime/runtime_cpu`: реальный код `mov
eax,42;ret` записан, переведён в RX, выполнен (42), W+X отклонён с EINVAL.
Документ — `docs/abi/executable-memory.md`.

**V8 — зелёный smoke (interpreter-only).** Аргументы: `v8_enable_temporal_support=false`
(убирает Rust `temporal_capi`), `v8_enable_pointer_compression=false`,
`v8_enable_webassembly=false`, `v8_use_external_startup_data=false`,
`cppgc_enable_caged_heap=false`/`cppgc_enable_larger_cage=false` (32 ГиБ cppgc cage
не помещается в модель VMO). Граф V8 **Rust-free: 1313 deps / 5854 команды**.
Добавлены: цель `v8_runtime` (реальные JS-выражения, запуск на потоке с 8 МиБ
стеком), режим `build-platform.sh --v8-graph`, `package-v8.sh`, гостевая
фикстура `tests/qemu/m10/v8_runtime`.

Что потребовалось в libc/патчах:
- `V8_OS_CHARMOS`/`V8_OS_POSIX` (`v8-patches.py`), `tm_gmtoff`/`tm_zone`,
  `sys/syscall.h`+`syscall(__NR_gettid)`.
- C++ iostreams: `<fstream>` больше не требует `std::filesystem` (ключ —
  `_LIBCPP_HAS_LOCALIZATION`), explicit instantiations в `ios.instantiations.cpp`
  включены тем же условием.
- POSIX: `ucontext.h`/`stack_t`/`sigaltstack`/`mcontext_t`/`sigaction`,
  `pthread_getattr_np`/`pthread_attr_getstack`/`pthread_attr_getguardsize`/
  `pthread_kill`, `getrusage`/`RLIMIT_DATA`, `tmpfile`, `MAP_NORESERVE`.
- `mmap`: `PROT_NONE` резервация, `MAP_FIXED|PROT_NONE` внутри существующего
  маппинга (V8 decommit/red zones), частичный `munmap` принимается как no-op
  (ядро не умеет дробить VMO; утечка ограничена выравниванием).
- `madvise(MADV_DONTNEED)` — успешная подсказка (swap нет).
- V8-платформенные заглушки CharmOS (`v8_platform_charmos.cc`): timezone UTC,
  пустой список shared libraries, `SignalCodeMovingGC`, `AdjustSchedulingParams`,
  stack trace stubs, `powl`.
- V8 `OS::CanReserveAddressSpace()=false` → emulated `VirtualAddressSubspace`
  (аллоцируется выровненным `mmap`, сегменты — mprotect/decommit).
- `SetDataReadOnly` (заморозка `v8_flags`) пропускается: mprotect страниц образа
  пока не поддержан.
- CodeRange ограничен 64 МиБ через `ResourceConstraints` (по умолчанию x64
  резервирует 512 МиБ, а ядро выделяет физические кадры VMO сразу).
- JSDispatchTable резервирует 256 МиБ → гостевой сценарий запускает QEMU с
  `-m 2048M` (`tools/qemu/v8_runtime.args`).

**Ограничения, не выдавать за большее:** JIT выключен (`--jitless`) — V8 x64
хочет RWX-страницы code range, а CharmOS держит W^X; включение JIT требует
PKU/RWX-политики ядра. Профайлер (SIGPROF) зарегистрирован, но сигнал не
генерируется. Teardown изолятора пропускается (`_exit`) из-за debug-check в
финальном GC. Blink/content/UI ещё не портированы.

Канонический гейт V8 — `tools/chromium/verify-v8.sh` (OS-сборка, `--v8-graph`,
ninja, ELF-аудит, `m10/v8_runtime`); контракт и ограничения —
`docs/abi/v8-runtime.md`.

Что закрыто в этом пакете:
- **GPU-стек убран из software-only графа** (было ~600 ошибок). `//ui/ozone/common`
  тянул `//ui/gl`→ANGLE→SwiftShader/dawn/Vulkan/glslang/LLVM. В `graphics-patches.py`:
  для `is_charmos` убраны EGL/GL-источники и `//ui/gl*`, `angle_shared_libvulkan`
  выключен, Vulkan-loader не линкуется, `ozone_platform_headless=false`. Граф ~9400
  команд, Rust нет.
- **Сетевые заголовки/функции libc**: `net/if.h`, `netinet/tcp.h`+`tcp_info`,
  `ifaddrs.h`, `resolv.h`, `sys/un.h`, `sys/select.h`, `sys/ioctl.h`, `memory.h`;
  `socketpair/sendmsg/recvmsg/getsockname/getpeername`, `gethostname/geteuid/getpeereid`,
  `isascii/llabs/pthread_setname_np`, `select()` поверх poll, `ioctl`, `res_ninit`,
  errno `EDQUOT/EUSERS/ENXIO`, multicast и BSD-типы.
- **Mojo native capability** (`mojo-patches.py`): `PlatformHandle::kCapability` /
  `MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY=7`, shared memory/каналы через
  VMO-капабилити, не FD/`ScopedFDPair`/`SCM_RIGHTS`; `IPC::ParamTraits<long>` для
  `int64_t==long`. FD-based broker (`mojo/core/broker_posix.cc`) для CharmOS
  отключён через `SHARED_MEMORY_SERVICE_REQUIRED()` — иначе он падал на
  `fcntl(F_GETFL)` по капабилити; память — прямые VMO, капабилити — по каналу.
- **Сертификаты**: `chrome_root_store_only=true` → встроенный Chrome Root Store
  (`CreateBuiltinWithChromeRootStore`), без системного хранилища и Rust-TPM.
- **Разные `#error`/`strict_cast`/`WARNING`** ветки для CHARMOS; `PLATFORM_CHARMOS`.
- **PS/2 колесо (ядро, `kernel/input/ps2.cpp`)**: добавлено включение IntelliMouse
  (magic 200/100/80, id 3), 4-байтные пакеты, `kInputWheel` («вверх» = +). Без этого
  гостевой тест не получал wheel. Отладочные `printf` в `wm.cpp` удалены.

**Не сделано/не выдавать за готовое:** это НЕ браузер. V8 выполняет JS только в
interpreter-режиме (JIT/W^X — отдельная задача), профайлер неактивен. Впереди
Blink/content (настоящие HTML/CSS/JS-страницы), UI браузера, реальные сетевые
запросы/DNS/TLS end-to-end.

# Предыдущее продолжение (2026-11-04)

Пользователь попросил продолжать браузер после оценки чужой работы4/10.
Предпочтения: минимум текста; сборка только завершёнными пакетами, без
перезапуска компилятора после мелких правок. Не запускать субагентов.

Проверено: полная OS сборка review2-os-build.log; QEMU m10/network_review PASS
(review2-network-guest.log). Исправлены libc select/ioctl, фиктивные интерфейсы
и DNS state, pthread_setname_np, ложная sendmsg/recvmsg реализация через readv/
writev. inet_address.cpp/netdb.cpp реально включены в libc, старые inet удалены.
NetInfo общий net_info.hpp, реальный net0/index1 и DNS ядра. IPv6 — только
форматирование/парсинг, не транспорт. Сеть/TLS ОС существовали раньше.

АКТИВНО: session80314 — with-cache verify-browser-foundation.sh, driver
build/chromium/review5-foundation-driver.log; compiler build/chromium/foundation-build.log,
out/charmos-native. ~1700/2111 пересобираемых/новых единиц, ошибок пока нет.
Не запускать
одновременно другой compiler, QEMU или prepare. Ждать завершения и собрать
весь список ошибок. GN уже успешен:38108targets, C++ audit2450deps/9424commands,
без Rust; новый Mojo audit1444deps/6455commands тоже PASS (Ozone9425commands).
Предыдущая полная
Ozone compile5176/5230 закончилась только multicast types/constants и3
unreachable warnings; все исправлены общим пакетом. Первый GN выявил host
//crypto:tpm из нового root-store generator;
профиль без Rust теперь исключает настоящий неподдерживаемый TPM и в host
утилите, без ослабления audit. Chromium sourcecache может отмонтироваться при
остановке WSL; запускать через with-cache.sh 1000 1000.

ТЕКУЩАЯ сборка проверяет: CHROME_ROOT_STORE_ONLY=true, настоящий Chrome-only
verifier, offline certificate_checks_charmos.cc, native variations enum1000,
исправленные gfx/legacy IPC/dragdrop platform branches. Оригинальные staged
patches сохранены, новые преобразования дописаны вконец graphics_patches().
Legacy IPC больше не сообщает успех с пустой памятью. Цель — ozone_runtime
link+ELFaudit+QEMU; driver сам упакует icudtl.dat и публичные test PEM через
package-certificate-fixtures.py, затем guest. Не редактировать running shell.

В ТЕКУЩИЙ ПАКЕТ УЖЕ СКОПИРОВАН, НО ЕЩЁ НЕ ПРОВЕРЕН В ГОСТЕ Mojo:
mojo-patches.py расширен native endpoint
bootstrap по ролям0x50000000..0x5fffffff, собственный ChannelCharmOS в
platform/mojo_channel_charmos.cc, framing v1 (см docs/abi/chromium-mojo.md),
корректные native counts/rights shared buffer; shared_memory_charmos.cc
TakeOrFail возвращает unexpected при невалидном объекте/нулевом размере.
Новые mojo_runtime.cc, mojo_channel_checks.cc, mojo_ipcz_checks.cc проверяют
typed ownership, child bootstrap/restart, large5MiB,20caps, backpressure,
concurrent writers, malformed frames, public invitations/message pipes/VMO.
Есть target в platform/BUILD.gn, strict audit whitelist, verify-mojo-block.sh
и QEMU m10/mojo_runtime fixture. verify-browser-foundation.sh уже пересобралOS,
подготовил единый профиль, прогнал оба audit и компилирует обе цели. Далее
сам выполнитELFaudit, упаковку ресурсов иQEMU network_review/ozone_runtime/
mojo_runtime. Не редактировать работающий shellscript.
Добавлен независимыйSDK charm/network.hpp, libc используетего; localhost::1
и AI_ADDRCONFIG дляmappedv4 исправлены. network_review теперь последовательный:
service cNet, addressesизshell, compatibilityизshell сявнымNoNetпроверкой.
Новый --check-only в prepare проверяет276 pinned patch contexts беззаписи,
прошёлдообщейпересборки. verify-mojo держит тот же --ozone-graph профиль.
Два новых C++ файла проверки сначала падали на incomplete CertNetFetcher и
capturing lambda в BindRepeating; оба исправлены, успешно скомпилированы.
Затем linker выявил5missing symbols: PathProviderPosix и Abseil allocator/
threadidentity/semaphores. Исправлено цельным пакетом: base_paths_charmos.cc
реально возвращает существующие configured directories, FILE_EXE/MODULE
честно false (kernel не выдаёт exe path). DIR_ASSETS=/bin override остаётся.
Abseil low_level_alloc.h pinned patch включает allocator на настоящем native
anonymous mmap, отключает async-signal-safe arena. Не включён глобальный
ABSL_HAVE_MMAP, так как native subset не равен всемуPOSIX. Для этого core
header пришлось пересобрать2111 units (включаяhostgenerators); не прерывать.
mojo_channel_checks теперь форсирует contention и timeout AbseilMutex.
gethostname теперь берётuname.nodename (unset "(none)"), а не выдуманныйcharmos;
network_review дополнен соответствующим контрактом.

Ускорение аудитов: graph_cache.py + audit-cpp-graph.py receipts соSHA256всех
GNinputs, Ninja manifests, args/stamp/omittedRust иpolicyhash; 5 тестовPASS.
Фактический snapshot25082 inputs~5.71sec. Старые reports текущегоaudit НЕ имеют
graph_snapshot: в первой версииcache не учлиdirectories в GNdepfile. Исправлено
hash directory membership; нужен следующий полныйaudit дляnewreceipt, далее
CPP-only cachehits. Нельзя заявлять, что реальныйcachehitуже проверен!
graph_cache.py --is-current OUTPUT REPORT[...] готов для безопасной проверки
freshness передresume. ПОСЛЕ окончания pipeline поправить verify-browser-
foundation.sh --resume: сейчас он безусловноskipGN; нужно послеprepare
использовать --is-current, еслинетсовпадающегоreceipt — повторить normalGN.
НЕ редактироватьshellпокаонработает. Подготовитьcacheprooftestsзатемвсе
criticalCPPfixes однопакетно. ДокументыABIиREADMEдополнены.

Новых push/commit не делали. Браузер ещё не готов: нет Blink/V8/content/UI.

# Предыдущее продолжение (2026-11-03, ревью чужого Ozone)

Пользователь: ревью работы другой модели и довести браузер до конца; затем
спросил оценку. Ответили в commentary4/10 с конкретными найденными ошибками;
РАБОТУ НЕ ОСТАНАВЛИВАТЬ. Подробный журнал BROWSER-REVIEW.md.
Другой агент закоммитил исходный проверенный пакет ddffba0, добавил remote,
написал untracked полный Ozone, но ни разу успешно его не собрал. Историю и
изменения сохранили. Никаких новых push/commit в этом продолжении не делали.

Исправлены в repo: HID->DomCode через USBpage07+converter; punctuation штатная,
левый/правый modifiers независимы; event translation отдаёт отдельный vector
до delegate dispatch и weakcheck после каждого (убран UAF char после raw).
Threadsafe presenter lookup отдельно от UI-only registry; Close убираетwidget.
Boundscoalescing сохраняет последний resize, не теряет WouldBlock; разные
pendingstates bounds/visibility. WindowDisplay неправильные work_width поля
исправлены. Таймер intFail->void, pixel keys+expected порядок исправлены.
SDK/WMcapture реально завершён и включён в Ozone SetCapture/ReleaseCapture.
WM nativeAlt теперь доставляется приложениям. WM capture feature16/t10/event110.

ПРОВЕРЕНО: review-os-build.log полная OS сборка; review-managed-guest.log
PASS m10/window_managed (включая отказ чужому окну, потерю capture при
скрытии/фокусе и настоящий mouse motion/button вне400x220 содержимого).
Первый GN исправления: неоднозначный breakpad patch; неверный puffinSHA.
Вместо удаления breakpad tools выбран настоящий .elf suffix в copy_exe;
у puffin точные unique contexts и .elf. Остальной старый staged patch history
сохранён, новые transforms добавлять после старых, иначе prepare отвергаетcache.
Одна команда GN теперь делает38117 targets из4412files примерно213сек
(это declarations, не компиляции). Проверка Ozone closure строгая, GNdesc
ошибка больше НЕ превращается в fake-success пустого deps.

Реальный audit выявил Rustsfv в //net. Выбран СУЩЕСТВУЮЩИЙ upstream C++
QUICHE fallback в net/http/structured_headers.cc; нет заглушек. Лицензия
tools/chromium/licenses/quiche.txt иmanifest добавлены. Последний audit:
2646deps/10848commands, Rust отсутствует.

АКТИВНАЯ СБОРКА: session93055 (WSL with-cache verify-ozone-block.sh),
build/chromium/review-ozone-driver.log, build/chromium/ozone-build.log.
Ninja -k0 -j4 ozone_runtime в out/charmos-native (НОВЫЙ полныйprofile).
Не запускать параллельно другой compiler/QEMU/prepare. Старый out/charmos-platform
baseline остаётся, verify-ozone раньше ошибочно читалего — исправлено.
Последнее состояние: ~7000/10848, compile продолжает собирать ВСЕ ошибки -k0.
Известны ошибки: ICU language_tag не имеет public ICU dep (исправлено в repo),
CBOR USE_CBOR_RUST true (выбран настоящий C++ parser), crypto TPM Rust headers
(неподдерживаемый TPM parser исключён; fake software provider возвращает nullptr).
Не хватает in6_addr/sockaddr_storage/AF_INET6/netdb.h/gethostname/getsockname,
native cert selection. Mojo уже падает в core_ipcz/shared_buffer/
platform_handle_utils/cpp system на .fd/readonly_fd у настоящего UniqueHandle.
Нужен реальный typed PlatformHandle/native channel/bootstrap/VMO adapter;
не добавлять fake AF_UNIX/SCM_RIGHTS или casts capability->FD.
Driver после compile попробует ELF audit+QEMU; не редактировать running shell!

ВАЖНО: ПОСЛЕ запуска session93055 в repo добавлены ещё исправления, пока НЕ
скопированные prepare в cache: native IcuBridge::normalizer всегда выбирает
реальный ICU4C (иначе featureflag может deref nullptr), runtime NFC/NFD/NFKC и
HTTP QUICHE проверки, прямой //net dep runtime, QUICHElicensemanifest. Текущая
первая компиляция НЕ содержит их. После полного сбора ошибок потребуется
prepare+GN/rebuild вместе со всеми необходимыми critical fixes. Не выдавать
текущую проверку за финальную. Нативные ICU adapters уже в cache исправлены:
private LocaleMatcher ctor ->builder.build, setNoDefaultLocale; ICU namespace
alias; incompleteImpl; types/functions. Runtime запускает InitializeICU и
пакетирует настоящий icudtl.dat в/bin, копия изpinned treeвbuild/user/user.

Оставшееся: фактически собрать/проверить Ozone и его новые регрессии, затем
Mojo nativecapability consumers (docs/abi/chromium-mojo.md пока ТОЛЬКОспека),
Blink/V8/network/browser. Fullscreen/borderless/customcursor ещёunsupported.
Это по-прежнему НЕ готовый браузер. Пользователь требует мало текста,
не компилировать после мелких200строк, только завершёнными пакетами.

# Предыдущее продолжение (2026-10-29, WM v3 + Ozone canvas)

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
## Остановка перед перезагрузкой — 2026-11-06

Пользователь попросил перезагрузить ПК. Текущая проверка остановлена;
активных ninja/clang/QEMU/python проверки после остановки не осталось.
Изменения сохранены в рабочем дереве, коммиты не создавались.

Повторное ревью V8 обнаружило и исправило:
- повышение прав read-only VMO через VmoProtect; предел исходной capability
  сохраняется после закрытия хендла, диапазоны проверяются без переполнения;
- отказ HandleDuplicate копировать законный kExec; запрет исполнения MMIO;
- неверный общий учёт защиты при mprotect части mmap: теперь постраничный;
- временное снятие PTE при смене защиты: новая protect_4k обновляет лист
  атомарно, сохраняет accessed/dirty и не выделяет таблицы;
- фиктивный успех sigaction/sigaltstack/pthread_kill/getrusage: неподдерживаемые
  действия теперь честно возвращают ENOTSUP, выходные структуры не портятся.
  Запрос отсутствующего alternate stack и SS_DISABLE поддерживаются.

V8-проба возвращает штатные Isolate::Dispose/V8::Dispose/DisposePlatform и
pthread_join вместо _exit; добавлены ArrayBuffer/исключения/JSON/полный GC.
Обходы трёх проверок weak native-context list отменены воспроизводимыми
обратными стадиями v8-patches.py. Сценарий ждёт clean shutdown после join,
возврат shell; v8_runtime.check.py дополнительно отклоняет позднее падение.

Проверено: первая версия запрета повышения прав + постраничного учёта прошла
runtime/runtime_none; последний полный OS build прошёл; новый host test_paging
PASS. Первый native V8 build с Dispose слинковал ELF и прошёл ELF audit
(review-v8-build.log), но helper завершился EOF после изменения его текста
во время исполнения. Не менять выполняемые shell-скрипты! Этот проход НЕ
содержал последних GC/JS-расширений и не запускался в госте.

Последний запуск verify-v8.sh --resume остановлен на graph_cache.py
--is-current (прочитал >1.5 ГиБ входов за несколько минут), до компиляции и
гостя. prepare уже применил последние изменения. Поэтому строгий V8 gate
и расширенные runtime_none/runtime_cpu пока НЕ подтверждены. Старый
build/chromium/v8-build.log всё ещё от прежнего запуска, не считать новым.

Продолжить (WSL, с правами root для mount cache):
`wsl -d Ubuntu -u root --cd /mnt/a/CharmOS -- bash tools/chromium/with-cache.sh 1000 1000 env CHARMOS_BUILD_JOBS=12 bash tools/chromium/verify-v8.sh --resume`
После завершения компиляции/гостя последовательно:
`bash tools/test.sh runtime 100 runtime_none`
`bash tools/test.sh runtime 100 runtime_cpu`
Не запускать QEMU параллельно с компилятором. Не править prepare/vendor или
исполняемые shell-скрипты во время сборки. Затем обновить proof/status docs.

Остаются реальные PROT_NONE/MAP_FIXED/partial munmap и lazy VMO; нынешние
no-op нарушают контракт и могут сохранять целые выровненные аллокации без
ограничения суммарной утечки. W^X не ослаблялся, PKU/RWX не выбран.
SetDataReadOnly ещё no-op. Blink/content/Chromium net adapter/UI ещё не готовы.
