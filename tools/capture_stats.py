#!/usr/bin/env python3
"""Sensing-layer stats for an FDLOG capture: the numbers Phase A is judged on.

Reports the module's active thresholds (from the INVOKE config echo), the detection rate, the
score histogram, frame spacing, and the blackouts -- multi-second stretches with no person box,
which is what actually hid the falls in the Sep 16 captures.

Usage:
    python3 tools/capture_stats.py monitor.log
    python3 tools/capture_stats.py old.log new.log      # side-by-side deltas

See plan/fall_detection_sensing_fix.md for what these numbers mean.
"""

import re
import sys
from collections import Counter

ANSI = re.compile(r"\x1b\[[0-9;]*m")
FDLOG = re.compile(r"FDLOG,\d+,(\d+),(\d+),(.*?)\s*$")
BOX = re.compile(r"\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]")
CONFIG = re.compile(r'"config":\s*\{([^}]*)\}')

# Aspect ratio (w/h) at or above this reads as "on the ground" rather than standing.
PRONE_AR = 1.0


class Capture:
    def __init__(self, path):
        self.path = path
        with open(path, errors="replace") as fh:
            text = ANSI.sub("", fh.read())

        self.polls = len(re.findall(r"FDPOLL", text))
        self.configs = Counter(m.group(1).strip() for m in CONFIG.finditer(text))
        self.overflows = 0
        self.frames = []  # (ms, [(x, y, w, h, score), ...])

        for line in text.splitlines():
            m = FDLOG.search(line)
            if not m:
                continue
            ms, overflow, payload = int(m.group(1)), int(m.group(2)), m.group(3)
            if overflow:
                self.overflows += 1
            if '"boxes"' not in payload:
                continue
            boxes = [
                (int(x), int(y), int(w), int(h), int(sc))
                for x, y, w, h, sc, tg in BOX.findall(payload)
                if int(tg) == 0 and int(w) > 0 and int(h) > 0
            ]
            self.frames.append((ms, boxes))

    @property
    def with_box(self):
        return [f for f in self.frames if f[1]]

    @property
    def scores(self):
        return [b[4] for _, boxes in self.frames for b in boxes]

    def blackouts(self, min_ms=1000):
        times = [ms for ms, _ in self.with_box]
        return [(times[i], times[i + 1], times[i + 1] - times[i])
                for i in range(len(times) - 1) if times[i + 1] - times[i] > min_ms]

    def report(self):
        print(f"=== {self.path} ===")
        if not self.frames:
            print("  no detection frames found\n")
            return

        print("  module config echo :", ", ".join(
            f"{{{cfg}}} x{n}" for cfg, n in self.configs.most_common()) or "(none)")

        span = self.frames[-1][0] - self.frames[0][0]
        rate = 100.0 * len(self.with_box) / len(self.frames)
        print(f"  polls / frames     : {self.polls} / {len(self.frames)}"
              f"{'  (mismatch!)' if self.polls and abs(self.polls - len(self.frames)) > 2 else ''}")
        print(f"  frames with a box  : {len(self.with_box)}  ({rate:.1f}%)")
        print(f"  span / frame rate  : {span / 1000.0:.1f}s  /  "
              f"{1000.0 * len(self.frames) / span if span else 0:.2f} fps")
        if self.overflows:
            print(f"  !! line overflows  : {self.overflows}  (UART_BUF_SIZE too small -- data lost)")

        sc = self.scores
        if sc:
            print(f"  score min/mean/max : {min(sc)} / {sum(sc) / len(sc):.1f} / {max(sc)}")
            print("  histogram:")
            hist = Counter(sc)
            widest = max(hist.values())
            for s in sorted(hist):
                bar = "#" * max(1, int(40 * hist[s] / widest))
                print(f"      {s:3d} | {bar} {hist[s]}")
            floor = min(sc)
            print(f"  -> score floor is {floor}. If this equals the module's tscore, the"
                  f" distribution is being clipped, not falling off naturally.")

            upright = [b[4] for _, bs in self.frames for b in bs if b[2] / b[3] < PRONE_AR]
            prone = [b[4] for _, bs in self.frames for b in bs if b[2] / b[3] >= PRONE_AR]
            if upright and prone:
                print(f"  upright n={len(upright)} mean={sum(upright) / len(upright):.1f}   "
                      f"prone n={len(prone)} mean={sum(prone) / len(prone):.1f}")
            elif upright:
                print(f"  upright n={len(upright)} mean={sum(upright) / len(upright):.1f}   prone n=0")

        gaps = sorted(self.frames[i + 1][0] - self.frames[i][0] for i in range(len(self.frames) - 1))
        if gaps:
            q = lambda p: gaps[min(len(gaps) - 1, int(p * len(gaps)))]
            print(f"  frame gaps (ms)    : min={gaps[0]} p25={q(.25)} median={q(.5)} "
                  f"p75={q(.75)} p95={q(.95)} max={gaps[-1]}")

        bl = self.blackouts()
        print(f"  blackouts >1s      : {len(bl)}")
        for start, end, gap in bl:
            print(f"      {start:6d} -> {end:6d}   {gap / 1000.0:.2f}s")
        print()


def main():
    paths = sys.argv[1:] or ["monitor.log"]
    caps = []
    for p in paths:
        c = Capture(p)
        c.report()
        caps.append(c)

    if len(caps) > 1:
        print("=== delta ===")
        base = caps[0]
        for c in caps[1:]:
            br = 100.0 * len(base.with_box) / len(base.frames) if base.frames else 0
            cr = 100.0 * len(c.with_box) / len(c.frames) if c.frames else 0
            print(f"  detection rate : {br:.1f}%  ->  {cr:.1f}%   ({cr - br:+.1f} pts)")
            bmin = min(base.scores) if base.scores else None
            cmin = min(c.scores) if c.scores else None
            print(f"  score floor    : {bmin}  ->  {cmin}"
                  f"{'   (UNCHANGED -- the threshold did not take)' if bmin == cmin else ''}")
            bb, cb = base.blackouts(), c.blackouts()
            print(f"  blackouts >1s  : {len(bb)}  ->  {len(cb)}")
            if bb and cb:
                print(f"  worst blackout : {max(g for _, _, g in bb) / 1000.0:.2f}s  ->  "
                      f"{max(g for _, _, g in cb) / 1000.0:.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
