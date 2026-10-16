# ABI: экран (syscall ABI v1.8)

**Статус: ЗАФИКСИРОВАН, v1.8 (2026-10-16).** Решения автора —
журнал `plans/os-gui-design.md` (2026-10-15): компоновщик — служба init в
юзерспейсе, фреймбуфер GOP получает только он; двойной буфер клиента; кадры по
сигналу «кадр показан», без VSync (у GOP его нет).

## 1. Хендл `Display`

- Объект ядра «экран»: линейный фреймбуфер, который передал загрузчик
  (boot.md, `Framebuffer`; UEFI 2.11 §12.9 GOP). Ядро кладёт его только в
  стартовое сообщение init с ролью **`Display` = 9** (process.md §2.1). init
  передаёт его единственной службе — компоновщику; другим процессам он не
  достаётся (как Power, power.md §1).
- Права: `Read` — `display_info`; `Map` — `display_map`; `Duplicate |
  Transfer` — как обычно.
- После того как Display отображён хотя бы раз, ядро больше не пишет на экран
  само (консоль ядра остаётся только на COM1).

## 2. Вызовы

| № | Имя | Аргументы | `RDX` при успехе |
|---|---|---|---|
| 65 | `display_info` | `display, DisplayInfo* out` | — |
| 66 | `display_map` | `display` | хендл `Vmo` фреймбуфера |

### 2.1. `DisplayInfo` (32 байта)

| Смещение | Поле | |
|---|---|---|
| 0 | `uint32 width` | пикселей |
| 4 | `uint32 height` | строк |
| 8 | `uint32 pitch` | байт на строку (≥ width × 4) |
| 12 | `uint32 format` | 1 — B,G,R,X по байтам (GOP BlueGreenRedReserved8BitPerColor); 2 — R,G,B,X (RedGreenBlueReserved8BitPerColor) |
| 16 | `uint64 size` | байт фреймбуфера (pitch × height, до страницы) |
| 24 | `uint64 reserved` | 0 |

- Другие форматы GOP (PixelBitMask, BltOnly) — `display_info` даёт
  `NotSupported`: экрана для компоновщика нет.

### 2.2. `display_map`

- VMO над физической памятью фреймбуфера, права `Read | Write | Map |
  Transfer` (без `Duplicate`: копий не плодим). Отображение — с типом памяти
  write-combining (SDM Vol. 3A §11.3, PAT), запись без чтения назад.
- Повторный вызов возвращает новый хендл на тот же VMO.

## 3. Новое в v1.8 (вместе с input.md)

| Что | Значение |
|---|---|
| вызовы `display_info`, `display_map` | 65, 66 |
| роль хендла `Display` | 9 |
| `abi_version` | 1.8 |
