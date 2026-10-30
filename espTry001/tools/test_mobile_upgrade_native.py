"""Exercise production FAT import and YMODEM sender against a fake boot peer."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / "tools/native_upgrade"

if __name__ == "__main__":
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ not found")
    with tempfile.TemporaryDirectory(prefix="mobile_upgrade_") as folder:
        exe = Path(folder) / "test_upgrade.exe"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{SUPPORT}", f"-I{ROOT / 'src'}",
                        str(ROOT / "src/services/mobile_upgrade.cpp"),
                        str(SUPPORT / "test_mobile_upgrade.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
