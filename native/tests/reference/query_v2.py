"""Pure KV Plus Code 2.0 lookup, independent of GIS libraries."""

import argparse
from functools import lru_cache
import json
from pathlib import Path
import sqlite3

from openlocationcode import openlocationcode as olc
from codec_v2 import BRANCH, BOUNDARY, FORMAT, LABEL_MASK, OVERLAP, child_index, summarize, unpack_directory, open_tree


class KVLookup:
    def __init__(self, get_tile, roots, admins, cache_size=256):
        self.get_tile = get_tile
        self.roots = roots
        self.admins = {int(i): v for i, v in admins.items()}
        self._tile = lru_cache(maxsize=cache_size)(self._read_tile)

    def _read_tile(self, key):
        blob = self.get_tile(key)
        if blob is None:
            raise ValueError(f"Index is corrupt: required tile {key} is absent")
        return open_tree(blob)

    def lookup(self, code):
        code = code.strip().upper()
        if not olc.isFull(code):
            raise ValueError("A valid full Plus Code including '+' is required")
        raw = code.replace("+", "").rstrip("0")
        length = len(raw)
        if length < 8:
            raise ValueError("At least 8 significant characters are required")
        root = self.roots.get(raw[:4], 0)
        level, node = 4, root
        if not isinstance(root, int):
            node = root[child_index(raw, 4)]
            level = 6
            if node == BRANCH:
                node = self._tile(raw[:6])
        while not isinstance(node, int) and level < min(length, 11):
            i = child_index(raw, level)
            values, children = node
            node = children[i] if values[i] == BRANCH else values[i]
            level = {6: 8, 8: 10, 10: 11}[level]
        if not isinstance(node, int):
            labels, flags = summarize(node)
            # Even identical center samples cannot prove uniformity of a branched area.
            return {"pluscode": code, "precision": min(length, 11), "max_precision": 11,
                    "status": "ambiguous", "admin": None, "matched_length": None,
                    "boundary_cell": True, "source_overlap": bool(flags & OVERLAP),
                    "sampled_candidates": [self.admins[i] for i in sorted(labels - {0})],
                    "includes_uncovered_samples": 0 in labels,
                    "candidate_semantics": "adaptive certified leaves and 11-digit centers; not exhaustive polygon intersections"}
        label = node & LABEL_MASK
        leaf = raw[:level]
        leaf_code = leaf[:8] + "+" + leaf[8:] if level >= 8 else leaf.ljust(8, "0") + "+"
        return {"pluscode": code, "precision": min(length, 11), "max_precision": 11,
                "status": "matched" if label else "outside_coverage", "admin": self.admins.get(label),
                "matched_length": level, "matched_pluscode": leaf_code,
                "boundary_cell": bool(node & BOUNDARY), "source_overlap": bool(node & OVERLAP),
                "assignment": "certified_full_cell" if level < 11 else "11_digit_center"}

    def lookup_latlng(self, latitude, longitude):
        if not (-90 <= latitude <= 90 and -180 <= longitude <= 180):
            raise ValueError("Finite WGS84 latitude/longitude required")
        return self.lookup(olc.encode(latitude, longitude, 11))

    def close(self):
        self._tile.cache_clear()


class PlusCodeKV(KVLookup):
    def __init__(self, database, cache_size=256):
        self.connection = sqlite3.connect(Path(database).resolve().as_uri() + "?mode=ro", uri=True)
        self.metadata = {k: json.loads(v) for k, v in self.connection.execute("SELECT key,value FROM metadata")}
        if self.metadata.get("format") != FORMAT or not self.metadata.get("complete"):
            self.connection.close()
            raise ValueError("Incomplete or incompatible v2 index")
        roots = {}
        for key, owner, blob in self.connection.execute("SELECT key,owner,value FROM roots"):
            roots[key] = owner if owner is not None else unpack_directory(blob)
        admins = {i: json.loads(v) for i, v in self.connection.execute("SELECT id,value FROM admins")}
        super().__init__(self._get_blob, roots, admins, cache_size)

    def _get_blob(self, key):
        row = self.connection.execute("SELECT value FROM tiles WHERE key=?", (key,)).fetchone()
        return row[0] if row else None

    def close(self):
        super().close()
        self.connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    repository = Path(__file__).resolve().parents[3]
    parser.add_argument("--db", type=Path, default=repository / "data" / "processed" / "v2" / "pluscode_admin_v2.sqlite")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--code")
    group.add_argument("--latlng", type=float, nargs=2)
    args = parser.parse_args()
    index = PlusCodeKV(args.db)
    try:
        print(json.dumps(index.lookup(args.code) if args.code else index.lookup_latlng(*args.latlng), ensure_ascii=False, indent=2))
    finally:
        index.close()
