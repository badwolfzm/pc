#!/usr/bin/env python3
"""Offline, conservative detector for overlapping agent path claims."""
import argparse
import json
import sys


def safe_label(value):
    return isinstance(value, str) and bool(value.strip()) and not any(
        ord(ch) < 32 or ord(ch) == 127 for ch in value
    )


def normalize(raw):
    if not safe_label(raw) or raw != raw.strip() or raw.startswith("/"):
        raise ValueError("paths must be nonempty repository-relative strings")
    is_dir = raw.endswith("/**")
    base = raw[:-3] if is_dir else raw
    if not base or any(ch in base for ch in "*?[]\\"):
        raise ValueError("only literal files or trailing '/**' directory claims are supported")
    parts = base.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ValueError("empty, dot, and parent path components are forbidden")
    return "/".join(parts), is_dir


def overlap(a, b):
    path_a, dir_a = a
    path_b, dir_b = b
    return (path_a == path_b or
            (dir_a and path_b.startswith(path_a + "/")) or
            (dir_b and path_a.startswith(path_b + "/")))


def find_collisions(document):
    if not isinstance(document, dict) or not isinstance(document.get("agents"), list):
        raise ValueError("expected object with 'agents' array")
    entries = []
    seen = set()
    for agent in document["agents"]:
        if not isinstance(agent, dict) or not safe_label(agent.get("id")) or agent["id"] != agent["id"].strip():
            raise ValueError("each agent must have a nonempty string id")
        agent_id = agent["id"]
        if agent_id in seen:
            raise ValueError("agent ids must be unique")
        seen.add(agent_id)
        paths = agent.get("paths")
        if not isinstance(paths, list) or not paths:
            raise ValueError("each agent must claim at least one path")
        for raw in paths:
            entries.append((agent_id, raw, normalize(raw)))
    collisions = []
    for i, (owner_a, raw_a, claim_a) in enumerate(entries):
        for owner_b, raw_b, claim_b in entries[i + 1:]:
            if owner_a != owner_b and overlap(claim_a, claim_b):
                collisions.append((owner_a, raw_a, owner_b, raw_b))
    return collisions


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("claims", help="JSON file of agent ids and repository paths")
    args = parser.parse_args(argv)
    try:
        with open(args.claims, encoding="utf-8") as stream:
            document = json.load(stream)
        collisions = find_collisions(document)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print("INVALID CLAIMS: " + str(exc), file=sys.stderr)
        return 2
    for owner_a, raw_a, owner_b, raw_b in collisions:
        print(f"OVERLAP: {owner_a} [{raw_a}] <> {owner_b} [{raw_b}]")
    print(f"Checked {len(document['agents'])} agents: {len(collisions)} overlap(s)")
    return 1 if collisions else 0


if __name__ == "__main__":
    sys.exit(main())
