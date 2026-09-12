from pathlib import Path
import json, os, shlex, shutil, subprocess, sys

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
COMPDB = BUILD / "compile_commands.json"

TOOLS = {
    "infer": shutil.which("infer"),
    "cppcheck": shutil.which("cppcheck"),
    "clang-tidy": shutil.which("run-clang-tidy"),
    "iwyu": shutil.which("include-what-you-use") or shutil.which("iwyu"),
}

NO_BUILD = "--no-build" in sys.argv

def run(*cmd, cwd=None, out=None, check=True):
    subprocess.run(cmd, cwd=cwd, stdout=out, stderr=subprocess.STDOUT, check=check)

if not NO_BUILD:
    BUILD.mkdir(exist_ok=True)

    run(
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_BUILD_TYPE=Debug",
    )

    (ROOT / "compile_commands.json").unlink(missing_ok=True)
    (ROOT / "compile_commands.json").symlink_to(COMPDB)

    if TOOLS["infer"]:
        shutil.rmtree(ROOT / "infer-out", ignore_errors=True)
        run(
            "infer",
            "run",
            "--results-dir",
            str(ROOT / "infer-out"),
            "--",
            "cmake",
            "--build",
            str(BUILD),
        )
    else:
        run("cmake", "--build", str(BUILD))

if TOOLS["clang-tidy"]:
    with open(ROOT / "clang-report.txt", "w") as f:
        run(
            "run-clang-tidy",
            "-p", str(BUILD),
            f"-j{os.cpu_count()}",
            r"^(?!.*build/_deps).*app/.*\.cpp$",
            r"^(?!.*build/_deps).*library/tree/src/.*\.cpp$",
            r"^(?!.*build/_deps).*test/.*\.cpp$",
            out=f,
            check=False,
        )

if TOOLS["cppcheck"]:
    with open(ROOT / "cppcheck-report.txt", "w") as f:
        run(
            "cppcheck",
            f"--project={COMPDB}",
            "--enable=all",
            "--inconclusive",
            "--std=c++20",
            "--check-level=exhaustive",
            "--suppress=unusedFunction",
            "--suppress=missingIncludeSystem",
            "-i", str(BUILD / "_deps"),
            out=f,
            check=False,
        )

if TOOLS["iwyu"]:
    with open(COMPDB) as f, open(ROOT / "iwyu-report.txt", "w") as log:
        for e in json.load(f):
            if "build/_deps" in e["file"]:
                continue
            if not any(x in e["file"] for x in ("app/", "library/tree/src/")):
                continue

            args = e.get("arguments") or shlex.split(e["command"])
            run(
                TOOLS["iwyu"],
                *args[1:],
                e["file"],
                cwd=e["directory"],
                out=log,
                check=False,
            )
