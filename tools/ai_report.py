#!/usr/bin/env python3
"""Sums up a partner AI log (the game run with $BH_AI_LOG=FILE, src/partner_ai_telemetry.cpp): what each partner did and how
the player fared.

    ai_report.py LOG [--from SECONDS] [--to SECONDS] [--json]

Damage to an enemy is put to a partner when it lands within half a second of that partner's attack (a shot or a blow with
the weapon ready); other damage (the player's, a shove) is "other". A partner is "stuck" while it holds forward and moves
less than 30 units in a second. "Downed" is hit points at 0 (the dying crawl a partner can still save). The response is the
time from the first enemy within 600 of the player to the first partner attack. Distances are the game's units (a character
is about 100 across)."""
import json
import re
import statistics
import sys

LINE = re.compile(r"(\w+)=(-?[\w./]+)")
HIT_WINDOW = 0.5
THREAT = 600.0
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
        fields["raw"] = rest
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


def rounds_used(rows):
    """Rounds gone from the gun in hand between samples (the same item, fewer rounds: shots; a reload adds, not counted)"""
    used = 0
    for a, b in zip(rows, rows[1:]):
        if "hand" not in a or "hand" not in b:
            continue
        ia, ra = a["hand"].rstrip("g").split("/")
        ib, rb = b["hand"].rstrip("g").split("/")
        if ia == ib and ia != "0" and int(rb) < int(ra):
            used += int(ra) - int(rb)
    return used


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


COME_NEAR = 300.0


def command_response(samples, event):
    """For a command: each partner's distance to the player when it's said and 10 s later, and how long until it was within
    COME_NEAR (None: not within the 20 s after)."""
    t = event["t"]
    out = {"t": t, "command": int(event["command"])}
    for c in partners(samples):
        rows = [s for s in samples if int(s["c"]) == c and t <= s["t"] <= t + 20]
        if not rows:
            continue
        at = lambda dt: next((float(s["d"]) for s in rows if s["t"] >= t + dt), None)
        near = next((s["t"] - t for s in rows if s["room"] == s["proom"] and float(s["d"]) < COME_NEAR), None)
        out[c] = {"d0": at(0), "d10": at(10), "near_after": round(near, 1) if near is not None else None}
    return out


def consumable(event):
    return int(event["class"], 16) & (0x1 | 0x2 | 0x400 | 0x800 | 0x4) != 0


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
            "rounds_used": rounds_used(rows),
            "damage_dealt": credited.get(c, 0),
            "damage_taken": sum(int(e["from"]) - int(e["to"]) for e in hurts),
            "heals": [f'{e["from"]}->{e["to"]}/{e["max"]}' for e in heals],
            "stuck_seconds": stuck_seconds(rows),
            "downed": any(hp(s["hp"])[0] <= 0 for s in rows),
        })
        out["partners"][c] = stats
    player_rows = [hp(s["php"]) for s in samples]
    player_hurt = [e for e in events if e["what"] == "hurt" and int(e["c"]) == 0]
    out["player"] = {
        "damage_taken": sum(int(e["from"]) - int(e["to"]) for e in player_hurt),
        "lowest_hp": min(h for h, _ in player_rows) if player_rows else None,
        "seconds_below_30pct": round(sum(0.5 for h, m in player_rows if m and h < 0.3 * m) / max(1, len(partners(samples))), 1),
        "downed": any(h <= 0 for h, _ in player_rows),
        "heals": [f'{e["from"]}->{e["to"]}/{e["max"]}' for e in events if e["what"] == "heal" and int(e["c"]) == 0],
    }
    out["enemies"] = {"damage_other": credited.get("other", 0), "kills": kills}
    # Pick-ups the item chooser made (src/partner_ai.cpp's PickItem): an item that isn't a weapon, ammunition or recovery item
    # (class flags 1, 2/0x400/0x800, 4) taken by a partner is the player's puzzle or progress taken (the New AI refuses them)
    picks = [e for e in events if e["what"] == "did" and "item" in e]
    out["pickups"] = {"consumables": sum(1 for e in picks if consumable(e) and "allowed" in e["raw"]),
                      "puzzle_items_taken": sum(1 for e in picks if not consumable(e) and "allowed" in e["raw"]),
                      "puzzle_items_refused": sum(1 for e in picks if not consumable(e) and "refused" in e["raw"])}
    # The first enemy within THREAT of the player, and the first partner attack after it
    threat = next((s["t"] for s in samples if 0 <= float(s["pnear"]) < THREAT), None)
    answer = next((a["t"] for a in attacks if threat is not None and a["t"] >= threat), None)
    out["response_seconds"] = round(answer - threat, 1) if answer is not None else None
    # The player's damage in the two minutes from that first threat: the partners' doing more than the zombies' numbers
    out["player"]["damage_2min"] = sum(int(e["from"]) - int(e["to"]) for e in player_hurt
                                       if threat is not None and threat <= e["t"] <= threat + 120)
    out["commands"] = [command_response(samples, e) for e in events if e["what"] == "command"]
    return out


# The numbers worth comparing between runs: a run's in one line, and the mean of several
KEYS = [("pickups", "puzzle_items_taken"), ("player", "damage_taken"), ("player", "damage_2min"), ("player", "seconds_below_30pct"), ("player", "lowest_hp"), ("enemies", "kills")]
PARTNER_KEYS = ["far_share", "median_distance", "attacks", "hits", "rounds_used", "damage_dealt", "damage_taken", "stuck_seconds"]


def flat(r):
    out = {f"{a}.{b}": r[a][b] for a, b in KEYS}
    out["player.downed"] = int(r["player"]["downed"])
    if "survived_seconds" in r:
        out["survived_seconds"] = r["survived_seconds"]
    out["response_seconds"] = r["response_seconds"]
    for c, p in r["partners"].items():
        for k in PARTNER_KEYS:
            out[f"p{c}.{k}"] = p[k]
        out[f"p{c}.downed"] = int(p["downed"])
    return out


def one_line(r):
    f = r if "player" not in r else flat(r)
    return ", ".join(f"{k} {round(v, 3) if isinstance(v, float) else v}" for k, v in f.items())


def mean(results):
    flats = [flat(r) for r in results]
    keys = [k for k in flats[0] if all(k in f and f[k] is not None for f in flats)]
    return {k: round(sum(f[k] for f in flats) / len(flats), 2) for k in keys}


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
    print("pick-ups: " + ", ".join(f"{k} {v}" for k, v in r["pickups"].items()))
    print("commands:", r["commands"])


if __name__ == "__main__":
    main()
