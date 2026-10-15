#!/usr/bin/env python3
# Из serial-журнала загрузки с acpi=dump — файлы таблиц: <каталог>/<префикс>-dsdt.aml,
# <префикс>-ssdt<N>.aml (образцы для хост-тестов интерпретатора AML).
import sys
log, out_dir, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
cur, data, n = None, [], 0
for line in open(log, encoding='utf-8', errors='replace'):
    line = line.strip()
    if not line.startswith('acpidump: '):
        continue
    body = line[len('acpidump: '):]
    if body.startswith('begin '):
        _, what, idx, length = body.split()
        cur, data = (what.lower(), idx, int(length)), []
    elif body == 'end' and cur:
        blob = bytes.fromhex(''.join(data))
        assert len(blob) == cur[2], (cur, len(blob))
        name = f"{prefix}-{cur[0]}{'' if cur[0] == 'dsdt' else cur[1]}.aml"
        open(f"{out_dir}/{name}", 'wb').write(blob)
        print(name, len(blob))
        n += 1
        cur = None
    elif cur:
        data.append(body)
print(n, 'tables')
