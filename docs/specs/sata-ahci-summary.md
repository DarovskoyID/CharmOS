# SATA/AHCI — сводка по открытым источникам

**Статус документа:** вторичная сводка. Основной текст Serial ATA Revision 2.6/3.x
(SATA-IO) недоступен: раздача только членам организации, как и у PCI-SIG.
Каждый пункт ниже помечен уровнем источника; каждый факт, влияющий на код,
должен быть подтверждён тестом в QEMU до того, как на него опираются.

Легенда уровней источника:

- `P` — **первичный открытый**: AHCI 1.3.1 (Intel, публикуется свободно),
  T13 ATA/ATAPI-8 / ACS рабочие черновики (публичные), SATA 1.0a.
- `S` — **вторичный**: OSDev wiki, исходники Linux `drivers/ata/libahci.c`,
  `include/linux/ahci.h`, исходники QEMU `hw/ide/ahci.[ch]`.
- `S!` — **вторичный, не проверяемый открыто**: относится к link/PHY-уровню,
  описан только в закрытом SATA Revision. Опираться нельзя — только для
  понимания контекста.

Конвенция пометок в коде (как в PCI-части):

```cpp
// SRC: S (osdev+libahci), VERIFY: tests/qemu/ahci_port_init.cpp
// SRC: P (AHCI 1.3.1 §3.1.2)
// SRC: S! link layer, НЕ полагаться — поведение проверено только в QEMU
```

---

## 1. Что именно закрыто и нужно ли это драйверу

Закрытый документ Serial ATA Revision 2.6/3.x — это преимущественно
**физический и канальный уровни**: OOB-сигнализация (COMRESET/COMINIT/COMWAKE),
8b/10b-кодирование, примитивы (SYNC, ALIGN, X_RDY/R_RDY, HOLD/HOLDA, WTRM,
EOF/SOF), скремблирование, скорости Gen1/2/3, спецификация разъёмов,
Port Multiplier и Port Selector в деталях, DEVSLP-тайминги.

**Драйвер под AHCI-контроллер ничего из этого не программирует напрямую.**
Весь этот уровень реализован в HBA аппаратно. Программная модель — это:

| Слой | Чем описан | Доступность |
|---|---|---|
| Регистры контроллера, DMA-структуры, FIS-обмен | AHCI 1.3.1 | открыто (`P`) |
| Набор команд устройства (IDENTIFY, READ/WRITE DMA EXT, FLUSH, NCQ) | T13 ATA/ATAPI-8, ACS-2/3/4 | открыто (`P`) |
| ATAPI-команды (CD/DVD) | MMC-6 / SPC | открыто (`P`) |
| Link/PHY | SATA Revision 2.6+ | закрыто (`S!`), драйверу не нужно |

Вывод: отсутствие SATA Revision **не блокирует** разработку. Блокирует оно
только точное описание тайминга сброса линка и семантики отдельных бит
`PxSERR` — и именно эти места надо помечать `S!` и покрывать тестом.

Исключения, где закрытый текст реально был бы полезен:

- точные значения таймаутов COMRESET/негоциации скорости (в QEMU они
  вырождены, на железе — нет);
- порядок восстановления после `PxSERR.DIAG.*` (CRC, Handshake, Link Sequence);
- FIS-based switching за Port Multiplier;
- DEVSLP/Partial/Slumber тайминги для энергосбережения.

Всё это — поздние этапы. На первом этапе их можно не реализовывать и честно
вернуть «не поддерживается».

---

## 2. Где брать открытые тексты

- **AHCI 1.3.1** — Intel публикует страницу спецификации:
  <https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html>.
  Зеркала PDF лежат в учебных курсах, например
  <https://courses.cs.washington.edu/courses/cse451/21sp/readings/ahci.pdf>.
  Это **первичный** документ для всей программной модели.
- **T13** — рабочие черновики ATA/ATAPI-8 и ACS: <https://t13.org/project-working-drafts>.
  Зеркало ATA/ATAPI-8: <https://read.seas.harvard.edu/cs161/2019/pdf/ata-atapi-8.pdf>.
- **SATA 1.0a** — исторически распространялся свободно, описывает FIS-уровень
  и базовые примитивы; для понимания канального уровня этого обычно хватает.
- **Исходники как исполняемая истина:** `qemu/hw/ide/ahci.c` — это ровно та
  модель, против которой пойдут тесты; `linux/drivers/ata/libahci.c` — рабочие
  обходы реальных багов контроллеров.

> В этой песочнице скачивание бинарных файлов заблокировано прокси, поэтому
> PDF-копии положить в репозиторий не удалось — ссылки выше открываются
> напрямую, качать нужно с машины.

---

## 3. Регистры HBA (`P`, AHCI 1.3.1 §3)

Правила доступа: регистры некэшируемые, locked-доступ не поддерживается,
максимальный размер обращения — 64 бита без пересечения 8-байтовой границы.
BAR5 (ABAR) в PCI-заголовке указывает на это окно.

### 3.1 Generic Host Control (`P`)

| Смещение | Имя | Назначение |
|---|---|---|
| 00h | CAP | возможности HBA |
| 04h | GHC | глобальное управление |
| 08h | IS | статус прерываний (битовая карта портов) |
| 0Ch | PI | реализованные порты (битовая карта) |
| 10h | VS | версия AHCI |
| 14h | CCC_CTL | coalescing: управление |
| 18h | CCC_PORTS | coalescing: порты |
| 1Ch | EM_LOC | enclosure management: расположение |
| 20h | EM_CTL | enclosure management: управление |
| 24h | CAP2 | расширенные возможности |
| 28h | BOHC | BIOS/OS handoff |

**CAP (00h)** (`P`):

| Биты | Поле | Значение |
|---|---|---|
| 31 | S64A | поддержка 64-битной адресации |
| 30 | SNCQ | поддержка NCQ |
| 29 | SSNTF | есть `PxSNTF` |
| 28 | SMPS | механические датчики наличия |
| 27 | SSS | staggered spin-up (HwInit, ставит BIOS) |
| 26 | SALP | агрессивное управление питанием линка |
| 25 | SAL | один activity-LED |
| 24 | SCLO | поддержка `PxCMD.CLO` |
| 23:20 | ISS | макс. скорость: 1=Gen1 1.5G, 2=Gen2 3G, 3=Gen3 6G |
| 18 | SAM | только AHCI, legacy task-file не реализован |
| 17 | SPM | Port Multiplier (command-based switching) |
| 16 | FBSS | FIS-based switching (только если SPM=1) |
| 15 | PMD | multiple DRQ block PIO (в AHCI ≥1.2 всегда 1) |
| 14 | SSC | поддержка Slumber |
| 13 | PSC | поддержка Partial |
| 12:08 | NCS | число слотов команд минус 1 (т.е. 1..32) |
| 7 | CCCS | command completion coalescing |
| 6 | EMS | enclosure management |
| 5 | SXS | внешний signal-only разъём |
| 4:00 | NP | число портов минус 1 (т.е. 1..32) |

**GHC (04h)** (`P`):

- бит 31 `AE` — AHCI Enable. Если `CAP.SAM=0` — RW, сброс в 0; если `SAM=1` —
  RO и всегда 1. **Ставить до обращения к остальным AHCI-регистрам.**
  Сброс `AE` возможен только записью `00000000h` во весь GHC.
- бит 2 `MRSM` — RO, MSI revert to single message.
- бит 1 `IE` — Interrupt Enable; при 0 все источники прерываний портов выключены.
- бит 0 `HR` — HBA Reset, write-1-to-set, самоочищается по завершении.
  Сброс возвращает автоматы в idle и переинициализирует порты через COMRESET —
  **кроме** случая `CAP.SSS=1`, когда раскрутку каждого порта делает ПО.

**PI (0Ch)** (`P`): битовая карта; число установленных бит ≤ `CAP.NP+1`,
минимум один бит установлен. Нельзя выводить число портов из `CAP.NP` —
итерироваться надо по `PI`.

### 3.2 Регистры порта (`P`)

База порта `n`: `ABAR + 100h + n*80h`, где `n` — позиция бита в `PI`.

| Смещение | Имя | Назначение |
|---|---|---|
| 00h | PxCLB | база списка команд, выравнивание 1 КиБ (биты 9:0 — RO 0) |
| 04h | PxCLBU | старшие 32 бита (RO 0 без `CAP.S64A`) |
| 08h | PxFB | база области принятых FIS, выравнивание 256 Б (4 КиБ при FBS) |
| 0Ch | PxFBU | старшие 32 бита |
| 10h | PxIS | статус прерываний порта (W1C) |
| 14h | PxIE | разрешение прерываний порта |
| 18h | PxCMD | команда и статус |
| 20h | PxTFD | task file data (STS/ERR) |
| 24h | PxSIG | сигнатура устройства |
| 28h | PxSSTS | SCR0: SStatus |
| 2Ch | PxSCTL | SCR2: SControl |
| 30h | PxSERR | SCR1: SError (W1C) |
| 34h | PxSACT | SCR3: SActive (для NCQ) |
| 38h | PxCI | Command Issue |
| 3Ch | PxSNTF | SCR4: SNotification |
| 40h | PxFBS | FIS-based switching |
| 44h | PxDEVSLP | Device Sleep |
| 70h..7Fh | PxVS | vendor specific |

**PxCMD (18h)** — биты, на которые опирается код (`S`, сверено с `libahci.h`
и `qemu/hw/ide/ahci.h`; номера бит обязательно проверить тестом):

| Бит | Имя | Смысл |
|---|---|---|
| 0 | ST | Start — HBA может обрабатывать список команд |
| 1 | SUD | Spin-Up Device (имеет смысл при `CAP.SSS=1`) |
| 2 | POD | Power On Device |
| 3 | CLO | Command List Override (сброс BSY/DRQ, нужен `CAP.SCLO`) |
| 4 | FRE | FIS Receive Enable |
| 7:8 | CCS | текущий слот команды (RO) |
| 14 | FR | FIS Receive Running (RO) |
| 15 | CR | Command List Running (RO) |
| 16 | CPS | Cold Presence State |
| 17 | PMA | Port Multiplier Attached |
| 18 | HPCP | Hot Plug Capable Port |
| 19 | MPSP | Mechanical Presence Switch |
| 20 | CPD | Cold Presence Detection |
| 21 | ESP | External SATA Port |
| 24 | ATAPI | Device is ATAPI |
| 26 | ALPE | Aggressive Link Power Management Enable |
| 27 | ASP | Aggressive Slumber/Partial |
| 31:28 | ICC | Interface Communication Control |

**PxSSTS (28h)** (`P`/`S`):

- `DET` (3:0): 0 — нет устройства и нет связи; 1 — устройство есть, связи нет;
  **3 — устройство есть, связь установлена**; 4 — PHY offline/BIST.
- `SPD` (7:4): 0 — связи нет; 1/2/3 — Gen1/Gen2/Gen3.
- `IPM` (11:8): 0 — нет устройства/связи; **1 — active**; 2 — Partial;
  6 — Slumber; 8 — DevSleep.

Критерий «устройство присутствует и готово»: `DET == 3 && IPM == 1` (`S`).

**PxSIG (24h)** — сигнатуры (`S`):

| Значение | Устройство |
|---|---|
| `0x00000101` | SATA-диск |
| `0xEB140101` | ATAPI |
| `0xC33C0101` | SEMB (enclosure management bridge) |
| `0x96690101` | Port Multiplier |

Сигнатуры на реальных контроллерах врут. Окончательная классификация — по
данным IDENTIFY / IDENTIFY PACKET DEVICE.

**PxTFD (20h)**: биты 7:0 — ATA Status (бит 7 BSY, бит 3 DRQ, бит 0 ERR),
биты 15:8 — ATA Error. Ждать очистки `BSY|DRQ` перед выдачей команды (`S`).

**PxIS / PxIE** — ключевые биты (`S`): `DHRS` (0, D2H Register FIS),
`PSS` (1, PIO Setup), `DSS` (2, DMA Setup), `SDBS` (3, Set Device Bits —
завершение NCQ), `DPS` (5), `PCS` (6, port connect change),
`PRCS` (22, PhyRdy change), `HBFS` (29, host bus fatal),
`HBDS` (28, host bus data error), `IFS` (27, interface fatal),
`INFS` (26, interface non-fatal), `TFES` (30, task file error).
`TFES|HBFS|HBDS|IFS` — фатальные, требуют рестарта порта.

**PxSERR (30h)** — `ERR` в 15:0, `DIAG` в 31:16. Подробная семантика
`DIAG.{N,I,W,B,D,C,H,S,T,F,X}` описана только в закрытом SATA Revision —
помечать `S!`, в коде ограничиться «фатально / не фатально» и сбросом W1C.

---

## 4. Структуры в памяти (`P` §4, детали `S`)

### 4.1 Command List

Массив из `CAP.NCS+1` (до 32) Command Header по 32 байта, выравнивание 1 КиБ,
адрес в `PxCLB/PxCLBU`.

Command Header:

| Смещение | Поле |
|---|---|
| DW0 биты 4:0 | `CFL` — длина Command FIS в DWORD (2..16) |
| DW0 бит 5 | `A` — ATAPI |
| DW0 бит 6 | `W` — запись (host → device) |
| DW0 бит 7 | `P` — prefetchable |
| DW0 бит 8 | `R` — reset |
| DW0 бит 9 | `B` — BIST |
| DW0 бит 10 | `C` — clear busy upon R_OK |
| DW0 биты 15:12 | `PMP` — порт Port Multiplier |
| DW0 биты 31:16 | `PRDTL` — число элементов PRDT |
| DW1 | `PRDBC` — байт передано (пишет HBA; обнулять перед выдачей) |
| DW2 | `CTBA` — база Command Table, выравнивание 128 Б |
| DW3 | `CTBAU` — старшие 32 бита |
| DW4..DW7 | reserved, обнулять |

### 4.2 Command Table

| Смещение | Содержимое |
|---|---|
| 00h..3Fh | Command FIS (до 64 байт) |
| 40h..4Fh | ATAPI-команда (12 или 16 байт) |
| 50h..7Fh | reserved |
| 80h.. | PRDT, 0..65535 элементов по 16 байт |

PRD-элемент:

| Смещение | Поле |
|---|---|
| 00h | `DBA` — адрес буфера, выравнивание 2 байта (бит 0 = 0) |
| 04h | `DBAU` — старшие 32 бита |
| 08h | reserved |
| 0Ch биты 21:0 | `DBC` — размер минус 1, максимум 4 МиБ (`0x3FFFFF`) |
| 0Ch бит 31 | `I` — прерывание по завершении этого элемента |

Длина каждого элемента должна быть кратна 2 байтам.

### 4.3 Received FIS (`PxFB`, 256 байт, выравнивание 256 Б)

| Смещение | Содержимое |
|---|---|
| 00h | DSFIS — DMA Setup FIS |
| 20h | PSFIS — PIO Setup FIS |
| 40h | RFIS — D2H Register FIS |
| 58h | SDBFIS — Set Device Bits FIS |
| 60h | UFIS — Unknown FIS |
| A0h..FFh | reserved |

---

## 5. FIS (`P`/`S`)

| Тип | Значение | Направление |
|---|---|---|
| Register H2D | `0x27` | host → device |
| Register D2H | `0x34` | device → host |
| DMA Activate | `0x39` | device → host |
| DMA Setup | `0x41` | двунаправленный |
| Data | `0x46` | двунаправленный |
| BIST Activate | `0x58` | двунаправленный |
| PIO Setup | `0x5F` | device → host |
| Set Device Bits | `0xA1` | device → host |

Register H2D (20 байт, 5 DWORD):

```
DW0: [7:0]=0x27  [8..]=pmport(4) rsv(3) C(1)  [23:16]=command  [31:24]=featurel
DW1: [7:0]=lba0  [15:8]=lba1  [23:16]=lba2  [31:24]=device
DW2: [7:0]=lba3  [15:8]=lba4  [23:16]=lba5  [31:24]=featureh
DW3: [7:0]=countl [15:8]=counth [23:16]=icc [31:24]=control
DW4: reserved
```

Бит `C` в DW0 = 1 означает «это команда», а не обновление control-регистра.

---

## 6. Последовательности

### 6.1 Инициализация HBA (`P` §10.1.2, порядок — `S`)

1. BIOS/OS handoff при `CAP2.BOH`: выставить `BOHC.OOS`, дождаться
   `BOHC.BOS == 0` (таймаут 25 мс, затем ещё 2 с на `BOHC.BB`).
2. Выставить `GHC.AE = 1` (если `CAP.SAM = 0`).
3. `GHC.HR = 1`, ждать самоочистки (таймаут 1 с). Снова `GHC.AE = 1`.
4. Прочитать `CAP`, `CAP2`, `PI`, `VS`.
5. Для каждого бита в `PI` — инициализация порта (ниже).
6. Очистить `IS` (W1C), затем `GHC.IE = 1`.

### 6.2 Инициализация порта (`P` §10.1.2 / §10.4.1, детали `S`)

1. **Остановить порт:** `PxCMD.ST = 0`, дождаться `PxCMD.CR == 0` (500 мс);
   `PxCMD.FRE = 0`, дождаться `PxCMD.FR == 0` (500 мс).
   Менять `PxCLB`/`PxFB` можно только при остановленном порте.
2. Выделить и обнулить command list (1 КиБ) и received FIS (256 Б),
   записать адреса в `PxCLB(U)`/`PxFB(U)`. При `CAP.S64A == 0` буферы
   обязаны быть ниже 4 ГиБ.
3. Если `CAP.SSS = 1`: `PxCMD.SUD = 1`, ждать `PxSSTS.DET == 3` (таймаут 1 с).
4. Очистить `PxSERR` (запись прочитанного значения обратно, W1C).
5. Проверить наличие устройства: `PxSSTS.DET == 3 && PxSSTS.IPM == 1`.
   Иначе порт пропустить.
6. Дождаться `PxTFD.STS.{BSY,DRQ} == 0` (таймаут 30 с для раскрутки шпинделя;
   при зависании — `PxCMD.CLO` при `CAP.SCLO`, иначе port reset).
7. **Запустить порт:** `PxCMD.FRE = 1`, затем `PxCMD.ST = 1`
   (строго в этом порядке; `ST` разрешено ставить только при `FRE = 1`).
8. Прочитать `PxSIG`, классифицировать устройство, выставить `PxCMD.ATAPI`
   при необходимости.
9. Очистить `PxIS`, настроить `PxIE`.

### 6.3 Выдача команды (`S`)

1. Очистить `PxIS` (W1C).
2. Найти свободный слот: первый бит, свободный и в `PxSACT`, и в `PxCI`,
   среди `CAP.NCS+1`.
3. Заполнить Command Header: `CFL = 5` (длина H2D FIS в DWORD), `W` по
   направлению, `PRDTL`, `PRDBC = 0`, `CTBA` на обнулённую Command Table.
4. Заполнить PRDT (≤ 4 МиБ на элемент, выравнивание 2 байта).
5. Собрать Register H2D FIS: `fis_type = 0x27`, `C = 1`, `command`,
   LBA48 в `lba0..lba5`, `device = 0x40` (LBA-режим),
   `countl/counth` — число секторов.
6. Дождаться `PxTFD.{BSY,DRQ} == 0`.
7. Для NCQ: сначала `PxSACT |= 1<<slot`, затем `PxCI |= 1<<slot`.
   Для обычных команд — только `PxCI`.
8. Ждать очистки бита в `PxCI` (или прерывания `DHRS`/`SDBS`).
   Параллельно проверять `PxIS.TFES` — при нём читать `PxTFD` и
   `RFIS` из received-FIS области.

### 6.4 Восстановление после фатальной ошибки (`S`, частично `S!`)

При `PxIS.{TFES,HBFS,HBDS,IFS}`:

1. `PxCMD.ST = 0`, ждать `CR == 0`.
2. Прочитать `PxTFD`, `PxSERR`, `RFIS`; очистить `PxSERR` и `PxIS`.
3. Если `PxTFD.{BSY,DRQ}` всё ещё стоят — port reset (COMRESET через
   `PxSCTL.DET = 1`, подождать ≥1 мс, затем `PxSCTL.DET = 0`,
   ждать `PxSSTS.DET == 3`).
4. `PxCMD.ST = 1`, перевыдать незавершённые команды.

Тайминги COMRESET — закрытая часть (`S!`); 1 мс взято из `libahci`
и в QEMU не проверяется по времени.

### 6.5 Минимальный набор команд устройства (`P`, T13)

| Команда | Код | Назначение |
|---|---|---|
| IDENTIFY DEVICE | `0xEC` | геометрия, поддержка LBA48/NCQ, размеры секторов |
| IDENTIFY PACKET DEVICE | `0xA1` | то же для ATAPI |
| READ DMA EXT | `0x25` | чтение LBA48 |
| WRITE DMA EXT | `0x35` | запись LBA48 |
| FLUSH CACHE EXT | `0xEA` | сброс кэша устройства |
| READ FPDMA QUEUED | `0x60` | NCQ-чтение |
| WRITE FPDMA QUEUED | `0x61` | NCQ-запись |
| PACKET | `0xA0` | передача ATAPI-пакета |
| SET FEATURES | `0xEF` | вкл./выкл. write cache и т.п. |

В IDENTIFY: слово 83 бит 10 — поддержка LBA48; слова 100..103 — ёмкость LBA48;
слово 76 — поддержка SATA/NCQ; слова 117..118 — логический размер сектора.

---

## 7. План проверки в QEMU

Запуск: `-device ich9-ahci,id=ahci -drive id=d0,file=disk.img,if=none
-device ide-hd,drive=d0,bus=ahci.0`. Модель QEMU — `hw/ide/ahci.c`,
читать её при расхождении.

Каждый пункт сводки выше должен быть привязан к тесту:

| № | Что проверяется | Критерий успеха |
|---|---|---|
| T1 | ABAR из BAR5, `CAP`/`VS` читаются | `VS` = 0x10300 для ich9-ahci |
| T2 | `GHC.AE` ставится, `GHC.HR` самоочищается | `HR` → 0 за < 1 с |
| T3 | `PI` согласован с `CAP.NP` | popcount(`PI`) ≤ `NP+1` |
| T4 | Карта смещений портов `100h + n*80h` | `PxCLB` read/write проходит |
| T5 | Выравнивания `PxCLB` 1 КиБ, `PxFB` 256 Б | младшие биты читаются как 0 |
| T6 | `PxSSTS` при подключённом диске | `DET=3`, `IPM=1`, `SPD` ≠ 0 |
| T7 | `PxSIG` для `ide-hd` и `ide-cd` | `0x00000101` / `0xEB140101` |
| T8 | Порядок `FRE` → `ST`, флаги `FR`/`CR` | `CR`=1 только после `ST`=1 |
| T9 | IDENTIFY DEVICE через PIO Setup | `PSFIS` заполнен, 512 байт данных, модель диска совпадает с `-drive` |
| T10 | READ DMA EXT, 1 сектор | `PRDBC` = 512, данные совпадают с образом |
| T11 | READ DMA EXT, > 4 МиБ (несколько PRDT) | все PRD закрыты, `PRDBC` = сумме |
| T12 | WRITE DMA EXT + FLUSH, перечитать | побайтовое совпадение |
| T13 | Бит `I` в PRD даёт прерывание | `PxIS.DPS`/`DHRS` выставлен |
| T14 | Ошибка команды (чтение за пределом LBA) | `PxIS.TFES`, `PxTFD.ERR`, `RFIS` корректен |
| T15 | Восстановление порта после T14 | следующая команда выполняется успешно |
| T16 | NCQ: 4 команды в `PxSACT`/`PxCI` одновременно | завершение через `SDBS`, все биты сняты |
| T17 | Port reset через `PxSCTL.DET` | `DET` снова 3, устройство живо |
| T18 | 32-битный режим при `CAP.S64A=0` | буферы < 4 ГиБ, `PxCLBU` = 0 |

Приоритет: T1–T12 — до того, как писать что-либо поверх блочного уровня.
T13–T18 — перед объявлением драйвера рабочим.

Отдельно: **ни один из тестов не проверяет link/PHY-уровень**. Пункты,
помеченные `S!`, остаются неподтверждёнными до проверки на реальном железе —
это надо держать в известных ограничениях драйвера.

---

## 8. Известные расхождения QEMU и железа (`S`)

- QEMU не моделирует задержки COMRESET/негоциации скорости — таймауты в
  драйвере тестами не покрываются и на железе могут оказаться малы.
- `CAP.SSS` в QEMU обычно 0, путь staggered spin-up остаётся непроверенным.
- Port Multiplier и FIS-based switching в базовой модели отсутствуют.
- Энергосостояния Partial/Slumber/DevSleep не моделируются содержательно.
- Реальные контроллеры требуют обходов (см. `ahci_broken_*` в `libahci`),
  которых в QEMU нет.

---

## Источники

- [Serial ATA AHCI 1.3.1 Specification — Intel](https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html)
- [AHCI 1.3.1 PDF (зеркало, CSE451 UW)](https://courses.cs.washington.edu/courses/cse451/21sp/readings/ahci.pdf)
- [T13 — Project Working Drafts](https://t13.org/project-working-drafts)
- [ATA/ATAPI-8 draft (зеркало, Harvard CS161)](https://read.seas.harvard.edu/cs161/2019/pdf/ata-atapi-8.pdf)
- [OSDev Wiki — AHCI](https://wiki.osdev.org/AHCI)
- [SATA-IO — Archived Specifications (раздача только членам)](https://sata-io.org/archived-specifications)
