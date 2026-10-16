"""Build and run the production CDC validation logic on the host."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ not found")
    with tempfile.TemporaryDirectory(prefix="cdc_native_") as folder:
        executable = Path(folder) / "test_cdc.exe"
        subprocess.run([compiler, "-std=c++17",
                        "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'src'}",
                        str(ROOT / "tools/native_cdc/test_line_coding.cpp"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        support = ROOT / "tools/native_cdc/transport"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{support}", f"-I{ROOT / 'src'}",
                        str(ROOT / "src/usb/usb_cdc_stream.cpp"),
                        str(support / "test_usb_cdc_stream.cpp"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{support}", f"-I{ROOT / 'src'}",
                        str(ROOT / "src/ui/ui_input.cpp"),
                        str(support / "test_download_action.cpp"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    main()
