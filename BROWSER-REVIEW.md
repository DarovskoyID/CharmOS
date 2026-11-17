# Нативный Chromium: изменения для ревью

## Аудит рабочего браузера (2026-11-13)

### Full Chrome: платформенная сборка (2026-11-15)

- Запущен продолжительный инкрементальный `chrome_port` в существующем
  `out/charmos-native`; старые объекты сохраняются, полная ОС не пересобирается.
  Первая `-k0` попытка дошла до 2633/17962 и выявила платформенные пробелы,
  после чего была остановлена для исправлений. Это ещё не гостевой PASS.
- Crashpad получил CharmOS `VMAddress/VMSize=uint64_t`, `METRICS_OS_NAME`,
  UUID через `base::RandBytes`, minidump OS tag, native signal names и
  POSIX-адаптации `st_mtime`, `/dev/null`, `_SC_OPEN_MAX`. `/proc`, `getdtablesize`
  и Unix `setresuid` не эмулируются; `DropPrivileges` остаётся no-op, потому
  что права процесса задаются capabilities при spawn.
- `components/crash/core/app` использует `crashpad_charmos.cc` вместо unsupported
  native backend: инициализация возвращает false, отчёты пусты, upload/database
  отсутствуют, прямой `GetCrashpadClient()` имеет явный FATAL. Crashpad client,
  handler, minidump и shared-memory backend не входят в CharmOS app target;
  существующий `ChromeCrashReporterClient` и browser engine сохраняются.

- Desktop WebUI теперь включает `version.mojom`, accessibility settings и
  QR-signin interfaces на CharmOS. Security interstitial Date/Time использует
  существующий `NOTIMPLEMENTED_LOG_ONCE` вместо compile-time `#error`.
  Проверка этих исправлений выполняется продолжением того же Ninja graph.
- Повторный Ninja подтвердил компиляцию всех ранее failed Crashpad objects,
  включая signals, close_multiple и minidump_misc_info_writer. Для QR Mojo
  дополнительно потребовался `enabled_features=["is_charmos"]`: одного
  `EnableIf` в `.mojom` недостаточно. `extensions::Command` использует стандартные
  desktop Linux keybinding names, без Linux userspace.
- Full Chrome переопределил Skia defaults и попытался собрать Rust PNG/Exif/
  Fontations также для Linux host tools. В `skia/BUILD.gn` CharmOS C++
  source-selection теперь применяется и к host tools порта, а существующие
  `graphic_shims_charmos.cc` добавлены в host target. Ошибочный промежуточный
  override в `third_party/skia/BUILD.gn` откачен точной pinned-миграцией.
  Rust не добавляется. Certificate manager получил такой же Mojo feature,
  как QR-signin, чтобы сохранить provisioned client cert в generated bindings.
- `gn gen` для `--chrome` использует `--root-target=//build/charmos:chrome_port`
  вместо `base_port`, чтобы проверка и генерация относились к полному продукту.
  Неподдерживаемые внешние `skia_use_*` args из `args.gn` удалены, а не скрыты
  отключением `--fail-on-unused-args`.
- Полный GN gen успешно создал 32373 targets. Host `graphic_shims_charmos.o`
  скомпилирован, прежние `SkPngRust*` и `SkTypeface_fontations` больше не
  попадают в начало Ninja graph. Для host `libpng` visibility расширена на
  `charmos_repo`, поскольку обычный `is_charmos` там ложен.
- Для PDFium добавлены native libc `ctime`, `ctime_r` и `wcsftime`: стандартный
  26-байтовый ctime buffer per thread, wide pattern через `wcsrtombs`, вывод
  через `mbstowcs`, включая Unicode-литералы. `runtime/runtime_threads` в QEMU:
  PASS, проверены epoch, оба ctime API, numeric и Cyrillic wide formats.
- Для ручной проверки `tools/run.sh --vnc` запускает графический QEMU через
  `127.0.0.1:5900` без GTK. VNC listener и загруженный desktop подтверждены;
  `Alt+B` запускает уже проверенный content_shell с multi-process/incognito.
  Полный Chrome этим запуском не подменяется и продолжает собираться отдельно.
- После добавления time functions PDFium `cjs_util.o` и `cmsps2.o` компилируются
  без прежних ошибок. `webui_url_constants.h` включает desktop `whats-new`
  definitions на CharmOS, устраняя следующий platform gate без удаления WebUI.
- Для длительного `chrome_port` используется `CHARMOS_BUILD_JOBS=8`: на 16 CPU
  и при >20 ГиБ доступной RAM это ускоряет проход без изменения состава ELF.
  Старые объекты сохраняются при каждом продолжении Ninja.
- Для `third_party/unrar` добавлен ABI v1.25 `fs_volume_stat` (syscall 87) и
  libc `statvfs/fstatvfs`: реальные ext2 block size/total/free blocks/inodes и
  read-only flag, `f_namemax=255`. FAT32 возвращает `NotSupported`. Инкрементальные
  kernel/user builds PASS; `m6/fs1_ext2` и `runtime/runtime_threads` в QEMU —
  оба PASS. Чужие `ExecutableFile` и process diagnostics в этих файлах не staged.
- Net C++ profile теперь действует и для Linux host tools: `CHARMOS_CPP_ONLY`
  отключает Rust SFV, QUICHE structured headers остаются рабочими. Kerberos/
  external GSSAPI выключен как отсутствующая зависимость. `CHROME_ROOT_STORE_ONLY`
  выбирает `CreateChromeOnlySystemTrustStore`. `structured_headers.o` и
  `cert_verify_proc.o` компилируются без прежних ошибок.

### Проверка рабочего UI, HTTPS и настройек (2026-11-15)

- Frame-сценарий реальными mouse/keyboard действиями завершён: drag, minimize,
  restore и Close не приводят к перезапуску или FATAL. В JSON было
  `frame_smoke_passed=true`, `passed=true`; снимки drag/minimized/restored
  просмотрены. Поздний Viz crash устранён порядком `OnClosed` → unregister.
- Multi-process сценарий подтверждает обычную и incognito вкладки, адресную
  строку и `https://example.com/`. Последний прогон: `history_smoke_passed=true`,
  `ui_smoke_passed=true`, `stability_passed=true`, `fatal_seen=false`,
  холодный старт 1287,431 мс при пределе проверки 2500 мс. Отдельная цель
  500 мс этим не закрыта.
- `build/browser-ui-history.png` и `build/browser-ui-settings.png` просмотрены:
  в меню есть `https://example.com/`, после настоящего клика Google отмечен
  галочкой, а кнопка поиска изменилась с `Я` на `G`. Это подтверждает
  пользовательское переключение настройки, а не только запись в JSON.
- Harness больше не использует номер старого incognito WM window как признак
  закрытия popup: ожидается destroy после текущего выбора, а большие перемещения
  указателя получают паузу перед кликом. Два прежних timeout относились к этим
  условиям ожидания, а не к FATAL браузера.
- Full-Chrome harness больше не ждёт несуществующий serial-лог смены заголовка
  расширения: `https://example.com` открывается явно, затем сохраняется снимок
  с видимым маркером content script. Расширение и PDF оба требуют визуального
  ревью; это изменение сценария ещё не является гостевым PASS полного Chrome.
- Временные логи Views/Aura, native input/capture, process-type и Mojo message
  diagnostics удалены. Пустые pinned patch entries явно восстанавливают upstream
  `widget.cc`, `root_view.cc` и `window_targeter.cc`: простое удаление записи из
  patch dictionary оставляло старый текст в source cache без пересборки. Миграция
  разрешает только точные прежние diagnostic fragments, 419 контекстов проверены,
  а очищенные исходники в кеше дополнительно прочитаны и подтверждены.
- После очистки инкрементальный Ninja завершил `[6/6] LINK ./content_shell.elf`
  и ELF audit. Новый бинарь повторно прошёл frame PASS и HTTPS/UI PASS:
  `history_smoke_passed=true`, `ui_smoke_passed=true`, `stability_passed=true`,
  `fatal_seen=false`, холодный старт 1318,174 мс. Все новые снимки просмотрены.
- Frame harness ждёт финальный `drag-move 340,...`, а не первый промежуточный
  кадр. Прежние два timeout были гонкой между двумя mouse-move: клик попадал
  либо в пустой заголовок, либо в Close; serial это подтвердил. После ожидания
  конечной позиции drag/minimize/restore/Close проходят на новом ELF.

### Повторное ревью чек-листа, 2026-11-14

### WM: popup capture и client-drawn рамка (2026-11-14)

- Видимый неактивный `kCreatePopup` теперь может захватывать указатель без смены
  фокуса. Это устраняет мгновенное закрытие Views-меню; прежний процесс доходил
  до `Esc`, а не исчезал до создания canvas. Координаты и ABI сообщений не менялись.
- Добавлены `kCreateBorderless=8` и `kFeatureBorderless=64`. Клиентская рамка
  использует полную рабочую высоту; обычные приложения сохраняют системный
  заголовок. Это основа для переноса вкладок Chromium в верхнюю строку окна,
  но не подтверждение готового Chromium UI: его сборка и screenshots отдельно.
- `window_managed` проверяет popup capture и borderless на всю рабочую высоту.
  Тест переведён на однократный `console managed`, чтобы после успешного выхода
  init не перезапускал его и не создавал ложный поздний сбой при poweroff.
- Проверка `tools/test.sh m10 90 window_managed`: PASS, штатный exit code 33,
  без второго запуска и без `managed window: failed` в serial.
- Shell теперь передаёт существующую init-команду `start` как `svc start <name>`;
  соответствующий запуск manual-сервиса был проверен в QEMU.

### Перенос вкладок в настоящий FrameView (текущая работа)

- Первый borderless снимок `build/browser-title-tabs-early.png` выявил две строки:
  стандартный Views title с кнопками, а под ним отдельный tab strip. Это не
  принимается как готовый результат.
- Добавлен `ShellFrameView`: tab strip переносится в него из content view,
  `GetBoundsForClientView()` отводит остальную высоту toolbar и странице.
  `TitleDragView` перемещает окно через native pointer capture; кнопки окна
  вызывают реальные `Widget::Minimize` и `Shell::Close`.
- WM различает явное `Destroy` приложения и обрыв соединения: первое не должно
  перезапускать браузер, второе сохраняет восстановление после падения.
- `tools/test.sh m10 120 mojo_runtime`: PASS, включая native endpoint, запуск
  ребёнка, IPC и повторный запуск. Следующий шаг — собрать FrameView и пройти
  пользовательские клики со свежими screenshots; пока PASS не объявляется.

- Свежие `build/browser-unicode-review.png`, `browser-ui-new-tab.png` и
  `browser-ui-menu.png` просмотрены: одна верхняя строка вкладок вместо синего
  title, адресная строка ниже, страница без дополнительной рамки. Настоящий click
  по `+` даёт `charmos-ui: new-tab 2 incognito=0`; меню полностью нарисовано,
  видны команды, история, выбор поиска и popup canvas. Отдельная проверка drag и
  window controls ещё требуется.
- Single-process incognito по-прежнему останавливается на штатном Chromium
  `DCHECK: single-process mode does not support multiple browser contexts`.
  Следующий запуск использует `CHARMOS_BROWSER_MULTIPROCESS=1` и GDB стеки выхода.

- Multi-process диагностика: utility и renderer получают `valid=1 errno=0` от
  `base::LaunchProcess`, а затем завершаются с `status=0 exit=0`. Ошибочный
  `GetProcessType()` в probe заменён на безопасный PID. Добавлены записи
  `charmos-main: process-type` и `charmos-mojo: recover role` для следующего запуска.
- Drag диагностика: `drag-start` и `capture-changed 1` подтверждены, но
  `drag-move` отсутствовал. В `Widget::OnMouseEvent` при этом были
  `pressed=0 capture=0`, а `RootView::OnMouseDragged` не вызывался.
- Найден конфликт захвата: `TitleDragView::OnMousePressed` вызывал
  `Widget::SetCapture` до завершения обработки нажатия самим `Widget`.
  Изменение global capture не проходило штатную проверку `current_capture`,
  поэтому `is_mouse_button_pressed_` оставался false. Лишний вызов и обходной
  `OnMouseMoved` удалены: Views должен сам выдать capture после pressed.
  Harness теперь посылает два реальных mouse-move и требует `drag-move`.
  Первый повторный QEMU показал `pressed=1 capture=0` на обоих движениях:
  подтверждение WM пришло только после release. Добавлено локальное ожидание
  capture request в Ozone; отказ WM по-прежнему приводит к `OnLostCapture`.
- После закрытия меню Viz поздно запросил surface уже удалённого widget 2 и
  упал на `Check failed: surface_ozone`. `PlatformWindowCharmOS::Close` теперь
  сначала вызывает `OnClosed` для разрушения host/compositor и только затем
  удаляет presenter из registry. Повторный QEMU больше не содержит
  `no presenter` или Viz FATAL после закрытия меню.
- Просмотрены свежие `browser-ui-drag.png`, `browser-ui-minimized.png` и
  `browser-ui-restored.png`: окно перемещается мышью, исчезает при minimize и
  возвращается по Alt+B. В serial оба движения имеют `pressed=1 capture=1`,
  `root-drag handler=1` и `drag-move`. `--close-first` PASS: настоящий Close
  вызывает `browser close requested/stopped`. После restore `WindowTargeter`
  показывал `visible=0` и отбрасывал ввод. Restore приходит без pending state,
  поэтому `UpdateState` не вызывался. Добавлены `kNormal` fallback и
  `Show`/`Hide` на `aura::Window`. Полный frame PASS пока не объявляется.
- Mojo probe показал согласованные буферы: например, `data_bytes=136` с
  `ids_offset=80` и `data_bytes=256` с `ids_offset=200`; указатель относителен
  к полю на смещении 40 и массив целиком помещается в сообщение. Ослаблять
  проверки `payload_num_interface_ids` не требуется. Избыточный log ограничен
  сообщениями с ненулевым смещением.
- Symbolizer показал, что повторяющийся SIGTRAP multi-process находится в
  `Message::version/header_v2`: probe безусловно читал V2 header также у
  сообщений версии 0/1. Это probe-induced DCHECK, а не доказанная порча Mojo
  transport. Добавлено `version() >= 2`; после инкрементальной сборки нужен
  новый пользовательский incognito-сценарий.
- Kernel GDB уточнил основной multi-process fault: `#PF` при `CR2=4` в
  `NotifyPeerClosureForSerializedHandles`, на ответе `Directory_OpenFile`.
  После ошибки отправки `TakeMojoMessage()` оставляет moved-from Message;
  повторное чтение его пустого `payload_buffer_` недопустимо. Добавлен выход
  для `!serialized_`, как и в соседней deserialize-ветке. Это устраняет только
  crash error-path, а не саму ошибку доставки.
- Для реальной доставки файловых handles ChannelCharmOS теперь преобразует
  Chromium FD в duplicate исходной kernel capability с сохранением прав;
  обратный `PlatformHandle::TakeFD()` импортирует capability как file/dir/socket
  FD через `charm_fd_from_handle`. Ранее не-capability handle просто отклонялся.
  `mojo_channel_checks` получил roundtrip `open/write → transfer → TakeFD/pread`.
  Первая линковка минимального runtime упала на `CollectStackTrace`;
  лишний LOG удалён, а существующий `partition_alloc_charmos.cc` добавлен
  в sources runtime, как и в других minimal executables.
  `tools/test.sh m10 120 mojo_runtime`: PASS, включая FD roundtrip.
  `--samples 0 --ui-smoke --cold-limit-ms 2500`: `passed=true`,
  `stability_passed=true`, `ui_smoke_passed=true`. Incognito создаётся и
  закрывается настоящими Ctrl+Shift+N/Ctrl+W, снимки просмотрены. Холодный
  multi-process старт 1,44 с; прежняя цель 500 мс отдельно не достигнута.
- Диагностика стека отдельно исключила stack overflow у Mojo IO thread: у него
  было около 12 КиБ использованного стека. TCP-probe GDB порта удалён из harness;
  ожидание `ss -ltn` не подключается к QEMU stub и не снимает `-S`.
- Следующий renderer fault доказал переполнение начального main stack:
  `RSP=0x7fffffef0000` — нижняя граница 64 КиБ, `CR2=base-8`, запись.
  `ContentMain` теперь целиком выполняется на dedicated pthread с 8-МиБ стеком,
  как и V8; startup handles и `RunContentProcess` не обходятся. На новом ELF
  renderer проходит HTML probe, открывает вкладки и incognito без прежнего #PF.

- Popup и обычная вкладка подтверждены настоящим pointer input в QEMU; ожидание
  harness исправлено на attachment второго Ozone canvas, потому что popup не
  создаёт второй startup-stage `Shell`.
- Incognito действительно упирался в `--single-process`: off-the-record
  BrowserContext нельзя подключить к renderer обычного профиля. Подключён
  прямой child launcher через native `base::LaunchProcess` и Mojo startup
  capability; отдельные renderer/GPU/network процессы реально стартуют.
- Удалён старый patch, скрыто возвращавший `--single-process` внутри
  `ShellMainDelegate`. Для уже подготовленных source caches добавлена точная
  миграция, не разрешающая произвольные vendor edits.
- Исправлены два следующих блокера: libc теперь сообщает документированный
  фиксированный stack главного потока через `pthread_getattr_np`; GPU можно
  оставить in-process при отдельном renderer.
- Полный многопроцессный gate пока НЕ зелёный: utility/network child-процессы
  завершаются и перезапускаются без диагностического FATAL, DOM-ready не
  достигается. До выяснения bootstrap failure обычный образ сохраняет
  проверенный `--single-process`; эксперимент включается только явным
  `CHARMOS_BROWSER_MULTIPROCESS=1`. Incognito поэтому ещё не закрыт.

- Подтверждённый исполняемый продукт — native `content_shell`, а не `chrome`:
  `content_port` зависит от `//content/shell:content_shell` в
  `tools/chromium/platform/BUILD.gn`. Поэтому правки desktop `chrome/browser`
  сами по себе не добавляют функции в запускаемый браузер.
- Исходный toolbar содержал адресную строку, назад/вперёд, reload/stop, поиск
  и in-memory закладки. Ниже отдельно перечислены последующие дополнения;
  расширения и PDF viewer ещё не реализованы.
- Настоящий Google/example.com rendering, Unicode, HTTPS foundation и регрессии
  уже подтверждены в `docs/browser-startup-review.md`; cold 668,571 мс не
  соответствует цели <500 мс (`passed=false`).
- Исправлена конфигурационная ошибка в `tools/chromium/build-platform.sh`:
  graphics/Ozone включают Blink, поэтому теперь получают тот же обязательный V8
  profile (без Temporal, pointer compression, WASM и cppgc cage), что и Blink.
  По требованию владельца сборка и QEMU не запускались; это изменение пока
  проверено только чтением конфигурации.
- В Ozone разрешены opaque popup/tooltip/menu top-level surfaces, которые ранее
  отклонялись. Временно WM отображает их как обычные managed окна с рамкой, а не
  как бесрамочные popup. Меню toolbar включено; translucent surfaces по-прежнему
  честно отклоняются. Это исходная правка, гостевой клик по меню ещё не проверен.
- Промежуточные managed-окна заменены настоящими вкладками одного `Shell`:
  `Shell::NewTab/SelectTab/CloseActiveTab` сохраняют независимые WebContents,
  а Views tab strip показывает заголовки, `+` и закрытие. Incognito использует
  настоящий off-the-record ShellBrowserContext; новые вкладки наследуют профиль.
- Добавлена история последних URL обычного профиля с открытием и очисткой.
  Запись выполняется в `/data/browser-history.txt`; incognito отфильтровывается
  через `IsOffTheRecord()`. Это не Chromium History Service: время визитов,
  синхронизация и полнотекстовый поиск пока отсутствуют.
- Яндекс выбран начальным поисковиком. Выбор Google/Яндекс сохраняется в
  `/data/browser-settings.txt`; закладки — в `browser-bookmarks.txt`. История,
  закладки и настройка поиска восстанавливаются из файлов при новом запуске.
  Для записи используется поддерживаемый native `base::File`, а не исключённый
  на CharmOS POSIX `AppendToFile`.
- Патч применён к pinned `content_shell` и Python-файл разобран синтаксически;
  компиляция и гостевые клики по новым командам ещё не выполнялись по запрету
  повторной сборки. Нельзя считать этот пакет гостевым PASS.
- Начат переход к настоящему desktop Chrome: добавлены `chrome_port`,
  `--chrome/--chrome-graph`, CharmOS entrypoint в `chrome_initial` и native link
  dependencies. В этом профиле upstream по умолчанию включает `enable_extensions`
  и `enable_pdf` (PDFium). Проверены 409 patch contexts и bash syntax; GN gen и
  компиляция ещё не выполнялись, поэтому расширения/PDF не объявляются рабочими.
- Полный Chrome получил native C `main` в Aura entrypoint и регистрацию isolated
  web app scheme на CharmOS. PDF child initialization остаётся штатным
  `ENABLE_PDF` кодом `ChromeMainDelegate`; отдельной подмены PDFium нет.
  Проверены 411 pinned contexts; запуск полного Chrome ещё не подтверждён.
- `base_paths_charmos.cc` выбирает `/bin/chrome` и `/data/chrome`, когда полный
  Chrome установлен; прежний content_shell остаётся fallback. Добавлены native
  `/data` home/cache/desktop пути. `tools/qemu/browser.sh` получил режим
  `CHARMOS_FULL_CHROME=1`: упаковывает обязательные resources/locales, выдаёт
  ошибку при их отсутствии и запускает Chrome с отдельным user-data-dir.
  Размер full-Chrome образа увеличен до 1 ГиБ. Bash syntax проверен, образ не
  пересобирался.
- Устранён ранний `NOTREACHED()` в `ChromeCrashReporterClient::GetProductInfo`:
  CharmOS получил собственный product id и crash-metrics path. Сбор/upload
  телеметрии не включался. Проверены 412 patch contexts, гостевой запуск ещё
  не выполнялся.
- Полный Chrome получил `tools/chromium/initial_preferences.json`: стартовая
  страница и поиск Яндекс, штатный TemplateURL/suggestions, без импорта чужого
  профиля. First-run initial preferences loader подключён на CharmOS без Linux
  upgrade utility; файл ставится рядом с `/bin/chrome`. JSON, bash и 413 patch
  contexts проверены, фактическое применение preferences в госте ещё не проверено.
- Добавлены стандартные Ctrl+T/Ctrl+W/Ctrl+Shift+N в shell. Browser harness
  получил `--ui-smoke`: реальные QEMU sendkey создают обычную и incognito вкладки,
  закрывают вкладку и сохраняют три PNG. PASS требует подтверждающих маркеров,
  стабильности процесса и cold target. Python AST и 413 patch contexts проверены;
  сам guest-сценарий и просмотр новых снимков ещё не выполнялись.
- Для будущей гостевой проверки добавлены локальный PDF и unpacked Manifest V3
  extension с background service worker и content script для example.com.
  Full-Chrome образ устанавливает их в `/data/tests` и загружает расширение через
  штатный `--load-extension`. PDF открывается пользователем по file URL. JSON и
  Bash проверены; рендер PDF и выполнение extension ещё не подтверждены гостем.
- Добавлен `tools/chromium/test-full-chrome.py`: уже собранный full-Chrome образ
  запускается через Alt+B, PDF открывается через Ctrl+L/file URL, затем example.com
  проверяется по заголовку от content script. Ctrl+T/Ctrl+Shift+N/Ctrl+W покрывают
  вкладки/incognito. Сохраняются шесть PNG и JSON; PDF требует явного визуального
  ревью, наличие файла не засчитывается как PDF PASS. Python AST проверен, гостевой
  запуск пока не выполнялся.
- Managed WM v3 расширен совместимым `kCreatePopup=4`/`kFeaturePopup=32`
  (`docs/abi/wm.md`). Меню, tooltip, drag и bubble Ozone больше не получают
  системный заголовок/кнопки; содержимое, input и configure используют нулевое
  смещение рамки. Обычные окна сохраняют прежний kTitle. Это пока статически
  проверенный исходный пакет: сборка и реальный клик по меню не выполнялись.
- `--ui-smoke` теперь также выполняет настоящий mouse click по overflow menu,
  ждёт маркер `charmos-ui: main-menu` и сохраняет `browser-ui-menu.png`. Старый
  screenshot просмотрен только как historical baseline: на нём ещё нет нового
  toolbar/tab strip, поэтому он не является подтверждением нового дизайна.
- Popup с запрошенной Views translucency больше не отклоняются: содержимое
  выводится opaque, скругление/тень принадлежат WM. Per-pixel alpha не заявляется;
  обычные translucent application windows по-прежнему получают явный отказ.
- После разрешения владельца запущена реальная сборка `--chrome`. Первый GN gate
  нашёл unresolved `linux_cros_extra_data` и `eula_dialog_linux`; Linux integration
  data исключены на CharmOS, platform-independent EULA target подключён отдельно.
  PDF fixture исправлен: фактическая длина content stream 99 байт совпадает с
  `/Length`. Результат повторной сборки ещё не получен.
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

### Полный Chromium: сборка и пользовательская проверка (2026-11-16, в работе)

После проверенного `content_shell` начат штатный `//chrome:chrome`, а не замена
браузера тестовой оболочкой. В платформенный порт добавлены нативные Crashpad,
C++-only конфигурации Skia/net, Chrome Root Store, необходимые Mojom/WebUI,
источники PDFium и libc `ctime`/`wcsftime`/`statvfs`. Текущая цель —
`//build/charmos:chrome_port`; результаты её компоновки и запуска ещё не
подтверждены.

`tools/qemu/browser.sh` в режиме `CHARMOS_FULL_CHROME=1` упаковывает настоящий
`chrome.elf`, ресурсы, локали, PDF и локальное расширение Manifest V3.
`tools/chromium/test-full-chrome.py` запускает отдельный QEMU без компиляции ОС,
открывает браузер клавишами и снимает старт, PDF, HTTPS с расширением, новую
вкладку, историю, настройки, Back/Forward и Incognito. Content script теперь
показывает маркер только после ответа `chrome.runtime` от service worker;
таким образом снимок проверяет обе части расширения. Все перечисленные снимки
требуют визуального просмотра; зелёный exit-сценария сам по себе не считается
доказательством работоспособности UI.

На момент этой записи полный Chrome ещё компилируется; расширения, PDFium и
полный браузерный UI в госте не заявляются рабочими до завершения этой проверки.

Промежуточный пакет полного Chrome: 475 hash-checked patch contexts. Добавлены
C++ реализации enterprise skill frontmatter и Stable Portability history (без
Rust), desktop declarations для Web Apps/passwords/feature engagement,
Chrome native-messaging paths, PDF Searchify resource, Enterprise Proxy и
корректный отказ Device Trust без attestation backend. `getifaddrs` теперь
выдаёт IPv4 и link-layer запись с настоящим MAC из `NetworkInfo`; `unrar`
получил `umask`, `S_IFCHR` и `link()` с явным `ENOTSUP`. Harness сам включает
`CHARMOS_FULL_CHROME=1` и `CHARMOS_BROWSER_MULTIPROCESS=1`, чтобы случайно не
проверять content_shell. Пакетная проверка контекстов и Python/Bash syntax
выполнена; повторная компиляция и пользовательский QEMU PASS ещё не получены.

Продолжение полного Chrome (2026-11-16): 522 pinned patch contexts. QR generator
реализован через существующий PDFium C++ ZXing encoder с byte-input и явной
минимальной версией; Rust protobuf stream framer заменён эквивалентным C++
parser. Добавлены desktop WebUI/signin/download guards, OpaqueBrowserFrameView,
app-management generation и missing strings. ProcessSingleton использует тот же
Chromium lock/cookie protocol по loopback TCP на порту, детерминированному от
profile path, поскольку Unix domain sockets в CharmOS отсутствуют. Исправлены
тестовые Safari/Rust GN dependencies и duplicate patch ownership. Опциональный
screen-AI Searchify не вызывается без backend, PDFium остаётся включён.
Проверены patch contexts и Python/Bash syntax; инкрементальный `chrome_port`
запущен, окончательная компоновка и пользовательский QEMU PASS ещё не получены.

Checkpoint (2026-11-16): 526 pinned contexts. Добавлены native TTS/WebUI
declarations, regional capabilities, NTP variations, isolated Web App scheme,
ClientCertStoreEmpty и platform-specific version/metrics branches. Исправлены
ProcessSingleton socket includes/options и Crashpad/WASM guards. Duplicate GN
entries разрешены: реальные patch ownership и dependency chains проверены.
Полный Chrome ещё компонуется; функциональность в госте не заявляется.

Checkpoint (2026-11-16): 531 pinned contexts. Rust-only Reading Mode evaluator,
ATHM token service и Firefox/NSS importer исключены только на CharmOS: для них
нет соответствующего backend, поэтому они не блокируют основной браузер.
Обычная история, PDFium, вкладки, extensions, ProcessSingleton и profile settings
остаются в полном `chrome_port`. Инкрементальная компиляция продолжается без
ошибок; линковка и гостевой запуск ещё не подтверждены.


Checkpoint (2026-11-16): 533 pinned contexts. Исправлены ошибки текущей
инкрементальной компиляции полного Chrome: C++ QR generator получает include path
к PDFium; Stable Portability history использует актуальный typed JSONReader API и
безопасную границу uint64; desktop overload RegionalCapabilitiesService доступен
на CharmOS; WebApp включает объявление isolated-app scheme. Неполный
OnDeviceTranslationInstaller и protobuf-dependent certificate provisioning
исключены при выключенных backends, а PrivateVerificationTokens factory не
содержит unreachable code. Добавлены недостающие desktop feature declarations.
Изменённые patch replacements сохраняют предыдущие prepared-состояния как
промежуточные, поэтому защита от unknown vendor edits остаётся действующей.
`prepare-platform.py --check-only` подтвердил 533 контекста; Python AST и
`bash -n` зелёные. Компоновка и пользовательский QEMU PASS ещё не получены.


Checkpoint (2026-11-16): обновлённый GN профиль успешно сгенерирован; полный
`chrome_port` дошёл до browser-компонентов. Найденный incomplete-type guard
уточнён: `OnDeviceTranslationInstaller` исключается только на CharmOS внутри
существующего `!ANDROID` блока, без ссылки на неэкспортированный buildflag и без
скрытия `ProfileLaunchObserver`/smart-restart members. 533 patch contexts снова
проверены. Текущая Ninja-задача ещё выполняется; финальная линковка и QEMU не
подтверждены.


Checkpoint (2026-11-16): исправлены ещё две browser-ошибки. CharmOS использует
`net::ClientCertStoreEmpty` напрямую, с нужным include и без недоступного
enterprise provisioning wrapper. `sandbox.mojom.Sandbox.kScreenAI` объявлен
без EnableIf: иначе генератор опускал enum и `ScreenAIServiceFactory` терял
обязательный `kServiceSandbox`; ScreenAI service backend остаётся выключенным.
Патчи применены к подготовленным исходникам, 533 контекста проверены. Текущая
Ninja-задача продолжает независимые объекты; линковка/QEMU ещё не подтверждены.


Checkpoint (2026-11-17): 547 pinned contexts. Разобрана финальная группа ошибок
прохода 25052/25077. Добавлены supervised-user declarations/GN sources и desktop
WebUI declarations; Isolated Web App scheme/favicons работают на CharmOS.
Недоступные provisioned-cert source, Contextual Tasks schema и ScreenAI utility
backend не регистрируются; обычный PDFium остаётся включён. QR encoder wrapper
перенесён в PDFium target, чтобы его internal macros/span не смешивались с
Chromium base config. Stable Portability больше не использует Rust string type.
Crashpad include/SetFirstChance guards объединены в одном patch ownership.
Для federated compute добавлены native C++ filesystem create_directories/remove
и недостающие POSIX errno, без Linux userspace. Resource IDs присутствуют в
сгенерированном `generated_resources.h`. Patch contexts зелёные; повторная
инкрементальная компиляция, линковка и пользовательский QEMU PASS ещё впереди.


Checkpoint (2026-11-17): 548 pinned contexts. `fxbarcode` разрешён на CharmOS
без включения несвязанного PDF XFA; QR wrapper использует только существующий
PDFium barcode target. Предыдущий запуск остановился на этом GN assert до
компиляции. Остальные проверки остаются в силе, линковка/QEMU ещё впереди.


Checkpoint (2026-11-17): исправлено применение QR dependencies. Старый
`third_party/pdfium/third_party/BUILD.gn` блок перекрывал новый bigint patch;
теперь dependency объединена с существующим владельцем и подготовленный GN файл
фактически содержит `pdf_enable_xfa || is_charmos`. AST-проверка подтверждает
отсутствие duplicate keys во всём graphics patch dictionary; удалены только
избыточные более ранние subset-блоки bubble/ATHM. Полный Chrome ещё не слинкован.
