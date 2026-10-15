# RTL8139: раскладка приёмного кольца (Rx ring)

Справка собрана из открытых источников, т.к. в даташите RTL8139D этого нет.
Источники перечислены в конце.

---

## 1. Структура кольца

Приёмный буфер — физически непрерывное кольцо. Данные идут:
сеть → on-chip Rx FIFO → (по достижении early-Rx threshold) → Rx buffer в RAM.

Перед каждым пакетом чип пишет 4-байтный заголовок:

> «The receive packet header (receive status and packet length) is written
> in front of the packet.» — *RTL8139 Programmer's Guide*

```
+0  +1  +2  +3   +4 ............................ +4+len
[ status ][ len ][ Ethernet frame (включая CRC)      ]
  16 бит   16 бит
```

Это один **little-endian dword**: младшие 16 бит — статус, старшие 16 бит — длина.

```c
rx_status = le32_to_cpu(*(__le32 *)(rx_ring + ring_offset));
rx_size   = rx_status >> 16;
pkt_size  = rx_size - 4;   /* Omit the four octet CRC from the length. */
```

### Биты статуса (совпадают с RSR)

| бит | имя   | смысл |
|----:|-------|-------|
|  0  | ROK   | Receive OK — пакет принят без ошибок |
|  1  | FAE   | Frame alignment error |
|  2  | CRC   | CRC error |
|  3  | LONG  | Пакет длиннее 4 КБ |
|  4  | RUNT  | Пакет короче 64 байт (media header + data + CRC) |
|  5  | ISE   | Invalid symbol error (только 100BASE-TX) |
| 6–12| —     | reserved |
| 13  | BAR   | Broadcast address received |
| 14  | PAM   | Physical address matched |
| 15  | MAR   | Multicast address received |

---

## 2. Входит ли CRC в длину

**Да.** В примере драйвера из Programmer's Guide `PacketLength` «include CRC»,
и при копировании берётся `PktLength - 4`. Linux делает то же:
`pkt_size = rx_size - 4`.

Т.е. полезная нагрузка = `len - 4`, а шаг по кольцу считается от полного `len`.

---

## 3. Выравнивание следующего пакета

Следующий заголовок всегда на **4-байтной (dword) границе**.

Programmer's Guide:

```c
next = (RxReadPtrOffset + PktLength + 4 + 3) & RX_READ_POINTER_MASK;
```

Linux `rtl8139_rx()`:

```c
cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;
```

- `+ rx_size` — тело пакета вместе с CRC
- `+ 4`       — сам заголовок
- `+ 3 & ~3`  — округление вверх до dword

Смещение в кольце берётся как `cur_rx % RX_BUF_LEN` (кольцо — степень двойки,
так что это просто маска).

---

## 4. CAPR — главная засада

**CAPR (0x38) смещён на 0x10.** Аппаратно читаемая позиция = `CAPR + 16`.

Поэтому:

```c
/* Linux: RxBufPtr == 0x38 */
RTL_W16(RxBufPtr, (u16)(cur_rx - 16));
```

Programmer's Guide: `outport(IOBase + CAPR, RxReadPtrOffset - 0x10);`
(комментарий там говорит «-4: avoid overflow», но код вычитает `0x10` —
верить надо коду, его же повторяют все рабочие драйверы).

Следствия:

1. **Инициализация:** `CAPR = 0xFFF0` (это −16 в 16 битах).
2. **Запас в буфере:** выделять нужно на **+16 байт** больше номинального размера
   (в Linux это `RX_BUF_PAD = 16`; OSDev рекомендует 8192 + 16).

### Соседние регистры

| адрес | имя | назначение |
|-------|-----|------------|
| 0x37  | CR   | бит 0 = **BUFE** (Rx buffer empty) |
| 0x38  | CAPR | read pointer драйвера, смещён на 0x10 |
| 0x3A  | CBR / CBA | write pointer чипа (current buffer address) |

---

## 5. Корректный цикл приёма

```c
while (!(RTL_R8(ChipCmd) & RxBufEmpty)) {        /* BUFE */
    u32 off  = cur_rx % RX_BUF_LEN;
    u32 st   = le32_to_cpu(*(__le32 *)(rx_ring + off));
    u16 size = st >> 16;

    /* пакет ещё дописывается (early receive в процессе) */
    if (size == 0xFFF0)
        break;

    /* санити-чек: иначе потеряешься в кольце навсегда */
    if (size < 8 || size > MAX_ETH_FRAME_SIZE + 4 || !(st & RxStatusOK)) {
        /* полный сброс Rx-движка, выход */
        rtl8139_rx_err(...);
        return;
    }

    deliver(&rx_ring[off + 4], size - 4);        /* без CRC */

    cur_rx = (cur_rx + size + 4 + 3) & ~3;
    RTL_W16(RxBufPtr, (u16)(cur_rx - 16));       /* CAPR с биасом 0x10 */
}
```

Важные моменты:

- **`size == 0xFFF0`** — чип ещё переливает пакет из FIFO. Не потреблять,
  выйти из цикла и подтвердить прерывание.
- **Пустота кольца** определяется по `BUFE` в CR (0x37, бит 0), а не сравнением
  `CAPR` с `CBR`.
- **Санити-чек обязателен.** Без него при сбое вы уедете по мусорному `size`
  и кольцо больше не восстановится; Linux в этом случае делает полный reset
  приёмника.
- **CAPR пишется после каждого пакета** (или хотя бы раз за цикл), иначе
  BUFE никогда не снимется.

---

## 6. Бит WRAP (RCR, бит 7)

| WRAP | поведение | что нужно |
|------|-----------|-----------|
| 0 | честное кольцо: хвост пакета переносится в начало буфера | split-copy при переходе через конец |
| 1 | чип дописывает остаток **сразу за концом буфера** | выделить +~1.5 КБ (1500/1536) запаса |

> «WRAP: when enabled, RTL8139(A/B) will move the rest of the packet data
> immediately after the buffer.» — *Programmer's Guide*

Итого для 8 КБ буфера:

- WRAP=0: `8192 + 16`
- WRAP=1: `8192 + 16 + 1536`

Linux в режиме `RX_BUF_IDX == 3` (64 КБ) вместо WRAP использует `wrap_copy()`,
т.к. запас за концом там невозможен.

Ещё из практики: **нельзя ставить Rx FIFO early threshold в NoEarly** —
прямое предупреждение из Programmer's Guide.

---

## 7. Чек-лист грабель

- [ ] CAPR инициализирован как `0xFFF0`, а не `0`
- [ ] При каждом обновлении CAPR вычитается `16`
- [ ] Буфер выделен с запасом `+16` (и `+1536`, если WRAP=1)
- [ ] `pkt_size = len - 4` (CRC отрезан), но шаг по кольцу от полного `len`
- [ ] Шаг по кольцу выровнен `(... + 3) & ~3`
- [ ] Проверка `len == 0xFFF0` перед обработкой
- [ ] Санити-чек `len` и `ROK` с полным сбросом Rx при провале
- [ ] Пустота кольца — по `BUFE`, не по `CAPR`/`CBR`
- [ ] Буфер физически непрерывный, выровнен, адрес в RBSTART — физический

---

## Источники

1. **RTL8139 Programmer's Guide v0.11 (Realtek, 1999)** — единственный
   официальный документ с раскладкой кольца; содержит пример драйвера.
   <https://bitsavers.mirrorservice.org/components/realtek/RTL8139/RTL8139_ProgrammersGuide_199901.pdf>
2. **Linux `drivers/net/ethernet/realtek/8139too.c`**, функция `rtl8139_rx()` —
   эталонная реализация.
   <https://googlers.googlesource.com/maze/linux/+/v3.1/drivers/net/8139too.c>
3. **HelenOS `uspace/drv/nic/rtl8139/defs.h`** — аккуратные определения битов
   заголовка и регистров.
   <https://www.helenos.org/export/6d8455d96fd6cd160666aef7ea55519828d14f42/mainline/uspace/drv/nic/rtl8139/defs.h>
4. **OSDev Wiki: RTL8139** — WRAP и размеры буфера.
   <https://wiki.osdev.org/RTL8139>
5. **realtek mailing list, «I can't understand the Rx packet header» (2001)** —
   исторический контекст по `rx_size = rx_status >> 16`.
   <https://beowulf.org/pipermail/realtek/2001-October/001140.html>
6. Для спорных моментов — **QEMU `hw/net/rtl8139.c`**: поведение железа там
   воспроизведено, включая `0xFFF0` и биас CAPR.
