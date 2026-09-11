#!/usr/bin/env python3
"""
Actualiza la pantalla con el tiempo REAL de una ciudad (Madrid por defecto).

Consulta Open-Meteo (gratuito, sin clave de API), traduce la respuesta al comando
/set_weather del firmware y lo envía por el puerto USB serie.

Ejemplos:
  python update_weather.py                          # Madrid, una vez, por COM9
  python update_weather.py --interval 15            # cada 15 minutos hasta Ctrl+C
  python update_weather.py --city Sevilla --port COM9
  python update_weather.py --dry-run                # muestra el comando sin enviarlo
  python update_weather.py --set-time               # además sincroniza la hora del PC

Requiere:  pip install pyserial      (la consulta HTTP usa sólo la librería estándar)
"""
import argparse
import json
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime

from send_weather import send, quote   # reutiliza el envío serie (sin reset de la placa)

GEOCODE_URL = "https://geocoding-api.open-meteo.com/v1/search"
FORECAST_URL = "https://api.open-meteo.com/v1/forecast"

# Coordenadas conocidas para no depender del geocodificador en el caso habitual
KNOWN_CITIES = {
    "madrid": (40.4168, -3.7038),
}

# Código WMO -> (palabra clave que entiende el firmware, descripción en español)
WMO = {
    0:  ("sunny",         "Cielo despejado"),
    1:  ("sunny",         "Mayormente despejado"),
    2:  ("partly cloudy", "Parcialmente nublado"),
    3:  ("cloudy",        "Cubierto"),
    45: ("fog",           "Niebla"),
    48: ("fog",           "Niebla con escarcha"),
    51: ("rain",          "Llovizna ligera"),
    53: ("rain",          "Llovizna"),
    55: ("rain",          "Llovizna intensa"),
    56: ("rain",          "Llovizna helada"),
    57: ("rain",          "Llovizna helada intensa"),
    61: ("rain",          "Lluvia ligera"),
    63: ("rain",          "Lluvia"),
    65: ("rain",          "Lluvia intensa"),
    66: ("rain",          "Lluvia helada"),
    67: ("rain",          "Lluvia helada intensa"),
    71: ("snow",          "Nieve ligera"),
    73: ("snow",          "Nieve"),
    75: ("snow",          "Nieve intensa"),
    77: ("snow",          "Granos de nieve"),
    80: ("rain",          "Chubascos ligeros"),
    81: ("rain",          "Chubascos"),
    82: ("rain",          "Chubascos fuertes"),
    85: ("snow",          "Chubascos de nieve"),
    86: ("snow",          "Chubascos de nieve fuertes"),
    95: ("storm",         "Tormenta"),
    96: ("storm",         "Tormenta con granizo"),
    99: ("storm",         "Tormenta con granizo fuerte"),
}

COMPASS = ["N", "NE", "E", "SE", "S", "SO", "O", "NO"]


def http_json(url: str, params: dict, timeout: float = 15) -> dict:
    full = url + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(full, headers={"User-Agent": "weather-display/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def geocode(city: str):
    key = city.strip().lower()
    if key in KNOWN_CITIES:
        return KNOWN_CITIES[key], city.strip().title() if key == "madrid" else city.strip()
    data = http_json(GEOCODE_URL, {"name": city, "count": 1, "language": "es", "format": "json"})
    results = data.get("results") or []
    if not results:
        raise SystemExit(f"Ciudad no encontrada: {city}")
    r = results[0]
    return (r["latitude"], r["longitude"]), r["name"]


def fetch_weather(lat: float, lon: float) -> dict:
    return http_json(FORECAST_URL, {
        "latitude": lat,
        "longitude": lon,
        "current": ",".join([
            "temperature_2m", "relative_humidity_2m", "apparent_temperature", "weather_code",
            "wind_speed_10m", "wind_direction_10m", "wind_gusts_10m", "pressure_msl", "is_day", "precipitation",
        ]),
        "daily": "temperature_2m_max,temperature_2m_min,uv_index_max,precipitation_probability_max",
        "timezone": "Europe/Madrid",
        "forecast_days": 1,
    })


def build_command(city: str, data: dict) -> tuple[str, dict]:
    cur = data["current"]
    daily = data.get("daily", {})
    code = int(cur.get("weather_code", -1))
    sky, desc = WMO.get(code, ("cloudy", f"Código WMO {code}"))
    if sky == "sunny" and int(cur.get("is_day", 1)) == 0:
        sky, desc = "night", "Noche despejada"

    wind_dir = COMPASS[int((float(cur.get("wind_direction_10m", 0)) + 22.5) // 45) % 8]
    wind = f"{round(float(cur['wind_speed_10m']))} km/h {wind_dir}"
    gust = cur.get("wind_gusts_10m")
    if gust and float(gust) >= float(cur["wind_speed_10m"]) + 15:
        wind += f" (rachas {round(float(gust))})"

    fields = {
        "city": city,
        "temperature": f"{round(float(cur['temperature_2m']))}ºC",
        "sky": sky,
        "humidity": round(float(cur["relative_humidity_2m"])),
        "wind": wind,
        "feels": round(float(cur["apparent_temperature"])),
        "min": round(float(daily["temperature_2m_min"][0])),
        "max": round(float(daily["temperature_2m_max"][0])),
        "pressure": round(float(cur["pressure_msl"])),
        "uv": round(float(daily["uv_index_max"][0])),
        "desc": desc,
    }
    prob = (daily.get("precipitation_probability_max") or [None])[0]
    if prob is not None and prob >= 30 and sky not in ("rain", "storm", "snow"):
        fields["desc"] = f"{desc} · {int(prob)} % prob. de lluvia"

    cmd = "/set_weather " + " ".join(f"-{k} {quote(v)}" for k, v in fields.items())
    return cmd, fields


def push(port: str, line: str) -> bool:
    resp = send(port, line, wait=2.0)
    ok = False
    for ln in resp.splitlines():
        ln = ln.strip()
        if ln.startswith(("OK", "ERR", "WARN")):
            print("  <", ln)
            ok = ok or ln.startswith("OK")
    if not ok and "OK" not in resp:
        print("  < (sin respuesta OK de la placa)")
    return ok


def run_once(args) -> bool:
    (lat, lon), city_name = geocode(args.city)
    data = fetch_weather(lat, lon)
    cmd, fields = build_command(city_name, data)
    stamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{stamp}] {city_name}: {fields['temperature']} {fields['sky']} · {fields['desc']} · "
          f"hum {fields['humidity']} % · {fields['wind']} · {fields['min']}/{fields['max']}º · "
          f"{fields['pressure']} hPa · UV {fields['uv']}")
    print("  >", cmd)
    if args.dry_run:
        return True
    ok = True
    if args.set_time:
        ok = push(args.port, f'/set_time -datetime {quote(datetime.now().strftime("%Y-%m-%d %H:%M:%S"))}') and ok
    return push(args.port, cmd) and ok


def main():
    ap = argparse.ArgumentParser(description="Envía el tiempo real de Open-Meteo a la Weather Display")
    ap.add_argument("--city", default="Madrid")
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--interval", type=float, default=0, metavar="MIN",
                    help="repetir cada N minutos (0 = una sola vez)")
    ap.add_argument("--set-time", action="store_true", help="sincronizar también la hora del PC")
    ap.add_argument("--dry-run", action="store_true", help="no enviar, sólo mostrar el comando")
    args = ap.parse_args()

    if args.interval <= 0:
        return 0 if run_once(args) else 1

    print(f"Actualizando cada {args.interval:g} min (Ctrl+C para salir)")
    while True:
        try:
            run_once(args)
        except KeyboardInterrupt:
            raise
        except Exception as e:  # red caída, puerto ocupado... seguimos intentando
            print(f"  ! {type(e).__name__}: {e}")
        try:
            time.sleep(args.interval * 60)
        except KeyboardInterrupt:
            print("\nFin")
            return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nFin")
