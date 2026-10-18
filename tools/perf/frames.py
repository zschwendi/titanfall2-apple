#!/usr/bin/env python3
"""Frame times from a GATE3_FRAMES file (one line per Present: "<ms since run start> <0|1>").

    frames.py FILE [--from S] [--to S] [--mission] [--all]
    frames.py selftest

A frame's time is its timestamp minus the previous line's timestamp (the first line has none).
By default only lines with flag 1 are counted; --all counts every line.  With --mission, --from
and --to count from the first line with flag 1 (the mission's first frame), not from the run's start.  --from and --to (seconds
since the run's start, inclusive) select frames by their own timestamp.
"""
import io
import json
import os
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout


def read_lines(path):
    """Return [(ms, flag)] for every non-empty line of the file."""
    rows = []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            parts = raw.split()
            if not parts:
                continue
            if len(parts) != 2:
                raise ValueError(f"{path}:{lineno}: expected 'ms flag', got {raw.strip()!r}")
            rows.append((float(parts[0]), int(parts[1])))
    return rows


def frame_times(rows, count_all, t_from, t_to):
    """Return the frame times in ms of the selected lines (the first line has none)."""
    out = []
    for i in range(1, len(rows)):
        ms, flag = rows[i]
        if not count_all and flag != 1:
            continue
        if t_from is not None and ms / 1000.0 < t_from:
            continue
        if t_to is not None and ms / 1000.0 > t_to:
            continue
        out.append(ms - rows[i - 1][0])
    return out


def nearest_rank(sorted_ms, num, den):
    """Nearest-rank percentile num/den: index ceil(q*n)-1, with integer arithmetic."""
    n = len(sorted_ms)
    k = -(-num * n // den)  # ceil(q*n) without float error
    return sorted_ms[max(k, 1) - 1]


def low_fps(sorted_ms, num, den):
    """1000 / mean frame time of the slowest num/den of frames (at least one frame)."""
    n = len(sorted_ms)
    k = max(1, -(-num * n // den))
    slow = sorted_ms[n - k:]
    return 1000.0 * k / sum(slow)


def summarize(times):
    n = len(times)
    s = sorted(times)
    total_ms = sum(s)
    seconds = total_ms / 1000.0
    result = {
        "frames": n,
        "seconds": round(seconds, 3),
        "avg_fps": round(n / seconds, 3) if seconds > 0 else 0.0,
        "median_ms": round(nearest_rank(s, 1, 2), 3),
        "p95_ms": round(nearest_rank(s, 95, 100), 3),
        "p99_ms": round(nearest_rank(s, 99, 100), 3),
        "p999_ms": round(nearest_rank(s, 999, 1000), 3),
        "max_ms": round(s[-1], 3),
        "low1_fps": round(low_fps(s, 1, 100), 3),
        "low01_fps": round(low_fps(s, 1, 1000), 3),
        "over_16_7": sum(1 for t in s if t > 16.7),
        "over_33_3": sum(1 for t in s if t > 33.3),
        "over_100": sum(1 for t in s if t > 100),
    }
    return result


def run_file(path, count_all=False, t_from=None, t_to=None):
    """Return (status, output text)."""
    rows = read_lines(path)
    times = frame_times(rows, count_all, t_from, t_to)
    if not times:
        sys.stderr.write(f"frames: no frame counted in {path}\n")
        return 1, ""
    return 0, json.dumps(summarize(times)) + "\n"


MISSION_RELATIVE = [False]      # --mission: --from and --to count from the first line with flag 1


def parse_args(argv):
    MISSION_RELATIVE[0] = False
    path = None
    count_all = False
    t_from = t_to = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--all":
            count_all = True
        elif a == "--mission":
            MISSION_RELATIVE[0] = True
        elif a in ("--from", "--to"):
            i += 1
            if i >= len(argv):
                raise ValueError(f"{a} needs a value in seconds")
            try:
                v = float(argv[i])
            except ValueError:
                raise ValueError(f"{a} needs a number of seconds, got {argv[i]!r}")
            if a == "--from":
                t_from = v
            else:
                t_to = v
        elif a.startswith("--"):
            raise ValueError(f"unknown option {a}")
        elif path is None:
            path = a
        else:
            raise ValueError(f"unexpected argument {a!r}")
        i += 1
    if path is None:
        raise ValueError("usage: frames.py FILE [--from S] [--to S] [--all] | frames.py selftest")
    return path, count_all, t_from, t_to


def selftest():
    checks = [0]

    def check(name, cond):
        if not cond:
            print(f"frames selftest: FAILED: {name}")
            return False
        checks[0] += 1
        return True

    def write(d, name, rows):
        p = os.path.join(d, name)
        with open(p, "w", encoding="utf-8") as f:
            for ms, flag in rows:
                f.write(f"{ms:.3f} {flag}\n")
        return p

    def run(argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = main(argv)
        return status, out.getvalue(), err.getvalue()

    with tempfile.TemporaryDirectory() as d:
        # 1,000 frames of 10 ms: the first line at 0 ms, then 1000 steps of 10 ms.
        steady = write(d, "steady.txt", [(10.0 * i, 1) for i in range(1001)])
        st, out, _ = run([steady])
        j = json.loads(out) if st == 0 else {}
        if not check("steady: exit 0", st == 0): return 1
        if not check("steady: frames 1000", j.get("frames") == 1000): return _fail(checks, "steady frames")
        if not check("steady: avg_fps 100", j.get("avg_fps") == 100): return _fail(checks, "steady avg_fps")
        if not check("steady: median_ms 10", j.get("median_ms") == 10): return _fail(checks, "steady median_ms")
        if not check("steady: low1_fps 100", j.get("low1_fps") == 100): return _fail(checks, "steady low1_fps")
        if not check("steady: one line of output", out.count("\n") == 1): return _fail(checks, "steady one line")

        # 990 frames of 10 ms and 10 of 50 ms (the slow ones last).
        rows = [(0.0, 1)]
        t = 0.0
        for k in range(1000):
            t += 50.0 if k >= 990 else 10.0
            rows.append((t, 1))
        mixed = write(d, "mixed.txt", rows)
        st, out, _ = run([mixed])
        j = json.loads(out) if st == 0 else {}
        if not check("mixed: exit 0", st == 0): return 1
        if not check("mixed: p99_ms 10", j.get("p99_ms") == 10): return _fail(checks, "mixed p99_ms")
        if not check("mixed: p999_ms 50", j.get("p999_ms") == 50): return _fail(checks, "mixed p999_ms")
        if not check("mixed: max_ms 50", j.get("max_ms") == 50): return _fail(checks, "mixed max_ms")
        if not check("mixed: low1_fps 20", j.get("low1_fps") == 20): return _fail(checks, "mixed low1_fps")
        if not check("mixed: over_33_3 10", j.get("over_33_3") == 10): return _fail(checks, "mixed over_33_3")
        if not check("mixed: over_16_7 10", j.get("over_16_7") == 10): return _fail(checks, "mixed over_16_7")
        if not check("mixed: over_100 0", j.get("over_100") == 0): return _fail(checks, "mixed over_100")

        # Flag 0 lines are left out by default and counted with --all.
        rows = [(0.0, 1)] + [(10.0 * i, 1 if i % 2 else 0) for i in range(1, 11)]
        flags = write(d, "flags.txt", rows)
        st, out, _ = run([flags])
        j = json.loads(out) if st == 0 else {}
        if not check("flags: default counts flag 1 only", st == 0 and j.get("frames") == 5): return _fail(checks, "flags default")
        st, out, _ = run([flags, "--all"])
        j = json.loads(out) if st == 0 else {}
        if not check("flags: --all counts every line after the first", st == 0 and j.get("frames") == 10): return _fail(checks, "flags --all")

        # --from/--to select by the frame's own timestamp (seconds).
        # Lines at 0, 1000, 2000, ... 9000 ms: frames at 1000..9000 ms, each 1000 ms.
        spaced = write(d, "spaced.txt", [(1000.0 * i, 1) for i in range(10)])
        st, out, _ = run([spaced, "--from", "3", "--to", "5"])
        j = json.loads(out) if st == 0 else {}
        if not check("range: --from 3 --to 5 selects 3 frames", st == 0 and j.get("frames") == 3): return _fail(checks, "range both")
        st, out, _ = run([spaced, "--from", "8"])
        j = json.loads(out) if st == 0 else {}
        if not check("range: --from 8 selects 2 frames", st == 0 and j.get("frames") == 2): return _fail(checks, "range from")
        st, out, _ = run([spaced, "--to", "2"])
        j = json.loads(out) if st == 0 else {}
        if not check("range: --to 2 selects 2 frames", st == 0 and j.get("frames") == 2): return _fail(checks, "range to")

        # An empty selection exits 1 with one line on standard error.
        st, out, err = run([spaced, "--from", "100"])
        if not check("empty: exit 1", st == 1): return _fail(checks, "empty status")
        if not check("empty: nothing on stdout", out == ""): return _fail(checks, "empty stdout")
        if not check("empty: one line on stderr", err.count("\n") == 1): return _fail(checks, "empty stderr")

        # A single line has no frame.
        one = write(d, "one.txt", [(0.0, 1)])
        st, _, _ = run([one, "--all"])
        if not check("one line: exit 1", st == 1): return _fail(checks, "one line")

        # Nearest rank on a small file: 4 frames 10, 20, 30, 40 ms.
        small = write(d, "small.txt", [(0.0, 1), (10.0, 1), (30.0, 1), (60.0, 1), (100.0, 1)])
        st, out, _ = run([small])
        j = json.loads(out) if st == 0 else {}
        if not check("small: median_ms 20 (ceil(2)-1 = index 1)", j.get("median_ms") == 20): return _fail(checks, "small median")
        if not check("small: p95_ms 40", j.get("p95_ms") == 40): return _fail(checks, "small p95")
        if not check("small: low1_fps 1000/40", j.get("low1_fps") == 25): return _fail(checks, "small low1")

    print(f"frames selftest: {checks[0]} checks passed")
    return 0


def _fail(checks, name):
    return 1


def main(argv):
    try:
        if argv == ["selftest"]:
            return selftest()
        path, count_all, t_from, t_to = parse_args(argv)
        if MISSION_RELATIVE[0]:
            first = next((ms for ms, flag in read_lines(path) if flag == 1), None)
            if first is None:
                raise ValueError("--mission: the file has no line with flag 1")
            t_from = first / 1000.0 + (t_from or 0.0)
            t_to = first / 1000.0 + t_to if t_to is not None else None
        status, text = run_file(path, count_all, t_from, t_to)
    except SystemExit as e:
        return int(e.code or 0)
    except (OSError, ValueError) as e:
        sys.stderr.write(f"frames: {e}\n")
        return 1
    sys.stdout.write(text)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
