#!/usr/bin/env python3
"""
Envía datos del tiempo a la pantalla por el puerto USB serie.

Ejemplos:
  python send_weather.py --port COM9 --city Madrid --temperature "32ºC" --sky sunny
  python send_weather.py --port COM9 --city Madrid --temperature 27 --sky "partly cloudy" \
                         --humidity 35 --wind "14 km/h SO" --min 19 --max 33 --pressure 1016 --uv 8
  python send_weather.py --port COM9 --raw '/set_weather -city "Madrid" -temperature "32ºC" -sky "sunny"'
  python send_weather.py --port COM9 --set-time          # sincroniza la hora del PC
  python send_weather.py --port COM9 --status            # imprime el JSON de estado

Requiere:  pip install pyserial
"""
import argparse
import json
import re
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("Falta pyserial:  pip install pyserial")

CRLF = "\r\n"


def send(port: str, line: str, wait: float = 1.5) -> str:
    # La placa usa USB-Serial/JTAG nativo: un cambio en DTR/RTS al abrir el puerto la reinicia.
    # Fijamos ambos a False ANTES de abrir para que pyserial no los active.
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.write_timeout = 2
    s.dtr = False
    s.rts = False
    s.open()
    try:
        time.sleep(0.15)
        pending = s.read(8192)
        if b"Calling app_main" in pending or b"ESP-ROM" in pending:
            # Se ha reiniciado a pesar de todo: esperar a que la consola arranque
            t0 = time.time()
            while time.time() - t0 < 6 and b"Consola lista" not in pending:
                pending += s.read(4096)
            time.sleep(0.2)
        s.reset_input_buffer()
        s.write((line + CRLF).encode("utf-8"))
        s.flush()
        t0 = time.time()
        out = b""
        while time.time() - t0 < wait:
            chunk = s.read(4096)
            if chunk:
                out += chunk
                if re.search(rb"(^|\n)(OK|ERR|WARN) ", out) and b"weather>" in out[-64:]:
                    break
    finally:
        s.close()
    return out.decode("utf-8", errors="replace")


def quote(v: str) -> str:
    return '"' + str(v).replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser(description="Envía el tiempo a la Weather Display por USB serie")
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--city")
    ap.add_argument("--temperature")
    ap.add_argument("--sky")
    ap.add_argument("--humidity")
    ap.add_argument("--wind")
    ap.add_argument("--feels")
    ap.add_argument("--min")
    ap.add_argument("--max")
    ap.add_argument("--pressure")
    ap.add_argument("--uv")
    ap.add_argument("--desc")
    ap.add_argument("--raw", help="línea de comando completa a enviar tal cual")
    ap.add_argument("--set-time", action="store_true", help="ajusta la hora del dispositivo con la del PC")
    ap.add_argument("--status", action="store_true", help="muestra /status")
    ap.add_argument("--wifi", nargs=2, metavar=("SSID", "PASSWORD"), help="configura la red Wi-Fi")
    a = ap.parse_args()

    lines = []
    if a.set_time:
        lines.append(f'/set_time -datetime {quote(datetime.now().strftime("%Y-%m-%d %H:%M:%S"))}')
    if a.wifi:
        lines.append(f"/set_wifi -ssid {quote(a.wifi[0])} -password {quote(a.wifi[1])}")
    if a.raw:
        lines.append(a.raw)
    fields = [("city", a.city), ("temperature", a.temperature), ("sky", a.sky), ("humidity", a.humidity),
              ("wind", a.wind), ("feels", a.feels), ("min", a.min), ("max", a.max),
              ("pressure", a.pressure), ("uv", a.uv), ("desc", a.desc)]
    kv = " ".join(f"-{k} {quote(v)}" for k, v in fields if v is not None)
    if kv:
        lines.append("/set_weather " + kv)
    if a.status:
        lines.append("/status")
    if not lines:
        ap.print_help()
        return 1

    rc = 0
    for line in lines:
        print(">", line)
        resp = send(a.port, line, wait=3.0 if "/status" in line else 1.5)
        for ln in resp.splitlines():
            ln = ln.strip()
            if not ln or ln == "weather>" or ln.startswith("weather>" + line[:8]):
                continue
            if ln.startswith("{"):
                try:
                    print(json.dumps(json.loads(ln), indent=2, ensure_ascii=False))
                    continue
                except ValueError:
                    pass
            print(" ", ln)
            if ln.startswith("ERR"):
                rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
