# Нативный Chromium для CharmOS

Порядок владельца: браузер, затем ИИ, затем магазин. Это часть открытого M10.
Нельзя выдавать браузер хоста, удалённый экран или собственную HTML-заглушку
за порт Chromium. Нужны настоящие расширения Chromium и встроенный PDFium;
выбран полный Chromium, не оболочка с обещанием отсутствующего движка.

## Исходники и воспроизводимость

Официальный Stable Linux 155.0.8059.84, ревизия
5869fe313571d75152b77813cf0a4ee26a4e6df0, метаданные Chromium Dash от
2026-10-17. Это исходная точка, не заявление о поддержке CharmOS.
Версии закреплены в tools/chromium/upstream.json. Текущий источник — ручной
официальный архив владельца в игнорируемом build/chromium. fetch.sh оставлен
как прежний Git fallback; после импорта архива sync повторно не запускать.
Hooks отключены: установка пакетов хоста и запуск скачанных build hooks
не входят автоматически в синхронизацию. Официальный depot_tools bootstrap
подготавливает свой Python/CIPD внутри каталога инструментов/кеша.
Телеметрия depot_tools отключена через официальный DEPOT_TOOLS_COLLECT_METRICS=0.
Исходные лицензии Chromium и каждого third_party сохранять; до распространения
бинарника формировать notices из фактически включённых зависимостей.
MIT CharmOS не заменяет лицензий сторонних компонентов.

Ручной архив владельца: 4390800948 байт, наблюдённый SHA-256 закреплён в
upstream.json. Импорт проверяет размер/хеш, пути tar через data filter,
контроль целостности XZ и chrome/VERSION. Хеш вычислен локально; это не
опубликованный SHA-256 издателя. Google Storage передал CRC32C, он записан
как метаданные и не выдаётся за выполненную проверку SHA-256 издателя.

## Быстрый кеш исходников

На A: обычная распаковка дала 5000 записей за 127 с. На C: осталось около
4,5 ГБ, поэтому кеш не размещать в корневой файловой системе Ubuntu.
Внутри build/chromium создан отдельный ext4-образ linux-cache.ext4 (160 GiB)
на A:, доступный в WSL как linux-cache. После перехода 30000 записей
распакованы за 43 с. Это кеш сборки, не диск CharmOS и не системный диск ПК.

WSL-команды могут иметь отдельные mount namespaces: подключение и работа
должны выполняться в одном вызове. Для владельца UID/GID 1000/1000:
`wsl -d Ubuntu -u root -- bash -lc 'cd /mnt/a/CharmOS && bash tools/chromium/with-cache.sh 1000 1000 python3 tools/chromium/import_archive.py'`.
Helper подключает только собственный образ с проверкой пути/метки, затем
сбрасывает root до обычного пользователя. Повторное подключение использует
тот же loop device, чтобы не создавать два независимых устройства поверх
одного файла. Новые образы помечаются sparse до увеличения размера.
Исходники после импорта: linux-cache/release/chromium-155.0.8059.84;
в Windows их нельзя читать как обычную папку A:, использовать with-cache.

## Проверенные пробелы платформы

Архив полностью импортирован: 1342990 записей, проверка XZ и VERSION
успешны; отчёт build/chromium/archive-import.json. GN из архива запускается
(2562 cfcd774b98f3). Штатный BUILDCONFIG.gn не знает target_os="charmos";
нужен настоящий платформенный порт, одной смены флага недостаточно.
В рабочем дереве реализуется новый ABI времени/энтропии, до завершения
пакетных проверок строки ниже описывают проверенный исходный baseline.

| Слой | В CharmOS сейчас | Необходимая работа |
|---|---|---|
| Время | clock_gettime игнорирует clockid, time даёт uptime | Реальное UTC для сертификатов, независимые monotonic deadlines |
| Случайность | TPM GetRandom доступен только внутреннему коду ядра | Криптографический источник для пользователя; при отсутствии энтропии явная ошибка |
| TLS | SHA-512/Ed25519/AES для своих форматов | Порт TLS-компонента Chromium, trust store, проверка цепочки, имени и срока |
| Сеть | Свой TCP/UDP и socket ABI | DNS, URL/прокси, неблокирующее ожидание и адаптер сетевого процесса |
| Процессы | Spawn с явными capability, без fork | Адаптер запуска renderer/network/GPU и передачи IPC/VMO |
| Графика | libwm v2, два буфера, software Canvas | Ozone/Skia software backend, ввод и lifecycle окон; GPU пока не объявлять доступным |
| Runtime | Своя ограниченная libc, pthread/futex | Аудит libc++/base, файлов, потоков, TLS, сигналов, памяти и сборочного GN toolchain |
| Изоляция | Capability ядра | Профили дочерних процессов; не отключать sandbox ради демонстрации |

Новые syscall/IPC интерфейсы сначала описать новой версией docs/abi.
Linux driver shim M11 не предоставляет Linux userspace ABI; запуск готового
Linux Chrome не заменяет эти адаптеры.

## Крупные проверяемые пакеты

### Реализованный runtime для первого пакета

BootInfo 1.1 / syscall 1.12 описаны в docs/abi/time-random.md: UTC из UEFI,
аппаратная энтропия RDRAND/TPM, getrandom/getentropy. Общий runtime mutex TPM
защищает также разблокировку закрытой области, когда драйверы уже работают.
Новые libc rwlock/TSD описаны в docs/abi/libc-threads.md; ключи с поколениями
не раскрывают старое значение при повторном выделении, cleanup ограничен
четырьмя проходами. Четыре сценария tests/qemu/runtime проверяют это в ring 3.

tools/chromium/audit-runtime.py создаёт статический список из настоящего
BoringSSL. Он включает неактивные ветки других ОС, не доказывает сборку.
После новых pthread функций остаётся pthread_atfork из чужой платформенной
ветки; на Generic ELF используется консервативное повторное получение энтропии.
tools/chromium/probe-tls.sh проверяет реальную единицу трансляции с комплектной
libc++, своими C-заголовками и без заголовков Linux. Даже успешный syntax probe
не равен собранной TLS-библиотеке, проверке сертификатов или HTTPS-соединению.
Обе единицы crypto/rand/getentropy.cc и crypto/thread_pthread.cc проходят.
Исходный блокер wchar/mbstate_t/stdio устранён рабочими реализациями.
Логи build/chromium/tls-probe/*.log.
Порядок include исправлен: libc++ wrappers идут перед своими C-заголовками,
hardening FAST задан явно. Добавлены настоящие div/ldiv/lldiv и assert.
Полная сборка crypto+ssl и native memory-TLS smoke также прошли (ниже).

Дисковая зависимость браузера: исходный замер первичного шифрования 15 МиБ
в TCG — 156099 мс. После объединения соседних промахов/грязных страниц по
6 страниц в существующий block ABI — 22066 мс на таком же профиле store.
Замер включает чтение, AES-XTS и запись, не измеряет скорость отрисовки.
За проход: 1441 логических read-запросов, 663 write, 3887 страниц writeback.
AES также собирается с -O2; алгоритм/формат/границы fsync не менялись.
Большой файл 200 КиБ с неполным перекрытием страниц проверяется после
перезагрузки на SATA, virtio/NVMe и USB; штатные fsck и driver-restart тесты
сохранены. Актуальные итоговые результаты — в HANDOFF.md.

### Следующие результаты, которые ещё необходимо получить

Обновление Unicode/runtime 2026-10-17: собственные UTF-8/wchar функции,
потоковые implicit mbstate, Gregorian gmtime_r/gmtime, bsearch и PRI macros.
Mutex/rwlock статической libc v2 требуют общей пересборки; recursive mutex,
condition absolute timeout и sched_yield нужны bundled libc++. Оба реальных
BoringSSL translation units (getentropy.cc и thread_pthread.cc) проходят
tools/chromium/probe-tls.sh без Linux defines/headers.

tools/chromium/build-tls.sh конфигурирует upstream CMake для Generic ELF,
сборка идёт внутри ext4-кеша на A:. Оба архива собраны полностью. Используется
штатный memory BIO / OPENSSL_NO_SOCK; Chrome сам владеет сетевым транспортом.
Старый BSD setsockopt stub не используется. Лог tls-build.log.
build-tls-smoke.sh связывает native ELF с libc CharmOS, двумя исходными
libc++ units (algorithm, verbose_abort) и тремя compiler-rt units
(udivti3, umodti3, udivmodti4). Никаких host libc/libstdc++/libgcc.
Гостевой browser/tls прошёл SHA-256 known vector, RAND_bytes, TLS1.2/1.3
HTTP request/response через memory BIO; hostname/trust/expired/future
отказы проверены. browser/https также прошёл настоящий TCP через virtio-net
к контролируемому host TLS server: два HTTP-ответа (TLS1.3/1.2), четыре
отказа по сертификатам. Хост отдельно подтвердил версии TLS, SNI, запросы
и отсутствие HTTP при ошибках проверки. Это не окно браузера и не Яндекс.
Итог общей регрессии фиксируется в HANDOFF.md.
Полные debug ELF сохраняются внутри ext4-кеша, в initrd идут stripped-debug
копии: runtime_tls.elf 11490472 → 2247224 байт при сохранении кода/данных.

1. Native TLS/HTTPS с отрицательными проверками уже работает. Дальше:
   полноценный libc++/Chromium base, GN platform/toolchain, public trust
   roots и DNS. Локальный HTTPS smoke не доказывает работу публичных сайтов.
2. Процессы/IPC и software platform backend: настоящее окно Chromium в QEMU,
   локальные HTML/JS/CSS, ввод, прокрутка, перезапуск упавшего renderer.
3. Полная оболочка: Яндекс по умолчанию, адресная строка, вкладки, история,
   настройки, отдельный incognito без записи истории, расширения и PDFium.
   Реальные тестовые страницы, PDF, тестовое расширение и screenshots.
4. Нагрузка в виртуалке: память/CPU/latency на одинаковых страницах,
   ограниченные фоновые задачи и отсутствие постоянной перерисовки в покое.

Источники: [Chromium build](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md),
[Chromium Dash](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Linux&num=1).

### Нативная GN-платформа (2026-10-17)

tools/chromium/prepare-platform.py применяет пять проверяемых по SHA-256
изменений закреплённого дерева: platform dispatch GN/C++, путь compiler-rt,
no-exception конфигурации libc++/abi. Неизвестные vendor edits отклоняются;
оригиналы сохраняются в ext4-кеше. Повторная подготовка не трогает одинаковые
файлы. Версия wrapper/audit входит в имена инструментов GN, C headers входят
в depfiles, native libc.a — в явные зависимости link. Это сохраняет корректную
инкрементальную сборку без постоянной пересборки ОС.

Запуск: with-cache.sh 1000 1000 bash tools/chromium/build-platform.sh.
Обычный upstream .gn/BUILDCONFIG используется с root target build/charmos.
platform_smoke связывается для Generic ELF и копируется без debug sections
в build/user/user; browser/platform запускает именно этот ELF в QEMU.
Код проверяет flags, vector из 4096 элементов, sort/copy/unique_ptr, atomics
и сохранность UTF-8 bytes. Он намеренно включает только два upstream units,
как прежний TLS smoke, и не доказывает работу полной стандартной библиотеки.

--full-runtime дополнительно собирает штатные libc++/abi зависимости.
No-exception ABI units уже компилируются; исключения/RTTI приложений не
включаются ради обхода ошибок. Остались настоящие недостающие интерфейсы:
locale backend, math/fenv, числовые преобразования, далее filesystem/stdio.
Их нельзя подменять пустыми реализациями или Linux ABI.

В архиве clang 24.0.0git (86922d51225db089fffb99475eefc4904f79e3c8),
release stamp пустой. Компилятор регистрируется через clang_base_path как
custom; официальный ожидаемый stamp не подделывается. Актуальная версия,
GN args, hash и размер ELF сохраняются в build/chromium/platform-report.json.
Post-link audit отвергает compiler TLS/dynamic loader, пока loader их не
поддерживает. Отрицательные проверки — tools/chromium/test-elf-audit.sh.
