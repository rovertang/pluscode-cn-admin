"""Verify C ABI JSON against the original Python V2 reader, including every block."""
import argparse
import ctypes as ct
import hashlib
import json
import lzma
import math
from pathlib import Path
import random
import shutil
import sqlite3
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parent
sys.path.insert(0, str(ROOT / "tests" / "reference"))
from query_v2 import PlusCodeKV
from codec_v2 import BRANCH, BOUNDARY, OVERLAP, FORMAT, pack_directory, pack_tree
from openlocationcode import openlocationcode as olc


class Native:
    def __init__(self, path, database):
        self.lib = ct.CDLL(str(path))
        text_out = ct.POINTER(ct.c_void_p)
        self.lib.pcad_open.argtypes = [ct.c_char_p, ct.c_void_p, ct.POINTER(ct.c_uint64), text_out]
        self.lib.pcad_lookup_code.argtypes = [ct.c_uint64, ct.c_char_p, text_out, text_out]
        self.lib.pcad_lookup_latlng.argtypes = [ct.c_uint64, ct.c_double, ct.c_double, text_out, text_out]
        self.lib.pcad_close.argtypes = [ct.c_uint64, text_out]
        self.lib.pcad_free.argtypes = [ct.c_void_p]
        self.lib.pcad_free.restype = None
        self.handle = ct.c_uint64()
        error = ct.c_void_p()
        status = self.lib.pcad_open(str(database).encode(), None, ct.byref(self.handle), ct.byref(error))
        message = ct.string_at(error.value).decode() if error.value else ""
        self.lib.pcad_free(error)
        if status:
            raise RuntimeError((status, message))

    def call(self, function, *arguments):
        result, error = ct.c_void_p(), ct.c_void_p()
        status = function(self.handle, *arguments, ct.byref(result), ct.byref(error))
        try:
            if status:
                raise RuntimeError((status, ct.string_at(error.value).decode() if error.value else ""))
            return json.loads(ct.string_at(result.value).decode())
        finally:
            self.lib.pcad_free(result)
            self.lib.pcad_free(error)

    def lookup(self, code):
        return self.call(self.lib.pcad_lookup_code, code.encode())

    def latlng(self, lat, lng):
        return self.call(self.lib.pcad_lookup_latlng, lat, lng)

    def close(self):
        error = ct.c_void_p()
        status = self.lib.pcad_close(self.handle, ct.byref(error))
        self.lib.pcad_free(error)
        assert status == 0


def fixture(path):
    conn = sqlite3.connect(path)
    conn.executescript("CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT); CREATE TABLE admins(id INTEGER PRIMARY KEY,value TEXT); CREATE TABLE roots(key TEXT PRIMARY KEY,owner INTEGER,value BLOB); CREATE TABLE tiles(key TEXT PRIMARY KEY,value BLOB)")
    meta = {"format": FORMAT, "complete": True, "precision": 11, "coverage": "selected_6_digit_tiles"}
    conn.executemany("INSERT INTO metadata VALUES (?,?)", [(k, json.dumps(v)) for k, v in meta.items()])
    for i in (1, 2):
        conn.execute("INSERT INTO admins VALUES (?,?)", (i, json.dumps({"admin_id": i, "province": "Province \U0001f600", "province_code": "1", "city": None, "city_code": None, "county": f"County {i}", "county_code": str(i)}, ensure_ascii=False)))
    terminal = ([0, 1, 2 | BOUNDARY | OVERLAP] + [1 | BOUNDARY] * 17, {})
    child = ([BRANCH, 1, 0, 2] + [0] * 396, {0: terminal})
    root = ([BRANCH, 1, 0, 2] + [0] * 396, {0: child})
    conn.execute("INSERT INTO roots VALUES (?,?,?)", ("6FG2", None, pack_directory([BRANCH] + [0] * 399)))
    conn.execute("INSERT INTO tiles VALUES (?,?)", ("6FG222", pack_tree(root)))
    conn.commit(); conn.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--db", type=Path, default=REPOSITORY / "data/processed/v2/pluscode_admin_v2.sqlite")
    parser.add_argument("--temp", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--all-tiles", action="store_true")
    args = parser.parse_args()
    args.temp.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    native, python = Native(args.library, args.db), PlusCodeKV(args.db, cache_size=8)
    checks = 0
    def compare(code):
        nonlocal checks
        actual, expected = native.lookup(code), python.lookup(code)
        assert actual == expected, (code, actual, expected)
        checks += 1
    rng = random.Random(20260907)
    for _ in range(1000):
        lat, lng = rng.uniform(-89.99, 89.99), rng.uniform(-179.99, 179.99)
        assert native.latlng(lat, lng) == python.lookup_latlng(lat, lng), (lat, lng)
        checks += 1
        for length in (8, 10, 11, 15): compare(olc.encode(lat, lng, length))
    for lat, lng in [(90, 180), (-90, -180), (0, -180), (0, 180), (90, 0), (-90, 0), (0, 0), (39.9042, 116.4074),
                     (0, -180 + 1e-8), (0, 180 - 1e-8), (90 - 1e-8, 0), (-90 + 1e-8, 0)]:
        assert native.latlng(lat, lng) == python.lookup_latlng(lat, lng), (lat, lng, native.latlng(lat, lng), python.lookup_latlng(lat, lng))
        checks += 1
    grid_rng = random.Random(20260907)
    grid_checks = 0
    for _ in range(2000):
        latitude = grid_rng.randrange(-80 * 40000, 80 * 40000) / 40000
        longitude = grid_rng.randrange(-179 * 32000, 179 * 32000) / 32000
        for delta in (0, -1e-9, 1e-9):
            lat, lng = latitude + delta, longitude + delta
            actual, expected = native.latlng(lat, lng), python.lookup_latlng(lat, lng)
            assert actual == expected, (lat, lng, actual, expected)
            checks += 1
            grid_checks += 1
    keys = [key for key, in python.connection.execute("SELECT key FROM tiles ORDER BY key")]
    if not args.all_tiles: keys = keys[::max(1, len(keys) // 1000)]
    last = time.perf_counter()
    for i, key in enumerate(keys):
        compare(key + "GG+GGG")
        if i % 100 == 0:
            compare(key + "GG+")
            compare(key + "GG+GG")
        if time.perf_counter() - last > 20:
            print(json.dumps({"compared_blocks": i + 1, "total": len(keys)}), flush=True)
            last = time.perf_counter()
    for code in ("BAD", "WC34+MX", "8PFR0000+", "8PFRWC34+M", "8PFRWC34+MX0"):
        try: native.lookup(code); raise AssertionError("Invalid code accepted")
        except RuntimeError as error: assert error.args[0][0] == 1
    for lat, lng in [(math.nan, 0), (math.inf, 0), (91, 0), (0, 181)]:
        try: native.latlng(lat, lng); raise AssertionError("Invalid coordinate accepted")
        except RuntimeError as error: assert error.args[0][0] == 1
    native.close(); python.close()
    example = args.temp / "fixture.sqlite"
    if example.exists(): example.unlink()
    fixture(example)
    native, python = Native(args.library, example), PlusCodeKV(example)
    for code in ("6FG22222+", "6FG22222+22", "6FG22222+222", "6FG22222+223", "6FG22222+224", "6FG22223+", "6FG22224+", "6FG22225+", "6FG22322+"):
        compare(code)
    native.close(); python.close()
    corruptions = {
        "incomplete": ("UPDATE metadata SET value='false' WHERE key='complete'", ()),
        "wrong_format": ("UPDATE metadata SET value='\"v1\"' WHERE key='format'", ()),
        "missing_tile": ("DELETE FROM tiles", ()),
        "bad_xz": ("UPDATE tiles SET value=?", (b"invalid",)),
        "unknown_id": ("UPDATE tiles SET value=?", (pack_tree(([3000] * 400, {})),)),
        "bad_span": ("UPDATE tiles SET value=?", (lzma.compress(b"\x01\x01" + struct.pack("<HHHHI", 0, 1, 0, BRANCH, 0xffffffff)),)),
        "bad_directory": ("UPDATE roots SET value=?", (b"invalid",)),
    }
    for name, (sql, parameters) in corruptions.items():
        bad = args.temp / (name + ".sqlite")
        shutil.copyfile(example, bad)
        conn = sqlite3.connect(bad); conn.execute(sql, parameters); conn.commit(); conn.close()
        index = None
        try:
            index = Native(args.library, bad)
            index.lookup("6FG22222+222")
            raise AssertionError("Corrupt index accepted: " + name)
        except RuntimeError as error: assert error.args[0][0] == 2, (name, error)
        finally:
            if index: index.close()
    with args.db.open("rb") as stream: db_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    report = {"passed": True, "json_comparisons": checks, "grid_edge_comparisons": grid_checks, "blocks_compared": len(keys), "corruptions_rejected": list(corruptions), "database_sha256": db_hash, "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(), "elapsed_seconds": round(time.perf_counter() - started, 3)}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)


if __name__ == "__main__": main()
