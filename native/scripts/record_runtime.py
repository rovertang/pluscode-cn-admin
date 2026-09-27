"""Record successful validate.sh completion against the exact tested artifacts."""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sdk, out = Path(sys.argv[1]), Path(sys.argv[2])
artifacts = [sdk / 'java/pluscode-admin-1.0.0.jar', sdk / 'linux-x86_64/lib/libpluscode_admin.so']
report = {'passed': True, 'platform': platform.platform(), 'libc': platform.libc_ver(),
          'java': subprocess.check_output([str(Path(os.environ['JAVA_HOME']) / 'bin/java'), '-version'], stderr=subprocess.STDOUT, text=True),
          'native_contract': True, 'java_xcheck_jni': True, 'external_gcc_cpp': True,
          'external_gcc_c': True, 'kotlin_compiled_and_executed': bool(sys.argv[3]),
          'tested_artifacts': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in artifacts}}
out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
