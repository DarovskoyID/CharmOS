# Спецификации

Код, работающий с железом или чужим форматом, пишется только по документам
отсюда (CLAUDE.md, правило 4). Добавляя файл, дописать строку в таблицу.

| Файл | Документ | Источник | Нужен с |
|---|---|---|---|
| `uefi-2.11.pdf` | UEFI Specification 2.11 | https://uefi.org/specifications (скачивается вручную: сайт за Cloudflare) | M1 |
| `intel-sdm-vol3a.pdf` | Intel SDM Vol. 3A, System Programming Guide (253668) | https://cdrdv2-public.intel.com/782154/253668-sdm-vol-3a.pdf | M1 |
| `elf-gabi.pdf` | ELF (Tool Interface Standard, gABI) | https://refspecs.linuxfoundation.org/elf/elf.pdf | M1 |
| `x86-64-psabi.pdf` | System V ABI, AMD64 Architecture Processor Supplement | https://gitlab.com/x86-psABIs/x86-64-ABI | M1 |
| `elf64.pdf` | ELF-64 Object File Format 1.5 (HP/Intel) — структуры Elf64_* (в `elf-gabi.pdf` только ELF32) | https://uclibc.org/docs/elf-64-gen.pdf | M1 |
| `ioapic-82093aa.pdf` | Intel 82093AA I/O APIC datasheet (290566-001) | копия: https://pdos.csail.mit.edu/6.1810/2006/readings/ia32/ioapic.pdf (intel.com больше не отдаёт) | M2 |
| `hpet-1.0a.pdf` | IA-PC HPET Specification 1.0a (Intel) | копия: https://sys-sideshow.cs.tu-dortmund.de/lehre/ws22/bsb/aufgaben/7/intel_hpet.pdf (intel.com больше не отдаёт) | M2 |
| `acpi-6.5.pdf` | ACPI Specification 6.5 (RSDP/XSDT §5.2.5–5.2.8, MADT §5.2.12) | https://uefi.org/specifications (вручную: Cloudflare) | M2 |
| `8259a.pdf` | Intel 8259A PIC datasheet (231468-003) — маскирование legacy PIC (OCW1) | копия: https://pdos.csail.mit.edu/6.1810/2004/readings/hardware/8259A.pdf | M2 |
| `intel-sdm-vol2.pdf` | Intel SDM Vol. 2A–2D, Instruction Set Reference (325383) — SYSCALL/SYSRET, CPUID, SWAPGS | https://cdrdv2-public.intel.com/782156/325383-sdm-vol-2abcd.pdf | M3 |
| `intel-sdm-vol4.pdf` | Intel SDM Vol. 4, Model-Specific Registers (335592) — адреса MSR (STAR/LSTAR/FMASK, GS_BASE) | https://cdrdv2-public.intel.com/782167/335592-sdm-vol-4.pdf | M3 |
| `vtd-5.2.pdf` | Intel VT-d Architecture Specification, rev. 5.20 (D51397-019) — IOMMU, таблица DMAR | https://cdrdv2-public.intel.com/919688/D51397-019-vt-directed-io-spec.pdf | M5 |
| `pci-reference.md` | **Вторичный** референс по PCI/PCIe (сводка автора по открытым источникам): заголовок, BAR, MSI/MSI-X, MCFG, enumeration. Официальные спецификации PCI-SIG недоступны. Известные ошибки: у `edu` нет MSI-X; не указан 28-битный DMA `edu`. Расхождения с docs/abi — прав docs/abi | автор проекта | M5 |
| `qemu-edu.rst` | Учебное устройство QEMU `edu` (1234:11e8): регистры, DMA, INTx/MSI | https://gitlab.com/qemu-project/qemu/-/raw/master/docs/specs/edu.rst | M5 |
| `virtio-1.2.pdf` | Virtual I/O Device (VIRTIO) 1.2, Committee Specification 01 (OASIS, 2022-07-01) — virtio-pci, virtqueue, virtio-blk | https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/virtio-v1.2-cs01.pdf | M6 |
| `nvme-base-2.1.pdf` | NVM Express Base Specification 2.1 (2024-08-05) — контроллер, очереди, Admin-команды | https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-Revision-2.1-2024.08.05-Ratified.pdf | M6 |
| `nvme-nvm-cmdset-1.1.pdf` | NVM Express NVM Command Set 1.1 (2024-08-05) — Read/Write/Flush, Identify Namespace | https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-Revision-1.1-2024.08.05-Ratified.pdf | M6 |
| `fatgen103.doc` | Microsoft EFI FAT32 File System Specification 1.03 (Word) | https://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/fatgen103.doc | M6 |
| `ext2-doc.html` | **Вторичный:** «The Second Extended File System», Dave Poirier (официальной спецификации ext2 нет) | https://www.nongnu.org/ext2-doc/ext2.html | M6 |
| `ahci-1.3.1.pdf` | Serial ATA AHCI 1.3.1 (Intel) — положен автором вручную (intel.com не отдаёт скриптом) | https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html | M6 |
| `nvme-pcie-1.4.pdf` | NVMe over PCIe Transport Specification 1.4 (2026-07-31) — doorbell, PRP, MSI-X, регистры контроллера на PCIe; положен автором вручную. Ревизия новее базовой 2.1 — при расхождении сверять | https://nvmexpress.org/specifications/ | M6 |
| `acs-3-r5.pdf` | T13/2161-D ATA/ATAPI Command Set - 3 (ACS-3), рабочий черновик rev. 5 (2013-10-28) — команды ATA, IDENTIFY DEVICE | копия: https://read.seas.harvard.edu/cs161/2019/pdf/ata-atapi-8.pdf (T13: https://t13.org/project-working-drafts) | M6 |
| `sata-ahci-summary.md` | **Вторичный:** сводка автора по SATA/AHCI (H2D FIS, последовательности); Serial ATA Revision закрыт (SATA-IO) | автор проекта | M6 |
| `intel-8254x-sdm.pdf` | PCI/PCI-X Family of Gigabit Ethernet Controllers Software Developer's Manual (82540EP/EM… — e1000 в QEMU), rev. 4.0, Intel 317453006 | копия: https://pdos.csail.mit.edu/6.828/2019/readings/hardware/8254x_GBe_SDM.pdf (intel.com отвечает 403) | M7 |
| `rfc/rfc*.txt` | RFC 791 (IPv4), 792 (ICMP), 826 (ARP), 768 (UDP), 894 (IP over Ethernet), 9293 (TCP), 1122 (требования к хостам), 5681 (TCP congestion), 6298 (RTO), 6691 (MSS), 2131/2132 (DHCP), 1034/1035 (DNS) | https://www.rfc-editor.org/rfc/rfcN.txt | M7 |
| `ns16c2752.pdf` | TI NS16C2552/NS16C2752 Dual UART (SNLS238D, 2013) — наследник PC16550D, совместимая раскладка регистров 16550 (RBR/THR/IER/IIR/FCR/LCR/MCR/LSR/MSR/SCR/DLL/DLM); для COM1 (QEMU эмулирует 16550A). Сам PC16550D с ti.com скриптом не скачивается | положен автором вручную (ti.com) | M8 |
| `rfc/rfc8032.txt` | RFC 8032 — Edwards-Curve Digital Signature Algorithm (Ed25519) — подписи пакетов | https://www.rfc-editor.org/rfc/rfc8032.txt | M8.5 |
| `fips-180-4.pdf` | FIPS PUB 180-4, Secure Hash Standard (SHA-256/512; SHA-512 нужен и Ed25519) | https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf | M8.5 |
| `rfc/rfc9110.txt`, `rfc/rfc9112.txt` | RFC 9110 (HTTP Semantics), RFC 9112 (HTTP/1.1) — httpd и клиент магазина | https://www.rfc-editor.org/rfc/rfcN.txt | M8.5 |
| `fips-197.pdf` | FIPS PUB 197 (upd1, 2023), Advanced Encryption Standard (AES) | https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197-upd1.pdf | M9 |
| `sp800-38e.pdf` | NIST SP 800-38E — режим XTS-AES для шифрования дисков. Сам алгоритм XTS описан в IEEE 1619 (платный); 800-38E на него ссылается | https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38e.pdf | M9 |
| `tpm2-part0-intro.pdf` … `tpm2-part3-commands.pdf` | TCG TPM 2.0 Library, Version 1.85: Part 0 Introduction, Part 1 Architecture, Part 2 Structures, Part 3 Commands | https://trustedcomputinggroup.org/resource/tpm-library-specification/ (https://tcg-space.nyc3.digitaloceanspaces.com/) | M9 |
| `tpm2-library-1.85-errata.pdf` | Errata к TPM 2.0 Library 1.85 | то же | M9 |
| `tcg-ptp-1.07.pdf`, `tcg-ptp-1.07-errata-1.1.pdf` | TCG PC Client Platform TPM Profile (PTP) for TPM 2.0, 1.07 — интерфейсы TIS (FIFO) и CRB, регистры | https://trustedcomputinggroup.org/resource/pc-client-platform-tpm-profile-ptp-specification/ | M9 |
| `tcg-pc-client-pfp-1.06.pdf` | TCG PC Client Platform Firmware Profile 1.06 r52 — назначение PCR, журнал измерений загрузки | https://trustedcomputinggroup.org/resource/pc-client-specific-platform-firmware-profile-specification/ | M9 |
| `tcg-efi-protocol.pdf`, `tcg-efi-protocol-errata-0.6.pdf` | TCG EFI Protocol 1.0 r0.13 — EFI_TCG2_PROTOCOL (измерения из загрузчика) | https://trustedcomputinggroup.org/resource/tcg-efi-protocol-specification/ | M9 |
| `ieee-p1619-d16.pdf` | IEEE P1619/D16 (май 2007) — черновик стандарта XTS-AES; технически почти совпадает с IEEE 1619-2007 («не для соответствия»). Annex B — тест-векторы, Annex C — псевдокод. Сам IEEE 1619 платный | https://crossbowerbt.github.io/docs/crypto/pdf00086.pdf | M9 |
| `ieee-1619-xts.md` | **Вторичный:** сводка автора по IEEE 1619 / SP 800-38E и критике XTS (Key1 ≠ Key2, лимиты data unit) | автор проекта | M9 |
| `rtl8139d.pdf` | Realtek RTL8139D(L) Single-Chip Fast Ethernet Controller, Rev. 1.11 (2001) | https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139D_DataSheet.pdf | M9 |
| `rtl8169-1.21.pdf` | Realtek RTL8169 Gigabit Ethernet MAC, Rev. 1.21 (2002) | https://people.freebsd.org/~wpaul/RealTek/rtl8169spec-121.pdf | M9 |
| `rtl8169s-8110s-1.3.pdf` | Realtek RTL8169S/RTL8110S (MAC+PHY), Rev. 1.3 (2003) | http://www.bitsavers.org/components/realtek/RTL8139/RTL8110S_8169S_DataSheet_1.3_200309.pdf | M9 |
| `rtl8169sc-8110sc-regs.pdf` | Realtek RTL8169SC/8110SC Registers, Rev. 1.0 | https://people.freebsd.org/~wpaul/RealTek/RTL8169SC_8110SC_reg_1_0.pdf | M9 |
| `rtl8139-rtl8169-datasheets.md` | **Вторичный:** сводка автора по даташитам RTL8139/8169 (регистры, модели Rx/Tx). Расхождения — прав даташит | автор проекта | M9 |
| `ibm-ps2-common-1990.pdf` | IBM PS/2 Hardware Interface Technical Reference — Common Interfaces (84F9735, 1990): «Keyboard/Auxiliary Device Controller» (8042), клавиатура и скан-коды | http://www.bitsavers.org/pdf/ibm/pc/ps2/84F9735_PS2_Hardware_Interface_Technical_Reference_Common_Interfaces_Oct90.pdf | M10 |
| `ibm-ps2-hitr-1988.pdf` | IBM Personal System/2 Hardware Interface Technical Reference (1988): контроллер и клавиатура (наборы скан-кодов 1–3) | http://www.bitsavers.org/pdf/ibm/pc/ps2/Personal_System_2_Hardware_Interface_Technical_Reference_May88.pdf | M10 |
| `hid-1.11.pdf` | USB Device Class Definition for HID 1.11 | https://www.usb.org/sites/default/files/hid1_11.pdf | M10 |
| `hid-usage-tables-1.12.pdf` | USB HID Usage Tables 1.12 | https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf | M10 |
| `usb-2.0.pdf` | Universal Serial Bus Specification 2.0 (из архива usb_20_20211008.zip) | https://www.usb.org/sites/default/files/usb_20_20211008.zip | M10 |
| `usb-msc-bot-1.0.pdf` | USB Mass Storage Class — Bulk-Only Transport 1.0 | https://www.usb.org/sites/default/files/usbmassbulk_10.pdf | M10 |
| \high-definition-audio-specification.pdf\ | Intel High Definition Audio Specification 1.0a (2010) | https://www.intel.com/content/www/us/en/standards/high-definition-audio-specification.html (вручную, автор) | M10 |
| \ps2-mouse-sources.md\ | **Вторичный:** сводка автора по протоколу мыши PS/2 (пакет 3 байта, команды) с источниками | автор проекта | M10 |
