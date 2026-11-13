# Нативный браузер CharmOS

Актуально: 2026-11-13. Журнал изменений и проверок: ../BROWSER-REVIEW.md.
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
- [x] Проверить native Blink/content_shell на реальных HTML/CSS/JS страницах,
  вводе и HTTPS. Просмотрены снимки Google/example.com и Unicode; см.
  `docs/browser-startup-review.md`.
- [ ] Довести новый shell UI до гостевого PASS: tab strip одного окна, меню,
  сохранение истории/закладок/поисковика и incognito уже реализованы в исходниках,
  но требуют компиляции и пользовательских mouse/keyboard проверок.
- [ ] Собрать и запустить настоящий `//chrome:chrome`: цель, entrypoint, ресурсы,
  initial preferences Яндекс, PDF/extension fixtures и full-Chrome harness уже
  добавлены; `chrome.elf` ещё отсутствует, поэтому extensions/PDFium не готовы.
- [ ] Проверить новый popup-role WM в реальных меню и регрессиях окон.
- [ ] Достичь cold <500 мс без предзагрузки: последний подтверждённый cold
  668,571 мс; warm и стабильность не закрывают этот критерий.
- [ ] Добавить кнопку «Сохранить DNS сайта». Перед реализацией уточнить,
  требуется экспорт DNS-записей или сохранение в локальном кэше.

## Текущее состояние

**2026-11-13: native content_shell реально запускается и отображает сайты.**
Подтверждённые регрессии, снимки и latency зафиксированы в
`docs/browser-startup-review.md`. Это ещё не полный Chromium: полный Chrome target
только добавлен и не собран, новые UI/WM изменения не прошли гостевые проверки.
Готовность отмечается только после пользовательских сценариев в CharmOS и просмотра
снимков; статические patch contexts и Python syntax не заменяют их.
