#!/usr/bin/env python3
"""Sums up a partner AI log (the game run with $BH_AI_LOG=FILE, src/partner_ai_telemetry.cpp): what each partner did and how
the player fared.

    ai_report.py LOG [--from SECONDS] [--to SECONDS] [--json]

Damage to an enemy is put to a partner when it lands within half a second of that partner's attack (a shot or a blow with
the weapon ready); other damage (the player's, a shove) is "other". A partner is "stuck" while it holds forward and moves
less than 30 units in a second. Distances are the game's units (a character is about 100 across)."""
import json
import re
import statistics
import sys

LINE = re.compile(r"(\w+)=(-?[\w./]+)")
HIT_WINDOW = 0.5
STUCK_UNITS = 30.0


def parse(path, start, end):
    samples, events = [], []
    for line in open(path, errors="replace"):
        kind, rest = line[:1], line[2:]
        fields = dict(LINE.findall(rest))
        words = [w for w in rest.split() if "=" not in w]
        if "t" not in fields:
            continue
        t = float(fields["t"])
        if t < start or t > end:
            continue
        fields["t"] = t
        if kind == "S":
            samples.append(fields)
        elif kind == "E":
            fields["what"] = words[0] if words else ("damage" if "enemy" in fields else "command" if "command" in fields else "?")
            events.append(fields)
    return samples, events


def hp(text):
    now, most = text.split("/")
    return int(now), int(most)


def partners(samples):
    return sorted({int(s["c"]) for s in samples})


def distance_stats(rows):
    same = [float(s["d"]) for s in rows if s["room"] == s["proom"]]
    other_room = sum(1 for s in rows if s["room"] != s["proom"])
    far = sum(1 for s in rows if s["room"] != s["proom"] or float(s["d"]) > 1500)
    return {
        "same_room_share": round(1 - other_room / len(rows), 3) if rows else None,
        "median_distance": round(statistics.median(same)) if same else None,
        "p90_distance": round(sorted(same)[int(len(same) * 0.9) - 1]) if len(same) >= 10 else None,
        "far_share": round(far / len(rows), 3) if rows else None,
    }


def stuck_seconds(rows):
    stuck = 0.0
    for a, b in zip(rows, rows[1:]):
        dt = b["t"] - a["t"]
        if dt <= 0 or dt > 1.5 or a["room"] != b["room"]:
            continue
        moved = ((float(b["x"]) - float(a["x"])) ** 2 + (float(b["z"]) - float(a["z"])) ** 2) ** 0.5
        holding_forward = int(a["pad"], 16) & 1 and int(b["pad"], 16) & 1
        if holding_forward and moved < STUCK_UNITS * dt:
            stuck += dt
    return round(stuck, 1)


def report(samples, events):
    out = {"seconds": round(samples[-1]["t"] - samples[0]["t"], 1) if samples else 0, "partners": {}}
    attacks = [e for e in events if e["what"] == "attack"]
    damage = [e for e in events if e["what"] == "damage"]
    credited = {}
    hits = set()
    for d in damage:
        before = [a for a in attacks if 0 <= d["t"] - a["t"] <= HIT_WINDOW]
        who = int(before[-1]["c"]) if before else "other"
        credited[who] = credited.get(who, 0) + int(d["damage"])
        if before:
            hits.add(id(before[-1]))
    kills = sum(1 for d in damage if int(d["hp"].split("/")[0]) <= 0)
    for c in partners(samples):
        rows = [s for s in samples if int(s["c"]) == c]
        mine = [a for a in attacks if int(a["c"]) == c]
        heals = [e for e in events if e["what"] == "heal" and int(e["c"]) == c]
        hurts = [e for e in events if e["what"] == "hurt" and int(e["c"]) == c]
        stats = distance_stats(rows)
        stats.update({
            "attacks": len(mine),
            "hits": sum(1 for a in mine if id(a) in hits),
            "damage_dealt": credited.get(c, 0),
            "damage_taken": sum(int(e["from"]) - int(e["to"]) for e in hurts),
            "heals": [f'{e["from"]}->{e["to"]}/{e["max"]}' for e in heals],
            "stuck_seconds": stuck_seconds(rows),
            "died": any(hp(s["hp"])[0] <= 0 for s in rows),
        })
        out["partners"][c] = stats
    player_rows = [hp(s["php"]) for s in samples]
    player_hurt = [e for e in events if e["what"] == "hurt" and int(e["c"]) == 0]
    out["player"] = {
        "damage_taken": sum(int(e["from"]) - int(e["to"]) for e in player_hurt),
        "lowest_hp": min(h for h, _ in player_rows) if player_rows else None,
        "seconds_below_30pct": round(sum(0.5 for h, m in player_rows if m and h < 0.3 * m) / max(1, len(partners(samples))), 1),
        "died": any(h <= 0 for h, _ in player_rows),
        "heals": [f'{e["from"]}->{e["to"]}/{e["max"]}' for e in events if e["what"] == "heal" and int(e["c"]) == 0],
    }
    out["enemies"] = {"damage_other": credited.get("other", 0), "kills": kills}
    out["commands"] = [(e["t"], int(e["command"])) for e in events if e["what"] == "command"]
    return out


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    start = float(args[args.index("--from") + 1]) if "--from" in args else 0.0
    end = float(args[args.index("--to") + 1]) if "--to" in args else float("inf")
    samples, events = parse(args[0], start, end)
    if not samples:
        sys.exit(f"{args[0]}: no samples")
    r = report(samples, events)
    if "--json" in args:
        print(json.dumps(r, indent=1))
        return
    print(f"{r['seconds']} s")
    for c, p in r["partners"].items():
        print(f"partner {c}: " + ", ".join(f"{k} {v}" for k, v in p.items()))
    print("player: " + ", ".join(f"{k} {v}" for k, v in r["player"].items()))
    print("enemies: " + ", ".join(f"{k} {v}" for k, v in r["enemies"].items()))
    print("commands:", r["commands"])


main()
