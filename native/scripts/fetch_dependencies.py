"""Fetch pinned upstream sources; builds use the resulting offline archives."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OLC = "83986da0156bbf51fba33d0327d8ca4b7f955c89"
SOURCES = {
    "sqlite": ("https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip", "sqlite-amalgamation-3530400.zip", "sqlite-amalgamation-3530400"),
    "xz": ("https://github.com/tukaani-project/xz/releases/download/v5.8.3/xz-5.8.3.tar.gz", "xz-5.8.3.tar.gz", "xz-5.8.3"),
    "zlib": ("https://zlib.net/zlib-1.3.2.tar.gz", "zlib-1.3.2.tar.gz", "zlib-1.3.2"),
    "olc": (f"https://codeload.github.com/google/open-location-code/tar.gz/{OLC}", f"olc-{OLC}.tar.gz", f"open-location-code-{OLC}"),
    "json": ("https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip", "json-3.12.0.zip", "include"),
}


def download(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "pluscode-admin-native-build"})
        partial = path.with_name(path.name + ".part")
        with urllib.request.urlopen(request, timeout=120) as response, partial.open("wb") as stream:
            while chunk := response.read(1024 * 1024):
                stream.write(chunk)
        partial.replace(path)
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", action="store_true", help="Maintainer-only: record hashes after upstream review")
    args = parser.parse_args()
    lock_path = ROOT / "source" / "dependencies.lock.json"
    lock = {} if args.record else json.loads(lock_path.read_text(encoding="utf-8"))
    for name, (url, filename, folder) in SOURCES.items():
        digest = download(url, ROOT / "source" / filename)
        if not args.record and digest != lock[name]["sha256"]:
            raise ValueError(f"Hash mismatch: {name}")
        lock[name] = {"url": url, "archive": filename, "root": folder, "sha256": digest}
        print(json.dumps({"dependency": name, "sha256": digest}), flush=True)
    if args.record:
        lock_path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
