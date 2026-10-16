# PS/2-мышь: откуда брать протокол (3-байтный пакет и команды)

## Краткий ответ

Первичного IBM-документа, который описывал бы протокол мыши так же, как
IBM PS/2 Technical Reference описывает 8042 и клавиатуру, **в открытом доступе
фактически нет**. Проверено: в *Personal System/2 Hardware Interface Technical
Reference* (May 1988) раздела про mouse / pointing device нет вообще — там только
Micro Channel, видео и клавиатуры 101/102. В каталогах IBM Technical Reference
отдельная "Mouse Technical Reference" тоже не числится.

Что реально есть у IBM:
- **Model-specific Technical Reference** (Model 50/60/70/80 и т.д., bitsavers /
  ardent-tool): там есть раздел *Pointing Device* — распиновка разъёма, уровни,
  тайминги последовательной линии, иногда список команд. Но формат пакета
  описан скупо.
- **Options and Adapters Technical Reference** — мышь как опция; смотреть там же.
- Это то максимум, что есть от IBM. Протокол мыши IBM опубликовала как
  *поведение устройства*, а не как спецификацию.

Поэтому де-факто спецификацией считаются документы ниже. Для реализации драйвера
их достаточно и они взаимно согласованы.

## Что брать как основу

1. **Synaptics TouchPad Interfacing Guide** (публичный PDF, 510-000080-*) —
   самое близкое к нормальной спецификации. В главе про PS/2 есть:
   формат 3-байтного пакета стандартной мыши, полный список команд
   `0xE6..0xFF`, значения mode byte, поведение в stream/remote/wrap режимах,
   ответы ACK/0xFA, Read Device Type, последовательности сброса.
   Синаптикс обязан был описать стандартный режим, т.к. тачпад его эмулирует.

2. **Adam Chapweske, "The PS/2 Mouse Interface"** — самый цитируемый документ,
   на него ссылаются практически все реализации. Таблица команд, формат пакета,
   разрешение/масштабирование, Intellimouse-расширения, тайминги линии
   clock/data. Зеркала: utcluj.ro, tu-dortmund.de.
   Парный документ того же автора — "The PS/2 Keyboard Interface".

3. **Andries Brouwer, "Keyboard scancodes", гл. 11–13**
   (`aeb.win.tue.nl/linux/kbd/scancodes-13.html`) — главы про aux-устройство
   и мышь: команды, протоколы Microsoft/Logitech/Intellimouse, wheel, 5 кнопок,
   как разные мыши отвечают на Read Device Type. Очень аккуратно собрано,
   с указанием расхождений между вендорами.

4. **Исходники ядра Linux**, `drivers/input/mouse/` —
   `psmouse-base.c` (протокол, детект, sliced commands),
   `logips2pp.c` (расширения Logitech), `synaptics.c`, `alps.c`.
   Это единственный источник, где честно описаны реальные отклонения
   конкретных мышей от «спеки». Для вопросов «а что делает настоящее железо» —
   смотреть сюда.

5. **OSDev Wiki, "PS/2 Mouse" и "8042 PS/2 Controller"** — вторичный, но
   корректный и удобный для быстрой сверки: порядок инициализации через 8042
   (0xD4 для пересылки байта в aux-порт, включение IRQ12), 3- и 4-байтные
   пакеты, overflow-биты.

6. **Microsoft, документация по IntelliMouse / wheel** — откуда взялась
   последовательность «sample rate 200, 100, 80» для перехода в 4-байтный
   режим и чем отличается Explorer (5 кнопок).

7. **Application notes вендоров MCU** (Cypress, Microchip, Atmel) — реализации
   PS/2 device side: тайминги, host-to-device передача, timeout'ы.
   Полезны для проверки таймингов, если пишешь не драйвер, а устройство.

## Суть протокола (для сверки)

Стандартный 3-байтный пакет, stream mode, по умолчанию 100 отчётов/с,
разрешение 4 count/mm:

```
Байт 1: YO XO YS XS  1  MB RB LB
Байт 2: X movement (8 бит, знак в XS — т.е. 9-битное дополнение до двух)
Байт 3: Y movement (8 бит, знак в YS)
```

- `LB/RB/MB` — левая/правая/средняя кнопка;
- `XS/YS` — знаковые биты для X/Y;
- `XO/YO` — переполнение (значение вышло за −256..255);
- бит 3 первого байта всегда 1 — главная зацепка для ресинхронизации потока;
- Y растёт **вверх**, не вниз — экранную координату надо инвертировать;
- `ACK` = `0xFA`, после `0xFF` (Reset) приходит `0xAA 0x00`.

Команды, которые нужны почти всегда: `0xFF` Reset, `0xF4` Enable Data Reporting,
`0xF5` Disable, `0xF3` Set Sample Rate, `0xE8` Set Resolution, `0xE6/0xE7`
Set Scaling 1:1 / 2:1, `0xE9` Status Request (3 байта), `0xF2` Read Device Type,
`0xEB` Read Data (в remote mode), `0xF0` Set Remote Mode, `0xEA` Set Stream Mode.

Расширение на колесо (Intellimouse, 4 байта): после сброса послать
Set Sample Rate 200, затем 100, затем 80; `0xF2` вернёт `0x03` вместо `0x00`.
Четвёртый байт — знаковое 4-битное смещение колеса (для 5-кнопочной версии
в старших битах ещё кнопки 4/5, `0xF2` → `0x04`).

## Практический порядок работы

Берёшь Synaptics guide как спеку, Chapweske — как читаемое описание,
Brouwer — для вендорных расхождений, Linux `psmouse-base.c` — для проверки,
что реально работает на железе. Model-specific IBM TR — только за
электрикой и таймингами линии.

## Ссылки

- IBM PS/2 Hardware Interface Technical Reference (May 1988), bitsavers:
  https://kuiper.mirrorservice.org/sites/www.bitsavers.org/pdf/ibm/pc/ps2/Personal_System_2_Hardware_Interface_Technical_Reference_May88.pdf
- Каталоги IBM Technical Reference (ardent-tool):
  https://ardent-tool.com/docs/catalog/pcdmca.html
- Chapweske, The PS/2 Mouse Interface (зеркало):
  https://users.utcluj.ro/~baruch/sie/labor/PS2/PS-2_Mouse_Interface.htm
- Chapweske, зеркало TU Dortmund:
  https://sys-sideshow.cs.tu-dortmund.de/lehre/ws24/bsb/aufgaben/1/ps2mouse.html
- Brouwer, Keyboard scancodes, гл. 13 (aux/mouse):
  https://aeb.win.tue.nl/linux/kbd/scancodes-13.html
- Brouwer, гл. 12:
  https://www.win.tue.nl/~aeb/linux/kbd/scancodes-13.html
- Linux synaptics.c (bootlin, пример вендорного драйвера):
  https://elixir.bootlin.com/linux/v2.6.22.19/source/drivers/input/mouse/synaptics.c
- Synaptics PS/2 emulation (разбор, XDC2015):
  https://wiki.freedesktop.org/xorg/Events/XDC2015/Program/paul_ps2emu.pdf
