"""Verify actual firmware URLs/schema and embedded CA trust with bounded HTTPS GETs.
Requires Python requests. No credentials, no firmware writes. Do not run repeatedly:
Yahoo endpoints are unofficial/rate limited. Exit nonzero for any unavailable endpoint.
"""
from pathlib import Path
import json
import re
import tempfile
import requests

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/live_data.cpp").read_text(encoding="utf-8")
urls = re.findall(r'"(https://[^"\n]+)"', source)
roots = (ROOT / "src/api_roots.h").read_text(encoding="utf-8").split('R"PEM(\n', 1)[1].split('\n)PEM";', 1)[0]
failed = False
with tempfile.TemporaryDirectory(prefix="cyd-api-check-") as folder:
    ca = Path(folder) / "roots.pem"
    ca.write_text(roots + "\n", encoding="ascii")
    for i, url in enumerate(urls):
        try:
            response = requests.get(url, headers={"User-Agent": "Mozilla/5.0"}, verify=str(ca), timeout=(5, 8))
            response.raise_for_status()
            data = response.json()
            if i == 0:
                current = data["current"]
                result = {k: current[k] for k in ("time", "temperature_2m", "relative_humidity_2m", "apparent_temperature", "weather_code", "wind_speed_10m")}
                assert all(isinstance(v, (float, int)) for k, v in result.items() if k != "time")
            elif i == 1:
                assert data["status"] == "success"
                result = data["response"]["price"]["gold_bar"]
                assert all(float(result[k].replace(",", "")) > 0 for k in ("buy", "sell"))
                result = dict(result, source_date=data["response"]["update_date"], source_time=data["response"]["update_time"])
            elif i == 2:
                assert data["status"] == "success"
                result = {"source_date": data["response"]["date"], "stations": {}}
                for station in ("ptt", "pt", "bcp"):
                    products = {}
                    for product, expected in zip(("gasohol_95", "gasohol_91", "gasohol_e20", "diesel"),
                                                ("แก๊สโซฮอล์ 95", "แก๊สโซฮอล์ 91", "แก๊สโซฮอล์ E20", "ดีเซล B7")):
                        key = "disel" if station == "bcp" and product == "diesel" else product
                        entry = data["response"]["stations"].get(station, {}).get(key)
                        if entry is None:
                            products[key] = "UNAVAILABLE (no substitution)"
                            continue
                        assert entry["name"] == expected, (station, key, entry)
                        assert float(entry["price"].replace(",", "")) > 0
                        products[key] = entry
                    result["stations"][station] = products
            else:
                assert data["chart"]["error"] is None
                meta = data["chart"]["result"][0]["meta"]
                result = {k: meta[k] for k in ("symbol", "currency", "instrumentType", "regularMarketPrice", "regularMarketTime")}
                assert meta["symbol"] == ["^SET.BK", "PTT.BK", "PTG.BK", "AOT.BK", "ADVANC.BK", "CPALL.BK"][i - 3]
                assert meta["instrumentType"] == ("INDEX" if i == 3 else "EQUITY")
                assert meta["currency"] == "THB"
                chart = data["chart"]["result"][0]
                stamps = chart["timestamp"]
                closes = chart["indicators"]["quote"][0]["close"]
                assert len(stamps) == len(closes)
                observations = [(stamp, close) for stamp, close in zip(stamps, closes)
                                if isinstance(close, (float, int)) and close > 0 and stamp <= meta["regularMarketTime"]]
                assert observations, "No real dated history"
                result.update(history_count=len(observations), first=observations[0], last=observations[-1], body_bytes=len(response.content))
                assert isinstance(meta["regularMarketPrice"], (int, float)) and meta["regularMarketTime"] > 1704067200
            print("VERIFIED", response.status_code, url, json.dumps(result, ensure_ascii=False))
        except Exception as error:
            failed = True
            print("UNAVAILABLE", url, type(error).__name__, str(error))
raise SystemExit(1 if failed else 0)
