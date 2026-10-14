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
