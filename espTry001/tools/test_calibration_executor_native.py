#!/usr/bin/env python3
"""Compile and run the real CalibrationExecutor against a fake serial peer."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
SUPPORT = ROOT / "tools" / "native_calibration"


def main() -> int:
    compiler = shutil.which("g++")
    if compiler is None:
        print("g++ not found", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="calibration_executor_") as temp:
        executable = pathlib.Path(temp) / "test_calibration_executor.exe"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{SUPPORT}",
            f"-I{ROOT / 'src'}",
            str(ROOT / "src" / "calibration_executor.cpp"),
            str(SUPPORT / "test_calibration_executor.cpp"),
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True)
        return subprocess.run([str(executable)], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
