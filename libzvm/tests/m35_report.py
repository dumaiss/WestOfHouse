#!/usr/bin/env python3
"""Collect existing MAME console captures and compare emulated VM timings."""
import argparse
import json
import re
from pathlib import Path

COMMANDS = ["look", "open mailbox", "take leaflet", "read leaflet",
            "inventory", "score", "look", "examine mailbox",
            "close mailbox", "open mailbox"]
FIELDS = set("counts us room score moves output_bytes hash written_stack_lower_bound guard_ok dyn_hits dyn_misses dyn_evictions static_hits static_misses static_evictions xread_calls xread_bytes xwrite_calls xwrite_bytes reader_calls reader_bytes fread_calls fread_bytes instructions op_0op op_1op op_2op op_var calls returns branches branches_taken yields decoder_fetches byte_reads word_reads byte_writes word_writes text_chars abbreviations dictionary_lookups dictionary_comparisons output_calls".split())


def collect(path):
    # Console snapshots repeat prior lines and may wrap within a field. Key by
    # phase/turn; the last complete copy of each metric replaces earlier copies.
    text = Path(path).read_text(errors="replace")
    records = {}
    for match in re.finditer(r"(bench|profile):\s*mode=(normal|discard)(.*?)(?=(?:bench|profile):|---|\Z)",
                             text, re.S):
        kind, mode, body = match.groups()
        compact = re.sub(r"\s+", "", body)
        prefix = re.match(r"phase=(load|startup|input|timer-pair)turn=(\d+)", compact)
        if prefix:
            phase, turn = prefix.group(1), int(prefix.group(2))
            numeric = compact[prefix.end():]
        elif compact.startswith("final"):
            phase, turn, numeric = "final", 0, compact[5:]
        elif kind == "profile" and re.match(r"[a-z][a-z_0-9]*=\d", compact):
            phase, turn, numeric = "final", 0, compact
        else:
            # Ignore configuration headers and partially redrawn phase names.
            continue
        key = (mode, phase, turn)
        record = records.setdefault(key, {"mode": mode, "phase": phase, "turn": turn})
        # Restrict keys to identifier boundaries in the unwrapped compact text.
        values = {field: int(value) for field, value in
                  re.findall(r"(?<![a-z_])([a-z][a-z_0-9]*?)=(\d+)", numeric)
                  if field in FIELDS}
        if "counts" in values and ("us" not in values or
                abs(values["us"] - values["counts"] * 128 // 5) > 1):
            # A console frame can catch the timer line while it is redrawn.
            values.pop("counts", None)
            values.pop("us", None)
        for field, value in values.items():
            record[("profile_" if kind == "profile" else "") + field] = value
        if "unavailable" in compact:
            record["timer_unavailable"] = True
    rows = sorted(records.values(), key=lambda r: (r["mode"], r["phase"], r["turn"]))
    return {"capture": str(path), "records": rows}


def comparison(before, after):
    index = lambda data: {(r["mode"], r["phase"], r["turn"]): r for r in data["records"]}
    old, new = index(before), index(after)
    rows = []
    for key in sorted(old.keys() & new.keys()):
        a, b = old[key], new[key]
        if "counts" not in a or "counts" not in b or not b["counts"]:
            continue
        mode, phase, turn = key
        name = COMMANDS[turn-1] if phase == "input" and 1 <= turn <= len(COMMANDS) else phase
        rows.append({"mode": mode, "phase": phase, "turn": turn, "name": name,
                     "baseline_counts": a["counts"], "optimized_counts": b["counts"],
                     "speedup": a["counts"] / b["counts"],
                     "reduction_percent": 100 * (a["counts"]-b["counts"]) / a["counts"]
                     if a["counts"] else 0})
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("captures", nargs="+", type=Path)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--require-complete", action="store_true")
    p.add_argument("--mode", action="append", choices=["normal", "discard"])
    args = p.parse_args()
    runs = [collect(path) for path in args.captures]
    if args.mode:
        for run in runs:
            run["records"] = [r for r in run["records"] if r["mode"] in args.mode]
    if not all(run["records"] for run in runs):
        p.error("a capture contains no benchmark records")
    if args.require_complete:
        for mode in args.mode or ["normal", "discard"]:
            states = []
            for run in runs:
                index = {(r["phase"], r["turn"]): r for r in run["records"] if r["mode"] == mode}
                expected = [("load", 0), ("startup", 0)] + [("input", i) for i in range(1, 11)]
                if any("counts" not in index.get(key, {}) for key in expected):
                    p.error(f"{run['capture']}: incomplete {mode} timing sequence")
                final = index.get(("final", 0), {})
                keys = ["room", "score", "moves", "output_bytes", "hash"]
                if any(key not in final for key in keys):
                    p.error(f"{run['capture']}: incomplete {mode} final state")
                states.append(tuple(final[key] for key in keys))
            if len(set(states)) != 1:
                p.error(f"{mode}: final state/output differs between captures")
    result = {"timer_unit_us": 25.6, "runs": runs}
    if len(runs) == 2:
        result["comparison"] = comparison(*runs)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    for run in runs:
        measured = [r for r in run["records"] if "counts" in r]
        print(f"{run['capture']}: {len(measured)} measured phase records")
    for row in result.get("comparison", []):
        print(f"{row['mode']:7} {row['turn']:2} {row['name']:18} "
              f"{row['baseline_counts']:10} -> {row['optimized_counts']:10} counts "
              f"{row['speedup']:.3f}x")


if __name__ == "__main__":
    main()
