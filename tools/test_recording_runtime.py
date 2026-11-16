"""Regression for successful FFmpeg exits that used to lose buffered output.

Every invocation must pass; no retry masks an empty or incomplete response.
Use both inherited/null stdin and joined/separate output pipes, like our
installer capability check and the native media supervisor respectively.
"""
import argparse
from hashlib import file_digest
import json
import os
from pathlib import Path
import subprocess
import time

from validate_recording_runtime import CAPABILITIES, validate_output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--repeat", type=int, default=20)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.repeat <= 100:
        parser.error("repeat must be in 1..100")
    binary = args.binary.resolve()
    with binary.open("rb") as stream:
        digest = file_digest(stream, "sha256").hexdigest()
    results = []
    for null_input in (False, True):
        for joined in (False, True):
            for iteration in range(args.repeat):
                for category in CAPABILITIES:
                    start = time.monotonic()
                    row = dict(category=category, iteration=iteration,
                               null_input=null_input, joined_output=joined)
                    try:
                        run = subprocess.run(
                            (str(binary), "-hide_banner", "-" + category),
                            env=dict(os.environ, AV_LOG_FORCE_NOCOLOR="1"),
                            stdin=subprocess.DEVNULL if null_input else None,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT if joined else subprocess.PIPE,
                            timeout=20, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                        row.update(returncode=run.returncode, output_bytes=len(run.stdout))
                        run.check_returncode()
                        validate_output(category, run.stdout.decode("utf-8", errors="replace"))
                        row["passed"] = True
                    except (OSError, subprocess.SubprocessError, ValueError) as error:
                        row.update(passed=False, error=str(error))
                    row["seconds"] = round(time.monotonic() - start, 4)
                    results.append(row)
    failed = sum(not row["passed"] for row in results)
    report = dict(binary=str(binary), sha256=digest, runs=len(results),
                  failed=failed, passed=failed == 0, results=results)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("sha256", "runs", "failed", "passed")}))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
