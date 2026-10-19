#!/usr/bin/env python3
"""Builds each Ovis program given and prints its peak memory and time.

    tools/peak_memory.py build/ovis eval/memory/*.ovis

The peak is the child's maximum resident set (ru_maxrss), in MB. Used by
proposals/memory.md's plan (E1) to record the baseline and what each step
saves.
"""
import os
import resource
import subprocess
import sys
import tempfile
import time


def main():
    ovis, programs = sys.argv[1], sys.argv[2:]
    extra = os.environ.get("OVIS_MEASURE_FLAGS", "").split()
    print(f"{'program':<24}{'peak MB':>10}{'seconds':>10}")
    for program in programs:
        with tempfile.TemporaryDirectory() as folder:
            exe = os.path.join(folder, "program")
            subprocess.run([ovis, "build", program, "-o", exe, "--offline",
                            "--keep-source", "--quiet", *extra], check=True)
            before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
            start = time.time()
            child = subprocess.Popen([exe], stdout=subprocess.DEVNULL)
            _, status, usage = os.wait4(child.pid, 0)
            seconds = time.time() - start
            child.returncode = os.waitstatus_to_exitcode(status)
            peak = usage.ru_maxrss / 1024
            name = os.path.basename(program).removesuffix(".ovis")
            note = "" if child.returncode == 0 else f"  (exit {child.returncode})"
            print(f"{name:<24}{peak:>10.1f}{seconds:>10.2f}{note}")


if __name__ == "__main__":
    main()
