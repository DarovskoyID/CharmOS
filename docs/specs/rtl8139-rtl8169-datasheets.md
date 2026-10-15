# Realtek RTL8139 и RTL8169 — сводка по даташитам

По открытым источникам. Дата: 2026-10-15.

---

# Часть I. RTL8139D(L) — Single-Chip Fast Ethernet Controller

Даташит: Rev. 1.11, 2001/11/09.

## 1. Основные характеристики

- Однокристальный Fast Ethernet контроллер: **MAC + PHY + трансивер** в одном чипе.
- **100-pin QFP / LQFP**, техпроцесс 0.25 µ CMOS.
- 10 и 100 Мбит/с, N-way автонегоциация, half/full duplex, **IEEE 802.3x** flow control.
- Тактирование: кварц или генератор **25 МГц** (±50 ppm).
- Буферы: **два независимых FIFO по 2 КБ** — отдельно на приём и передачу.
- Питание: Vcc 3.0–3.6 В и Vdd25 2.3–2.7 В; I/O **5V-tolerant**; вывод VCTRL с внешним PNP-транзистором формирует 2.5 В.
- Loopback, 32-битный general-purpose таймер, Boot ROM до **128 КБ**, продвинутый режим энергосбережения.

## 2. Шина PCI

- **PCI Rev 2.2**, PCI clock 16.75–40 МГц.
- Bus master для передачи данных; target для доступа к регистрам через PCI memory или I/O space.
- Fast back-to-back транзакции; **multi-function** режим — арбитраж со вторым PCI master (REQB2 / GNTB2 / IDSEL2).
- Выводы AD разделяются с адресными и данными линиями Boot ROM.
- Поддержка **CLKRUN** (запрос запуска клока).

### PCI Configuration Space

| Смещение | Поле | Значение |
|---|---|---|
| 00h | Vendor ID | **10ECh** (Realtek) |
| 02h | Device ID | **8139h** |
| 08h | Revision ID | 10h (дублируется на операционном 005Eh) |
| 2Ch / 2Eh | SVID / SMID | из EEPROM |
| 34h | Capability pointer | 50h |
| 3Eh / 3Fh | MNGNT / MXLAT | по умолчанию 20h |
| 50h | PCI PM capability | — |
| 52h | PMC | PM capabilities |
| 54h/55h | PMCSR | PME status / PME enable |
| 60h | VPD capability | — |

## 3. Операционная карта регистров (ключевое)

| Смещение | Регистр | Назначение |
|---|---|---|
| 0000h–0005h | IDR0–5 | MAC-адрес станции, автозагрузка из EEPROM |
| 0008h–000Fh | MAR0–7 | 64-битный multicast hash filter (ставит драйвер) |
| 0010h–001Fh | TSD0–3 | статус и размер Tx-дескрипторов 0–3 |
| 0020h–002Fh | TSAD0–3 | адрес начала буфера Tx-дескрипторов 0–3 |
| 0030h–0033h | RBSTART | адрес начала Rx ring buffer |
| 0034h–0035h | ERBCR | Early Rx byte count |
| 0036h | ERSR | Early Rx status |
| 0037h | CR | Command: reset, RX/TX enable, buffer empty |
| 0038h–0039h | CAPR | Current Address of Packet Read — указатель чтения драйвера |
| 003Ah–003Bh | CBR | Current Buffer Address — байт в Rx-буфере |
| 003Ch–003Dh | IMR | Interrupt mask |
| 003Eh–003Fh | ISR | Interrupt status (чтение сбрасывает биты) |
| 0040h–0043h | TCR | Transmit configuration |
| 0044h–0047h | RCR | Receive configuration |
| 0048h–004Bh | TCTR | 32-битный счётчик таймера |
| 004Ch–004Fh | MPC | Missed Packet Counter (24 бита) |
| 0050h | 9346CR | EEPROM command / mode |
| 0051h | CONFIG0 | размер Boot ROM |
| 0052h | CONFIG1 | LED select, LWAKE mode, VPD, PM enable |
| 0054h–0057h | TimerInt | значение сравнения таймерного прерывания |
| 0058h | MSR | Media status: link, speed, flow control |
| 0059h | CONFIG3 | Magic Packet, LinkUp wake, fast back-to-back |
| 005Ah | CONFIG4 | формат wake-up frame, LWAKE pattern, analog power off |
| 0062h–0063h | BMCR | MII Basic Mode Control: reset, speed, auto-neg, duplex |
| 0064h–0065h | BMSR | MII Basic Mode Status: link, auto-neg complete, capabilities |
| 0066h–0067h | ANAR | Auto-Negotiation Advertisement |
| 0068h–0069h | ANLPAR | Auto-Negotiation Link Partner Ability |
| 006Ah–006Bh | ANER | Auto-Negotiation Expansion |
| 006Ch–0075h | DIS, FCSC, NWAYTR, REC, CSCR | счётчики disconnect / false carrier / RX_ER, N-way test, CS config |
| 0078h–0083h | PHY1_PARM, TW_PARM, PHY2_PARM | параметры PHY и twister |
| 0084h–00D3h | CRC0–7, Wakeup0–7, LSBCRC0–7 | CRC wake-up фреймов, 64-битные маски, LSB-маски |
| 00D8h | Config5 | доп. wake/power опции (не защищён 9346CR) |

### Важные битовые поля

- **CR (0037h):** RST (бит 4), RE — receiver enable (3), TE — transmitter enable (2), BUFE — buffer empty (0).
- **ISR / IMR (003Eh / 003Ch)**, одинаковая раскладка: ROK (0), RER (1), TOK (2), TER (3), RXOVW (4), PUN/LinkChg (5), FOVW (6), LenChg (13), TimeOut (14), SERR (15).
- **TCR (0040h):** IFG1–0 (interframe gap), LBK1–0 (loopback), CRC append (бит 16), MXDMA (max Tx DMA burst), TXRR (retry count), CLRABT (бит 0).
- **RCR (0044h):** ERTH (early Rx threshold), RBLEN (длина Rx-буфера, биты 12–11), RXFTH (Rx FIFO threshold), WRAP (бит 7), AER / AR (приём ошибочных и runt), биты режимов приёма — broadcast (AB), multicast (AM), physical match (APM), promiscuous (AAP).

## 4. Модель приёма — кольцевой буфер

1. Драйвер задаёт базу кольца в **RBSTART (0030h)**.
2. Длина кольца — **RCR биты 12–11**: 8 КБ / 16 КБ / 32 КБ / 64 КБ, каждая +16 байт.
3. Чип пишет пакет в кольцо, предваряя его **4-байтным Rx status header**. Биты статуса: ROK, CRC, FAE, RUNT, LONG, BAR, MAR, PAM.
4. Драйвер читает пакеты и продвигает **CAPR (0038h)**, освобождая место. **CBR (003Ah)** показывает, сколько байт ожидает обработки.
5. **WRAP (RCR бит 7)** определяет поведение пакета, пересекающего конец кольца: продолжать с начала или писать за концом буфера. Во втором режиме драйвер обязан зарезервировать не менее **1.5 КБ** дополнительного места.
6. Переполнения: **RXOVW** — переполнение кольца, **FOVW** — переполнение status FIFO. **MPC** считает отброшенные пакеты.
7. Early receive (ERSR / ERBCR) позволяет поднять прерывание до полного приёма пакета.

## 5. Модель передачи — 4 дескриптора

- Ровно **четыре Tx-дескриптора**: регистр статуса **TSD0–3** (0010h–001Fh) и регистр адреса **TSAD0–3** (0020h–002Fh) у каждого.
- Последовательность: драйвер пишет пакет в буфер по адресу из TSAD, затем записывает счётчик байт в TSD — это запускает передачу и одновременно сбрасывает read-only биты статуса.
- **OWN (бит 13)** — ставится чипом по завершении DMA этого дескриптора; драйвер сбрасывает его при старте передачи.
- **SIZE (биты 12–0)** — длина пакета. Пакеты длиннее **1792 байт (0700h)** блокируют очередь до установки OWN этого дескриптора.
- Биты статуса: TOK (15), TUN (14), NCC — счётчик коллизий (27–24), CDH (28), OWC (29), TABT (30), CRS (31).
- **ERTXTH (биты 21–16)** — early transmit threshold в единицах 32 байта.
- **TSAD (0060h–0061h)** — сводка битов OWN / TOK / TUN / TABT по всем четырём дескрипторам.

## 6. EEPROM (93C46)

- Устройство: **93C46**, 1 Кбит (64 × 16-битных слова).
- **Автозагрузка:** после RSTB или команды autoload чип читает адреса 00h–31h; занимает около **2 мс**. Загрузка происходит только если ID-слово в байтах 00h–01h равно **8129h**.
- Содержимое: subsystem IDs (06h–09h), MNGNT/MXLAT (0Ah–0Bh), Media/BMCR defaults (0Ch), Ethernet ID (0Eh–13h), дефолты CONFIG0/1/3/4/5, параметры PHY, checksum (32h–33h), VPD data (40h–7Fh).
- **Режимы доступа (9346CR, EEM1–0):** 00 — normal, 01 — autoload, 10 — программирование 93C46 (прямое управление выводами), **11 — разрешение записи config-регистров**. CONFIG0/1/3/4 и часть битов BMCR записываются только в режиме 11.
- Vendor ID и Device ID из EEPROM больше не загружаются.

## 7. Управление питанием

- **PMEB** — open-drain выход PME для сигнализации wake-событий системе.
- **LWAKE** — выход Wake-on-LAN, четыре режима (active high, active low, положительный импульс, отрицательный импульс); задаётся CONFIG1 LWACT и CONFIG4 LWPTN. По умолчанию active high.
- **ISOLATEB** — при активации чип перестаёт драйвить и сэмплировать шину PCI, кроме PME.
- **AUX** — детект вспомогательного питания; подтяжка к высокому через резистор разрешает wake из D3cold или APM power-down.
- **Wake-события:** Magic Packet (CONFIG3 бит 5), LinkUp (CONFIG3 бит 4), до восьми wake-up фреймов. Фреймы используют CRC-регистры 0084h–008Bh, 64-битные маски 008Ch–00C4h, LSB-маски 00CCh–00D3h. CONFIG4 бит 5 (LongWF) переключает в режим длинных wake-up фреймов.
- **Config5 (00D8h):** разрешения broadcast/multicast/unicast wake-up фреймов (биты 6–4), LANWake enable (бит 1), энергосбережение при link-down (бит 2), поведение PME status (бит 0).
- **Analog power-down:** автоматически при отсутствии линка и выключенном wake; вручную — CONFIG4 AnaOff (бит 6).

---

# Часть II. RTL8169 — Gigabit Ethernet Media Access Controller

Даташит: Rev. 1.21, 2002/03/27. Это **только MAC** — внешний PHY через GMII/MII или TBI. Интегрированные варианты: RTL8169S / RTL8110S (Rev. 1.3, 2003) и RTL8169SC / RTL8110SC.

## 1. Основные характеристики

- **208-pin QFP**, питание 3.3 В и 1.8 В, I/O **5V-tolerant**.
- 10/100/1000 Мбит/с через **MII/GMII**; 1000 Мбит/с через **TBI**. Автонегоциация.
- **PCI 2.2**: 32/64 бит, 33/66 МГц, поддержка **DAC** (dual address cycle), MRL / MRM, MWI.
- Внутренние FIFO: **8 КБ на передачу**, **48 КБ на приём**.
- **Дескрипторное** управление буферами, отдельная **высокоприоритетная Tx-очередь** для QoS/CoS.
- Offload: NDIS5 IP/TCP/UDP checksum, **large send offload**, IEEE **802.1Q VLAN** tagging.
- Tally counters (аппаратная статистика), full-duplex flow control (802.3x), цифровой и внешний аналоговый loopback.
- Wake-on-LAN: Magic Packet, LinkChg, wakeup frames; четыре варианта выхода LWAKE.
- Опциональный EEPROM (93C46 / 93C56) для ID, VPD, CIS. Опциональный Boot ROM до **128 КБ**.

## 2. Интерфейс GMII / TBI

- **GMII/MII:** шины данных TxD[7:0] и RxD[7:0] с управлением TxEN / RxDV / RxER. **GTxCLK** — тактовый сигнал передачи 125 МГц. Также CRS, COL и управление MDC/MDIO.
- **TBI:** 10-битные шины код-групп Tx[9:0], Rx[9:0]; RxCLK0 / RxCLK1 по 62.5 МГц каждый. TBILBK — индикация TBI loopback.
- **Клок и сброс:** CLOCK125 — вход от внешнего PHY или генератора. RSTPHYB сбрасывает внешний PHY.

## 3. Дескрипторный DMA

- Отдельные кольца: **нормального приоритета (TNPDS)** и **высокого приоритета (THPDS)** на передачу, и **Rx-кольцо (RDSAR)**.
- Адреса начала дескрипторов выровнены по **256 байт**.
- **TPPoll (0038h)** — Tx Priority Polling: драйвер сигнализирует чипу, что в очереди есть пакеты (биты HPQ, NPQ, FSWInt).
- Максимальные размеры DMA burst задаются в TCR и RCR; бит PCI multiple read/write в **C+CR** переопределяет их.

## 4. Карта регистров (ключевое)

| Смещение | Регистр | Назначение |
|---|---|---|
| 0000h | IDR0–5 | MAC-адрес станции, автозагрузка из EEPROM |
| 0008h | MAR0–7 | multicast hash filter |
| 0020h | TNPDS | начало Tx-кольца нормального приоритета (64 бита) |
| 0028h | THPDS | начало Tx-кольца высокого приоритета (64 бита) |
| 0037h | CR | Command: RST, RE (Rx enable), TE (Tx enable) |
| 0038h | TPPoll | Tx polling: HPQ, NPQ, FSWInt |
| 003Ch | IMR | Interrupt mask |
| 003Eh | ISR | Interrupt status (**запись 1 сбрасывает** — в отличие от 8139) |
| 0040h | TCR | Tx config: IFG, loopback, CRC append, MXDMA |
| 0044h | RCR | Rx config: RXFTH, AAP / APM / AM / AB, AER / AR |
| 0048h | TCTR | 32-битный general-purpose таймер |
| 0050h | 9346CR | EEPROM command / разрешение записи config |
| 0051h–0056h | CONFIG0–5 | размер Boot ROM, LED mode, LWAKE, управление wake |
| 0058h | TimerInt | значение таймерного прерывания |
| 005Ch | MULINT | выбор multiple (early) interrupt |
| 0060h | PHYAR | доступ к регистрам GMII/MII |
| 0064h | TBICSR | TBI control & status |
| 006Ch | PHYStatus | скорость линка, duplex, flow control, EnTBI |
| 0084h–00C3h | Wakeup0–4 | данные фильтра wakeup-фреймов |
| 00C4h–00CDh | CRC0–4 | 16-битные CRC для wakeup-фреймов |
| 00DAh | RMS | Rx Maximum packet Size (до 16K−1 байт) |
| 00E0h | C+CR | C+ Command: endian mode, RxVLAN, RxChkSum, DAC, MulRW |
| 00E4h | RDSAR | начало Rx-дескрипторов (64 бита, выравнивание 256 байт) |
| 00ECh | ETThR | Early Tx threshold (единицы по 32 байта) |

Примечание: регистра «CPCR» в даташите нет — C+ Command Register находится по **00E0h** и обозначается **C+CR**.

## 5. Jumbo frames

- **RMS (00DAh)** разрешает приём пакетов до **16K−1 байт**.
- В описании TCR отмечено, что подсчёт Tx underrun-discard применяется только к jumbo-фреймам.

## 6. Offload и VLAN

- **Checksum:** RxChkSum (C+CR бит 5) включает разгрузку контрольных сумм на приёме.
- **VLAN:** RxVLAN (C+CR бит 6) включает снятие VLAN-тега на приёме.
- **Large send и Tx checksum** заявлены в списке возможностей; их регистровые органы управления в разделе регистров напрямую не расписаны.

## 7. Смягчение прерываний (interrupt mitigation)

- **Early receive interrupts:** MULINT (005Ch) и MulERINT (RCR бит 24) поднимают Rx-прерывание после передачи заданного числа байт.
- **Timer interrupt:** TimerInt (0058h) вызывает установку бита TimeOut в ISR, когда TCTR достигает заданного значения.
- **Маскирование:** IMR разрешает/запрещает каждый источник ISR. INTAB — выход с открытым коллектором.

## 8. Управление питанием

- Поддержка ACPI и PCI Power Management; **PMEn** в CONFIG1 управляет регистрами PM capability.
- **Wake-события:** Magic Packet, LinkUp/LinkChg и до **пяти** wakeup-фреймов (unicast, broadcast, multicast).
- Сигналы: **PMEB** (open drain) и **LWAKE** с четырьмя режимами выхода (active high/low, положительный/отрицательный импульс).
- **Aux power:** чип определяет вспомогательное питание при включении и может пробуждаться из **D3cold**, если MA8 подтянут к aux-питанию.
- Продвинутый low-power режим применяется, когда не используются ни LAN, ни wakeup-функции.

---

# Сравнение двух архитектур

| | RTL8139D | RTL8169 |
|---|---|---|
| Скорость | 10/100 | 10/100/1000 |
| PHY | интегрирован | внешний (GMII/MII или TBI) |
| Корпус | 100-pin QFP/LQFP | 208-pin QFP |
| Шина | PCI 2.2, 32 бит | PCI 2.2, 32/64 бит, 33/66 МГц, DAC |
| FIFO | 2 КБ + 2 КБ | 8 КБ Tx + 48 КБ Rx |
| Модель Tx | **4 фиксированных дескриптора** (TSD/TSAD) | дескрипторные кольца, 2 приоритета |
| Модель Rx | единое кольцо (RBSTART + CAPR/CBR) | дескрипторное кольцо (RDSAR) |
| ISR | чтение сбрасывает | запись 1 сбрасывает |
| Offload | нет | checksum, LSO, VLAN |
| Jumbo | нет | до 16K−1 (RMS) |

Практическое следствие для написания драйвера: 8139 программируется «в лоб» — копирование в один из четырёх Tx-буферов и чтение из кольца со статус-заголовками; 8169 требует построения дескрипторных колец с выравниванием 256 байт, битов OWN/EOR и polling через TPPoll. OSDev Wiki имеет рабочие разборы обоих (`wiki.osdev.org/RTL8139`, `wiki.osdev.org/RTL8169`) — полезно для сверки с даташитом.

## Источники

### RTL8139
- [RTL8139D(L) Datasheet Rev. 1.11 (PDF)](https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139D_DataSheet.pdf)
- [RTL8139D — realtek.info (PDF)](http://realtek.info/pdf/rtl8139d.pdf)
- [RTL8139C spec (PDF, FreeBSD mirror)](https://people.freebsd.org/~wpaul/RealTek/spec-8139c(160).pdf)
- [RTL8139C+ spec (PDF, FreeBSD mirror)](https://people.freebsd.org/~wpaul/RealTek/spec-8139cp(150).pdf)
- [RTL8139B (PDF)](http://o3one.org/hwdocs/ethernet/realtek/8139b24.pdf)
- [OSDev Wiki — RTL8139](https://wiki.osdev.org/RTL8139)

### RTL8169
- [RTL8169 spec Rev. 1.21 (PDF, FreeBSD mirror)](https://people.freebsd.org/~wpaul/RealTek/rtl8169spec-121.pdf)
- [RTL8169 spec Rev. 1.21 (PDF, зеркало IITG)](https://www.iitg.ac.in/asahu/cs421/RealTek.pdf)
- [RTL8110S / RTL8169S Datasheet Rev. 1.3, 2003 (PDF, bitsavers)](http://www.bitsavers.org/components/realtek/RTL8139/RTL8110S_8169S_DataSheet_1.3_200309.pdf)
- [RTL8169S NIC Datasheet Rev. 1.4 (PDF)](http://realtek.info/pdf/rtl8169s.pdf)
- [RTL8169SC/8110SC Registers Rev. 1.0 (PDF)](https://people.freebsd.org/~wpaul/RealTek/RTL8169SC_8110SC_reg_1_0.pdf)
- [OSDev Wiki — RTL8169](https://wiki.osdev.org/RTL8169)
