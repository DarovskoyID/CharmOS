# USB в M10

Свой xHCI в юзерспейсе, по Intel xHCI 1.2; USB 2.0, HID 1.11,
USB-IF Bulk-Only Transport 1.0 и Seagate SCSI Commands Reference Rev. M.
Первичные документы сохранены в docs/specs, источник SCSI — scsi-source.md.

lib/usb содержит wire-структуры, bounded command/event ring helpers,
проверяемые парсеры конфигураций и boot HID. Драйвер — user/drivers/xhci.

Работают: handoff/reset, scratchpads, MSI-X, корневые порты, enumeration,
control/interrupt/bulk transfers; клавиатура и относительная мышь boot HID;
один BOT/SCSI-накопитель LUN 0 на контроллер, сектора 512 байт, ёмкость до
READ CAPACITY(10), READ/WRITE(10), SYNCHRONIZE CACHE(10). DMA-сегменты
ядра идут прямо в bulk TRB; HID обслуживается во время дисковых запросов.
При ошибке BOT процесс завершается, менеджер перезапускает его и блочный
слой повторяет незавершённый запрос по обычным правилам block.md.

Пока нет внешних хабов/TT, USB3 companion descriptor tuning, произвольного
HID report protocol, переподключения, нескольких LUN и UAS. Устройства
перечисляются при запуске. Поддерживается до 16 слотов, 8 HID-интерфейсов
на устройство, конфигурационный дескриптор до 4096 байт.

Проверки: test_xhci на хосте, m10/xhci, usb_hid, usb_store1/2/3 и usb_combo.
Последний запускает GUI с USB-корня и совмещает 2 МиБ write/sync/readback
в pthread с клавиатурой и мышью на том же контроллере. Проверка реального
ПК остаётся критерием M10 и не заменяется этими QEMU-сценариями.
