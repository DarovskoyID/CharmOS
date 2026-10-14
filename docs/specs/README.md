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
