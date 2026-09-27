"""Inspect delivered ELF exports, dependencies, segment alignment, and JAR bytecode."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parent
C_EXPORTS = ['pcad_abi_version', 'pcad_version', 'pcad_open', 'pcad_lookup_code',
             'pcad_lookup_latlng', 'pcad_metadata', 'pcad_cache_stats',
             'pcad_prefetch_nearby', 'pcad_clear_cache', 'pcad_close', 'pcad_free']
JNI_EXPORTS = ['Java_com_askcodex_pluscode_PlusCodeIndex_native' + suffix for suffix in
               ['Open', 'LookupCode', 'LookupLatLng', 'Metadata', 'CacheStats',
                'PrefetchNearby', 'ClearCache', 'Close']]


def inspect(path, machine, bits, android):
    data = path.read_bytes()
    assert data[:4] == b'\x7fELF' and data[5] == 1, path
    assert data[4] == (2 if bits == 64 else 1), path
    assert struct.unpack_from('<H', data, 18)[0] == machine, path
    if bits == 64:
        offset = struct.unpack_from('<Q', data, 32)[0]
        stride, count = struct.unpack_from('<HH', data, 54)
        fmt = '<IIQQQQQQ'
    else:
        offset = struct.unpack_from('<I', data, 28)[0]
        stride, count = struct.unpack_from('<HH', data, 42)
        fmt = '<IIIIIIII'
    alignments = []
    for i in range(count):
        fields = struct.unpack_from(fmt, data, offset + i * stride)
        if fields[0] == 1:
            alignment = fields[-1]
            assert alignment >= (16384 if android and bits == 64 else 4096), path
            alignments.append(alignment)
    assert alignments
    symbols = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(path)], text=True)
    defined = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) >= 8 and line.split()[6] != 'UND'}
    assert set(C_EXPORTS + JNI_EXPORTS) <= defined, (path, set(C_EXPORTS + JNI_EXPORTS) - defined)
    dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
    needed = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
    allowed = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so'} if android else {
        'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'ld-linux-x86-64.so.2'}
    assert set(needed) <= allowed, (path, needed)
    versions = sorted({x.decode() for x in re.findall(rb'GLIBC_\d+\.\d+(?:\.\d+)?', data)},
                      key=lambda x: tuple(map(int, x[6:].split('.'))))
    if versions and not android:
        assert tuple(map(int, versions[-1][6:].split('.'))) <= (2, 28), versions
    return {'path': str(path.relative_to(ROOT)), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'machine': machine,
            'bits': bits, 'load_alignments': alignments, 'needed': needed,
            'glibc_versions': versions, 'required_exports_verified': len(C_EXPORTS + JNI_EXPORTS)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=REPOSITORY / 'sdk')
    args = parser.parse_args()
    checks = [inspect(args.sdk / 'linux-x86_64/lib/libpluscode_admin.so', 62, 64, False)]
    for abi, machine, bits in [('arm64-v8a', 183, 64), ('x86_64', 62, 64), ('armeabi-v7a', 40, 32)]:
        checks.append(inspect(args.sdk / f'android/jniLibs/{abi}/libpluscode_admin.so', machine, bits, True))
    with zipfile.ZipFile(args.sdk / 'java/pluscode-admin-1.0.0.jar') as archive:
        classes = {name: struct.unpack_from('>H', archive.read(name), 6)[0]
                   for name in archive.namelist() if name.endswith('.class')}
        assert classes and all(version == 52 for version in classes.values()), classes
        assert 'META-INF/proguard/pluscode-admin.pro' in archive.namelist()
    result = {'passed': True, 'elf': checks, 'java_class_versions': classes,
              'android_device_runtime_tested': False}
    out = REPOSITORY / 'validation/native/binaries.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
