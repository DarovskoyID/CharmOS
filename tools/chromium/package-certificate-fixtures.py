#!/usr/bin/env python3
"""Package public certificates only from the pinned upstream test fixtures."""
import hashlib
import json
from pathlib import Path
import re
import sys

source, output = map(Path, sys.argv[1:])
fixtures = {
    'net-test-ok.pem': 'ok_cert.pem',
    'net-test-root.pem': 'root_ca_cert.pem',
    'net-test-expired.pem': 'expired_cert.pem',
}
report = {}
for destination, name in fixtures.items():
    original = source / 'net/data/ssl/certificates' / name
    certificates = re.findall(rb'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',
                              original.read_bytes(), re.DOTALL)
    if len(certificates) != 1:
        raise SystemExit('Unexpected certificate fixture: ' + str(original))
    content = certificates[0] + b'\n'
    (output / destination).write_bytes(content)
    report[destination] = {'source': str(original.relative_to(source)),
                           'sha256': hashlib.sha256(content).hexdigest()}
(output / 'native-certificate-fixtures.json').write_text(json.dumps(report, indent=2) + '\n')
