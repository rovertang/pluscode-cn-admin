"""Package the verified native SDK, sources, V2 database, and usage docs."""

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import tarfile
import zipfile


NATIVE = Path(__file__).resolve().parents[1]
REPOSITORY = NATIVE.parent
SDK = REPOSITORY / "sdk"
REPORTS = REPOSITORY / "validation" / "native"
RELEASES = REPOSITORY / "releases"
DATABASE = REPOSITORY / "data" / "processed" / "v2" / "pluscode_admin_v2.sqlite"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def notices(tools):
    lock = json.loads((NATIVE / "source/dependencies.lock.json").read_text())
    parts = [
        "PlusCode Admin Native SDK - Third Party Notices\n"
        "Pinned source archives are stored under native/source/.\n"
        "SQLite amalgamation: public domain; see its source header.\n"
    ]
    for name, license_file in [("xz", "COPYING.0BSD"), ("zlib", "LICENSE"), ("olc", "LICENSE")]:
        item = lock[name]
        with tarfile.open(NATIVE / "source" / item["archive"]) as archive:
            text = archive.extractfile(item["root"] + "/" + license_file).read().decode()
        parts.append(f"\n===== {name} / {item['url']} =====\n{text}")
    parts.append("\n===== nlohmann/json 3.12.0 =====\n" + (NATIVE / "source/json-LICENSE.MIT").read_text())
    for name in [
        "zig/lib/libcxx/LICENSE.TXT",
        "zig/lib/libcxxabi/LICENSE.TXT",
        "zig/lib/libunwind/LICENSE.TXT",
        "ndk/android-ndk-r29/NOTICE",
        "ndk/android-ndk-r29/NOTICE.toolchain",
    ]:
        path = tools / name
        if path.is_file():
            parts.append(f"\n===== Build runtime notice: {name} =====\n{path.read_text()}")
    (SDK / "THIRD_PARTY_NOTICES.txt").write_text("\n".join(parts), encoding="utf-8")


def release_files():
    selected = []
    for root in [NATIVE, SDK, REPORTS]:
        selected.extend(path for path in root.rglob("*") if path.is_file() and "__pycache__" not in path.parts)
    selected.extend([DATABASE, REPOSITORY / "README.md"])
    return sorted(set(selected))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, help="Extracted release toolchain used to regenerate notices")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    RELEASES.mkdir(parents=True, exist_ok=True)
    manifest_path = RELEASES / "pluscode-admin-1.0.0-manifest.json"
    if args.verify_only:
        manifest = json.loads(manifest_path.read_text())
        for name, item in manifest["files"].items():
            path = REPOSITORY / name
            assert path.stat().st_size == item["bytes"] and digest(path) == item["sha256"], name
        print(json.dumps({"verified_files": len(manifest["files"]), "passed": True}))
        return
    if args.tools:
        notices(args.tools)
    for name in ["binaries.json", "differential.json", "runtime.json"]:
        report = json.loads((REPORTS / name).read_text())
        assert report["passed"], name
    assert digest(DATABASE) == "9abaed308d8d1286fd08fe6ab1517d5345198892a5873dfeb2dbd34b0687fe4f"
    files = release_files()
    records = {
        path.relative_to(REPOSITORY).as_posix(): {"bytes": path.stat().st_size, "sha256": digest(path)}
        for path in files
    }
    manifest = {
        "version": "1.0.0",
        "created_at": datetime.now().astimezone().isoformat(),
        "files": records,
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    bundle = RELEASES / "pluscode-admin-1.0.0-complete.zip"
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as archive:
        for name in records:
            archive.write(REPOSITORY / name, name)
        archive.write(manifest_path, manifest_path.relative_to(REPOSITORY).as_posix())
        assert archive.testzip() is None
    (RELEASES / "SHA256SUMS.txt").write_text(f"{digest(bundle)}  {bundle.name}\n", encoding="ascii")
    print(json.dumps({"bundle": str(bundle), "bytes": bundle.stat().st_size, "files": len(records), "passed": True}))


if __name__ == "__main__":
    main()
