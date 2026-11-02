#!/usr/bin/env python3
"""Reads a capture from a FD_PROBE_COMMANDS build and reports which AT commands the Grove Vision
module actually implements.

The firmware emits a marker line before each probe:

    FDPROBE,<idx>,<CR|CRLF>,<command>

and every FDLOG line after it, up to the next marker, is that command's reply. This script pairs
them up and classifies each reply by the module's own "code" field (0 = accepted, 5 = EINVAL /
unknown command -- see sscma_client_commands.h's sscma_client_error_t).

Usage:
    python3 tools/probe_report.py monitor.log
"""

import json
import re
import sys

# sscma_client_error_t, managed_components/wvirgil123__sscma_client/include/sscma_client_commands.h
ERRNAMES = {
    0: "OK", 1: "AGAIN", 2: "ELOG", 3: "ETIMEDOUT", 4: "EIO", 5: "EINVAL",
    6: "ENOMEM", 7: "EBUSY", 8: "ENOTSUP", 9: "EPERM", 10: "EUNKNOWN",
}

ANSI = re.compile(r"\x1b\[[0-9;]*m")
MARKER = re.compile(r"FDPROBE,([^,]+),([^,]+),(.*?)\s*$")
FDLOG = re.compile(r"FDLOG,\d+,(\d+),\d+,(.*?)\s*$")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "monitor.log"
    with open(path, errors="replace") as fh:
        lines = [ANSI.sub("", ln) for ln in fh]

    probes = []  # (idx, terminator, command, [reply json strings])
    current = None
    for ln in lines:
        m = MARKER.search(ln)
        if m:
            current = (m.group(1), m.group(2), m.group(3), [])
            probes.append(current)
            continue
        m = FDLOG.search(ln)
        if m and current is not None:
            current[3].append(m.group(2))

    if not probes:
        print(f"No FDPROBE markers in {path} -- was this captured from an FD_PROBE_COMMANDS build?")
        return 1

    accepted, rejected, silent = [], [], []
    print(f"{'#':>4}  {'term':<5} {'command':<15} {'code':<12} reply")
    print("-" * 100)
    for idx, term, cmd, replies in probes:
        if idx in ("init", "done"):
            continue
        if not replies:
            print(f"{idx:>4}  {term:<5} {cmd:<15} {'(no reply)':<12}")
            silent.append(cmd)
            continue
        for reply in replies:
            code, name = None, ""
            try:
                obj = json.loads(reply)
                code, name = obj.get("code"), obj.get("name", "")
            except (ValueError, AttributeError):
                pass
            label = f"{code} {ERRNAMES.get(code, '?')}" if code is not None else "unparsed"
            print(f"{idx:>4}  {term:<5} {cmd:<15} {label:<12} {reply[:120]}")
            if code == 0:
                accepted.append((cmd, name, reply))
            elif code is not None:
                rejected.append(cmd)

    print()
    print(f"ACCEPTED (code 0): {len(accepted)}")
    for cmd, name, _ in accepted:
        print(f"    {cmd}   -> name={name!r}")
    print(f"REJECTED         : {len(rejected)}  {sorted(set(rejected))}")
    print(f"NO REPLY         : {len(silent)}  {sorted(set(silent))}")

    print()
    tscore = [r for c, n, r in accepted if "TSCORE" in c]
    if tscore:
        print("*** TSCORE is reachable -- the score threshold can be set. Replies:")
        for r in tscore:
            print(f"      {r}")
    else:
        print("*** No TSCORE command accepted. If AT+ALGO?/AT+ALGOS? were accepted above, read")
        print("    their reply for the algorithm-config syntax -- 'tscore' lives under")
        print("    \"algorithm\": {\"config\": {\"tscore\": N}} in every INVOKE echo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
