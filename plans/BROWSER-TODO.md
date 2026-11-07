# Нативный браузер CharmOS

Актуально: 2026-11-06. Журнал изменений и проверок: ../BROWSER-REVIEW.md.
Этот список относится к явно запрошенному порту Chromium и не меняет порядок
остальных фаз ROADMAP.md. Готовность подтверждается гостевыми тестами.

## Сделано и проверено

- [x] Нативные C/C++ runtime и базовые компоненты Chromium.
- [x] Skia: настоящее окно ОС, растровая графика, шрифты, PNG и ввод.
- [x] Оконный SDK v3: размер, заголовок, видимость, фокус, поколения буферов
  и откат при отказе; совместимость с v2.
- [x] Захват мыши, события за границами окна, снятие захвата при скрытии
  и смене фокуса. Проверено m10/window_managed.
- [x] SurfaceOzoneCanvas: передача кадров между потоками и подтверждение
  от компоновщика. Проверено m10/canvas_runtime в исходном пакете.
- [x] W^X исполняемая память (ABI v1.19): право `kExec`, syscall `VmoProtect`,
  libc `mmap(PROT_EXEC)`/`mprotect` (mmap RW → код → RX → исполнение; W+X
  отклоняется). Проверено `runtime/runtime_none` и `runtime/runtime_cpu`.
- [x] **V8 выполняет JavaScript (2026-11-05):** `m10/v8_runtime` PASS —
  isolate/context/parser/interpreter, проверены арифметика, `for..of`,
  замыкания, рекурсия и объекты. Граф Rust-free (1313 deps/5854 команды).
  Профиль interpreter-only (`--jitless`): V8 x64 требует RWX code range, а
  CharmOS держит W^X; JIT — отдельная задача (PKU/политика RWX в ядре).

## Осталось

- [x] Закрыть повторное ревью V8: штатные GC/Dispose/join без `_exit` и
  обходов проверки списка контекстов; честный отказ неподдерживаемых
  сигнальных API и rusage; регрессия прав VMO и постраничной защиты.
  **Проверено 2026-11-06:** строгий `m10/v8_runtime`, `runtime_none`,
  `runtime_cpu` и host `test_paging` PASS.
- [x] Реализовать ленивые физические кадры и ленивый VMO.
  **Проверено 2026-11-06 (ABI v1.21):** `VmoReserve`, commit при map/mprotect,
  атомарный OOM-rollback, refresh физ. адреса alias-маркера, commit в
  `vmo_frames`; libc использует reserve для anonymous PROT_NONE и не коммитит
  нетронутые known-zero страницы при MAP_FIXED. Host `test_paging`,
  runtime_none/runtime_cpu, m4/ipc и строгий V8 gate на 512 МиБ PASS.
  Документ — `docs/abi/lazy-vmo.md`.
- [x] Реализовать реальный частичный `munmap`.
  **Проверено 2026-11-06 (ABI v1.22):** `VmoUnmapRange=85` снимает листья
  диапазона; соседние страницы живут, mprotect/MAP_FIXED поверх дыры → ENOMEM,
  повторный unmap — no-op, полный unmap освобождает VMO. runtime_none/runtime_cpu
  и строгий V8 gate PASS. Документ — `docs/abi/application-sdk.md`,
  `libc-mapping.md`.
- [ ] Освобождать committed-страницы (MADV_DONTNEED/discard) и поддерживать
  произвольные fixed-резервации вне существующего libc-маппинга. Сейчас
  фреймы VMO освобождаются только при уничтожении VMO.

- [x] Завершить исправления нового Ozone и получить зелёный ozone_runtime:
  регистрация платформы, фабрика, delegate, клавиатура/мышь, resize и закрытие.
  **Проверено 2026-11-05:** `m10/ozone_runtime` PASS (verify-ozone-block.sh).
  GPU-стек исключён из software-only графа; PS/2-колесо добавлено в ядро
  (`kernel/input/ps2.cpp`, IntelliMouse 4-байтный пакет, `kInputWheel`).
- [x] Завершить native Mojo: типизированные capabilities, VMO, транспорт,
  большие сообщения, запуск дочерних процессов и гостевые проверки.
  **Проверено 2026-11-05:** `m10/mojo_runtime` PASS — capability endpoints,
  one-shot bootstrap, read-only VMO, child restart; normal/ipcz channels, 5 MiB,
  capability batches, backpressure/rejection; public invitation, message pipe,
  shared-buffer transfer, restart. FD-based broker отключён для CharmOS.
- [ ] Доделать сетевой адаптер Chromium: адреса/DNS, асинхронные сокеты,
  реальные запросы и TLS-проверка сертификатов.
- [ ] Собрать и запустить Blink/content: настоящие HTML/CSS-страницы с DOM и
  рендерингом. **V8 уже зелёный** (`m10/v8_runtime`, interpreter-only); Blink
  ещё не начат и требует V8 + `//third_party/blink` в графе.
  JIT-профиль V8 (RWX code range / PKU) — отдельная задача ядра.
  **Рекогносцировка 2026-11-06 (`blink-recon.sh`):** `//third_party/blink/public:blink`
  — 7223 зависимости, `//content/public/app:app` — 12171; тянут
  ui/gpu/media/device/cc/services. Rust удаляется для CharmOS: `xml_ffi`
  заменён на C++/libxml2 `XMLDocumentParser`; `jxl`/`symphonia`/`dav1d`
  отключены; `base32` и WebAudio FFT переведены на C++; `rust_crash` исключён;
  шаблон `rust_target` при `enable_rust=false` даёт пустую C++-группу без
  Rust-кода. Дальше — `--blink` compile и закрытие платформенных ошибок.
- [ ] Доделать интерфейс браузера: адресная строка, вкладки, навигация,
  загрузки, настройки и установка/запуск из оболочки ОС.
- [ ] Добавить кнопку «Сохранить DNS сайта». Перед реализацией уточнить,
  требуется экспорт DNS-записей или сохранение в локальном кэше.
- [ ] Завершить недостающие оконные функции и общие проверки браузера:
  несколько окон, ошибки сети, перезапуск, сохранение данных и ресурсы.

## Текущее состояние

**2026-11-05: фундамент зелёный.** Канонический `verify-browser-foundation.sh`
проходит целиком — OS-сборка, GN, графы Ozone (2450 deps/9425 команд) и Mojo
(1444/6455) без Rust, оба ELF-аудита, гостевые тесты `m10/network_review`,
`m10/ozone_runtime`, `m10/mojo_runtime` — все `ok`. Это Ozone-платформа, native
Mojo-транспорт и сетевой слой; **это ещё не браузер.**

**Следующий этап — сам браузер (content/Blink/V8 + UI).** Измерено по GN (без
компиляции): `//content/shell:content_shell` — 19286 зависимостей, из них 5
Rust-крейтов: `third_party/rust/harfrust`, `jxl`, `sfv`, `temporal_capi`, `xml`;
`//content:content` — 12143/6; `//chrome:chrome` — 15270/6. Крупные модули:
third_party 10529, components 2357, services 2062, ui 1770, media 626, content 429,
gpu 266, mojo 190, v8 85, skia 73, sandbox 39. Нужно: заменить/исключить эти Rust
крейты (sfv→QUICHE и harfrust→C++ уже сделаны для net/graphics, но в content-shell
замыкании они ещё видны), закрыть платформенные пробелы content/Blink/V8/sandbox/GPU
и собрать ~19k целей. Это на порядок больше фундамента и требует отдельных больших
пакетов; Ozone-тест не считается готовым браузером.
