import json
from pathlib import Path
import sys
import time

path = Path(sys.argv[1]) / "chromium/tls-fixture/https-result.json"
for _ in range(50):
    if path.exists():
        break
    time.sleep(0.1)
result = json.loads(path.read_text())
assert result == {"requests": 2, "rejected": 4, "unexpected": [],
                  "versions": ["TLSv1.3", "TLSv1.2"],
                  "server_names": ["charmos.test", "charmos.test", "wrong.test",
                                   "charmos.test", "charmos.test", "charmos.test"]}, result
print("https host: two real requests, four rejected sessions verified")
