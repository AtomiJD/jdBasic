"""Run all benchmark implementations and print a comparison table.

Usage: python jdb/bench/run_bench.py   (run from project root)
"""
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
JDB = ROOT / "build" / "jdBasic.exe"

MSVC = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
SDK  = r"C:\Program Files (x86)\Windows Kits\10"
SDKV = "10.0.26100.0"
CL   = fr"{MSVC}\bin\Hostx64\x64\cl.exe"
CL_INC = [fr"{MSVC}\include", fr"{SDK}\Include\{SDKV}\ucrt",
          fr"{SDK}\Include\{SDKV}\um",   fr"{SDK}\Include\{SDKV}\shared"]
CL_LIB = [fr"{MSVC}\lib\x64", fr"{SDK}\Lib\{SDKV}\ucrt\x64",
          fr"{SDK}\Lib\{SDKV}\um\x64"]

# (label, command, cwd) tuples per benchmark.
# We measure wall-clock around the actual run, after any pre-build step.
def build_cpp(name):
    """Compile with MSVC cl.exe /O2 (same compiler jdBasic itself uses)."""
    src = BENCH / f"{name}.cpp"
    exe = BENCH / f"{name}_cpp.exe"
    obj = BENCH / f"{name}_cpp.obj"
    cmd = [CL, "/nologo", "/O2", "/std:c++17", "/EHsc"]
    for inc in CL_INC: cmd.append(f"/I{inc}")
    cmd += [str(src), f"/Fe:{exe}", f"/Fo:{obj}", "/link"]
    for lib in CL_LIB: cmd.append(f"/LIBPATH:{lib}")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    obj.unlink(missing_ok=True)
    return exe

def build_jdb_native(name):
    """jdBasic --compile produces <source>.exe next to the .jdb."""
    src = BENCH / f"{name}.jdb"
    out_exe = BENCH / f"{name}.exe"
    # --compile resolves jdb_runtime.obj relative to CWD = project root.
    subprocess.run([str(JDB), "--compile", str(src.relative_to(ROOT))],
                   cwd=str(ROOT), check=True)
    # Native EXE depends on jdbrt.dll — copy next to it.
    rt = ROOT / "build" / "jdbrt.dll"
    target_rt = BENCH / "jdbrt.dll"
    if rt.exists() and not target_rt.exists():
        target_rt.write_bytes(rt.read_bytes())
    return out_exe

def time_run(label, argv, cwd=None):
    """Run argv, return (label, elapsed_s, last_stdout_line)."""
    t0 = time.perf_counter()
    proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    elapsed = time.perf_counter() - t0
    if proc.returncode != 0:
        print(f"  [FAIL] {label}: rc={proc.returncode}\n{proc.stderr[:400]}")
        return (label, None, "ERROR")
    out = proc.stdout.strip().splitlines()
    last = out[-1] if out else ""
    return (label, elapsed, last)

def fmt_row(label, secs, result, baseline):
    if secs is None:
        return f"  {label:<22} {'ERROR':>10}  {'-':>8}  {result}"
    rel = secs / baseline if baseline else 1.0
    return f"  {label:<22} {secs*1000:>8.1f} ms  {rel:>6.1f}x  {result}"

def run_benchmark(name):
    print(f"\n=== {name} ===")
    print("Building...")
    cpp_exe = build_cpp(name)
    jdb_exe = build_jdb_native(name)

    runs = [
        ("jdBasic interpreter",  [str(JDB), str(BENCH / f"{name}.jdb")], str(ROOT)),
        ("jdBasic --compile",    [str(jdb_exe)],                          str(BENCH)),
        ("C++ (g++ -O2)",        [str(cpp_exe)],                          str(BENCH)),
        ("Python (CPython)",     [sys.executable, str(BENCH / f"{name}.py")], str(BENCH)),
        ("Python (Numba LLVM)",  [sys.executable, str(BENCH / f"{name}_numba.py")], str(BENCH)),
    ]

    results = []
    for label, argv, cwd in runs:
        print(f"  Running {label}...", flush=True)
        results.append(time_run(label, argv, cwd))

    baseline = next((s for l, s, _ in results if l == "C++ (g++ -O2)" and s), None)
    print()
    print(f"  {'Implementation':<22} {'Time':>11}  {'vs C++':>7}  Result")
    print(f"  {'-'*22} {'-'*11}  {'-'*7}  {'-'*30}")
    for label, secs, res in results:
        print(fmt_row(label, secs, res, baseline))
    return results

if __name__ == "__main__":
    if not JDB.exists():
        sys.exit(f"jdBasic.exe not found at {JDB}; build first.")
    run_benchmark("bench_pi")
    run_benchmark("bench_mandel")
