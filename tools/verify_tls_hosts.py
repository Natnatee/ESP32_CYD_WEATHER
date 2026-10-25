"""Direct TLS 1.2 + hostname verification using ONLY the firmware PEM roots.
No proxy, credentials, insecure fallback, or device writes. One connection per host.
Requires Python stdlib; host OpenSSL is not proof of ESP32 mbedTLS performance.
"""
from pathlib import Path
import re
import socket
import ssl
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/live_data.cpp").read_text(encoding="utf-8")
urls = re.findall(r'"(https://[^"\n]+)"', source)
hosts = list(dict.fromkeys(urlsplit(url).hostname for url in urls))
roots = (ROOT / "src/api_roots.h").read_text(encoding="utf-8").split('R"PEM(\n', 1)[1].split('\n)PEM";', 1)[0]
context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
context.load_verify_locations(cadata=roots)
context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
failures = 0
for host in hosts:
    started = time.monotonic()
    try:
        with socket.create_connection((host, 443), timeout=8) as raw:
            with context.wrap_socket(raw, server_hostname=host) as connection:
                certificate = connection.getpeercert()
                print(f"VERIFIED {host} TLS1.2 exact-CA hostname/SNI elapsed={time.monotonic()-started:.3f}s "
                      f"cipher={connection.cipher()[0]} issuer={certificate['issuer']} "
                      f"valid={certificate['notBefore']}..{certificate['notAfter']}")
    except (OSError, ssl.SSLError) as error:
        failures += 1
        print(f"FAILED {host} {type(error).__name__}: {error}")
raise SystemExit(bool(failures))
