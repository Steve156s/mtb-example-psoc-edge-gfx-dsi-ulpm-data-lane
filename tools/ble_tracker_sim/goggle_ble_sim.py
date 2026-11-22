#!/usr/bin/env python3
"""
HUD Link Tracker / phone simulator for bring-up without a LoRa Tracker.

Connects to the goggle ("ASUS HUD", HUD Link service), says HELLO as a
Tracker, then sends a simulated GNSS fix and three team members every second
and a text message every 20 s. Prints the goggle status notifications.

    pip install bleak
    python3 goggle_ble_sim.py                 # first goggle found
    python3 goggle_ble_sim.py --address AA:BB:CC:DD:EE:FF
    python3 goggle_ble_sim.py --phone --wifi "Lodge" "secret123"

Security: the default firmware (GOGGLE_LINK_SECURE=1) only accepts writes on
an encrypted link. Pair the goggle in the OS Bluetooth settings first (type
the 6-digit code shown on the HUD), or build the goggle with
make GOGGLE_LINK_SECURE=0 for a quick test.

The packet layout matches shared/goggle_link/goggle_link_proto.h.
"""
import argparse
import asyncio
import math
import struct
import time

BASE = "4a5d{:04x}-6f1e-4b8c-9c3a-1d2e5a0b7c60"
UUID_SERVICE = BASE.format(0x0000)
UUID_GNSS = BASE.format(0x0001)
UUID_TEAM = BASE.format(0x0002)
UUID_MESSAGE = BASE.format(0x0003)
UUID_STATUS = BASE.format(0x0004)
UUID_CONTROL = BASE.format(0x0005)
UUID_WIFI = BASE.format(0x0006)

VER = 1
CMD_HELLO, CMD_SET_TIME, CMD_BRIGHTNESS, CMD_WIFI, CMD_CLEAR_MSG = 1, 2, 3, 4, 5
ROLE_TRACKER, ROLE_PHONE = 1, 2
MSG_TEXT, MSG_SOS, MSG_NAV, MSG_AI = 0, 1, 2, 3
FLAG_SOS, FLAG_STALE, FLAG_LOW_BATT, FLAG_PTT = 1, 2, 4, 8


def enc_gnss(lat, lon, alt_m, speed_ms, course_deg, sats=9, hdop=1.2, fix3d=True, utc=None):
    flags = 0x01 | (0x02 if fix3d else 0)
    return struct.pack("<BBBBiiiHHI", VER, flags, sats, int(hdop * 10),
                       int(round(lat * 1e7)), int(round(lon * 1e7)), int(round(alt_m * 10)),
                       int(round(speed_ms * 100)), int(round(course_deg * 100)) % 36000,
                       int(utc if utc is not None else time.time()))


def enc_team(members):
    out = struct.pack("<BB", VER, len(members))
    for m in members:
        name = m["name"].encode("ascii", "replace")[:4].ljust(4, b"\0")
        out += struct.pack("<BBBbHHhH4s", m["id"], m.get("flags", 0), m.get("batt", 0xFF),
                           m.get("rssi", -100), int(round(m["bearing"] * 100)) % 36000,
                           min(int(m["dist"]), 0xFFFF), int(m["dalt"]), m.get("age", 0), name)
    return out


def enc_msg(text, mtype=MSG_TEXT, from_id=0, prio=0, ttl_s=30):
    body = text.encode("utf-8")[:96]
    return struct.pack("<BBBBH", VER, mtype, from_id, prio, ttl_s) + body


def enc_ctrl(cmd, payload=b""):
    return struct.pack("<BB", VER, cmd) + payload


def enc_wifi(ssid, password):
    s, p = ssid.encode(), password.encode()
    return bytes([VER, len(s)]) + s + bytes([len(p)]) + p


def dec_status(b):
    if len(b) != 16 or b[0] != VER:
        return None
    _, batt, flags, links, spd, hdg, alt, temp, _ = struct.unpack("<BBBBHHihH", bytes(b))
    return dict(battery=None if batt == 0xFF else batt, flags=flags, links=links,
                speed_kmh=spd * 0.036, heading=None if hdg == 0xFFFF else hdg / 100,
                alt_m=alt / 10, temp_c=temp / 10)


def offset(lat, lon, bearing_deg, dist_m):
    """Point at bearing/distance from lat/lon (flat earth, fine for < 10 km)."""
    dn = dist_m * math.cos(math.radians(bearing_deg))
    de = dist_m * math.sin(math.radians(bearing_deg))
    return lat + dn / 111320.0, lon + de / (111320.0 * math.cos(math.radians(lat)))


async def run(args):
    from bleak import BleakClient, BleakScanner

    address = args.address
    if address is None:
        print("Scanning for the HUD Link service ...")
        dev = await BleakScanner.find_device_by_filter(
            lambda d, ad: UUID_SERVICE in [u.lower() for u in ad.service_uuids], timeout=15)
        if dev is None:
            raise SystemExit("goggle not found")
        address = dev.address
    print("Connecting to", address)

    async with BleakClient(address) as client:
        if args.pair:
            try:
                await client.pair()
            except Exception as e:  # not supported on every OS backend
                print("pair():", e)

        def on_status(_, data):
            print("goggle status:", dec_status(data))

        await client.start_notify(UUID_STATUS, on_status)
        role = ROLE_PHONE if args.phone else ROLE_TRACKER
        await client.write_gatt_char(UUID_CONTROL, enc_ctrl(CMD_HELLO, bytes([role])), response=True)
        tz_q15 = int(round(-time.timezone / 900))
        await client.write_gatt_char(UUID_CONTROL,
                                     enc_ctrl(CMD_SET_TIME, struct.pack("<Ib", int(time.time()), tz_q15)),
                                     response=True)

        if args.wifi:
            await client.write_gatt_char(UUID_WIFI, enc_wifi(*args.wifi), response=True)
            print("Wi-Fi credentials sent")

        # simulated run down a slope near (args.lat, args.lon)
        lat0, lon0, alt0 = args.lat, args.lon, args.alt
        t0 = time.time()
        n = 0
        while args.seconds <= 0 or time.time() - t0 < args.seconds:
            t = time.time() - t0
            speed = 8.0 + 4.0 * math.sin(t / 10)              # m/s
            course = (135 + 20 * math.sin(t / 15)) % 360
            lat, lon = offset(lat0, lon0, course, speed * t)
            alt = alt0 - 0.25 * speed * t
            if not args.phone:
                await client.write_gatt_char(UUID_GNSS, enc_gnss(lat, lon, alt, speed, course),
                                             response=False)
                team = [
                    dict(id=2, name="BOB", bearing=(220 + t) % 360, dist=1200 + 10 * t, dalt=-40,
                         batt=80, rssi=-95, age=3),
                    dict(id=3, name="ANNA", bearing=40, dist=2600, dalt=85, batt=15, rssi=-110,
                         age=5, flags=FLAG_SOS if 30 < t % 60 < 45 else 0),
                    dict(id=4, name="KEN", bearing=(10 + 2 * t) % 360, dist=max(50, 320 - 2 * t),
                         dalt=12, batt=55, rssi=-80, age=1, flags=FLAG_PTT if int(t) % 10 < 3 else 0),
                ]
                await client.write_gatt_char(UUID_TEAM, enc_team(team), response=False)
            if n % 20 == 0:
                text = "Meet at lift 3" if not args.phone else "Turn left in 200 m"
                mtype = MSG_TEXT if not args.phone else MSG_NAV
                await client.write_gatt_char(UUID_MESSAGE, enc_msg(text, mtype, from_id=4),
                                             response=True)
            n += 1
            await asyncio.sleep(1.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--address")
    ap.add_argument("--phone", action="store_true", help="act as the phone (nav message, no GNSS)")
    ap.add_argument("--pair", action="store_true", help="call pair() before writing")
    ap.add_argument("--wifi", nargs=2, metavar=("SSID", "PASSWORD"))
    ap.add_argument("--lat", type=float, default=36.8053)
    ap.add_argument("--lon", type=float, default=140.6851)
    ap.add_argument("--alt", type=float, default=1200.0)
    ap.add_argument("--seconds", type=float, default=0, help="0 = run until Ctrl+C")
    asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    main()
