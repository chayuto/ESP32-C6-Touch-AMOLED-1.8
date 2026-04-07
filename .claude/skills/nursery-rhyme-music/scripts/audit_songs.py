#!/usr/bin/env python3
"""
Nursery rhyme rhythm audit — checks that every song's note durations
sum to complete measures for its time signature.

Usage: python3 audit_songs.py [path/to/song_data.c]
Default: projects/15_nursery_rhymes/main/song_data.c
"""

import re
import sys
import os

W=2000; H=1000; DH=1500; Q=500; DQ=750; E=250; DE=375; S=125
dur_map = {'W':W,'H':H,'DH':DH,'Q':Q,'DQ':DQ,'E':E,'DE':DE,'S':S}

def find_song_data():
    """Find song_data.c relative to repo root."""
    for root in ['.', '..', '../..']:
        p = os.path.join(root, 'projects/15_nursery_rhymes/main/song_data.c')
        if os.path.exists(p):
            return p
    return None

def audit(path):
    with open(path) as f:
        content = f.read()

    songs = re.findall(
        r'/\* ═+\n \* (\d+)\. (.+?)\n.*?(\d)/(\d).*?═+\s*\*/\n(.*?)(?=\n/\*\s*═|\nconst)',
        content, re.DOTALL)

    if not songs:
        print(f"ERROR: No songs found in {path}")
        return 1

    errors = 0
    for num, name, ts_n, ts_d, body in songs:
        ts_n, ts_d = int(ts_n), int(ts_d)
        measure_ms = ts_n * 500 if ts_d == 4 else 1500

        notes = re.findall(r'\{(\w+),\s*(\w+)\}', body)
        total_ms = sum(dur_map.get(d, 0) for _, d in notes)
        note_count = len(notes)
        measures = total_ms / measure_ms
        remainder = total_ms % measure_ms

        # Check if it's a clean bar count or a valid pickup (x.5)
        is_pickup = abs(measures - round(measures * 2) / 2) < 0.01 and remainder != 0
        is_clean = remainder == 0

        if is_clean:
            status = f"PASS ({measures:.0f} bars)"
        elif is_pickup and abs(measures % 1 - 0.5) < 0.01:
            status = f"PASS ({measures:.1f} bars, anacrusis)"
        else:
            status = f"FAIL ({measures:.2f} bars, remainder {remainder}ms)"
            errors += 1

        print(f"  {num:>2}. {name:<35} {note_count:>3} notes  {total_ms:>6}ms  {status}")

    print(f"\n  {len(songs)} songs checked, {errors} errors")
    return errors

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else find_song_data()
    if not path or not os.path.exists(path):
        print(f"ERROR: Cannot find song_data.c")
        print(f"Usage: {sys.argv[0]} [path/to/song_data.c]")
        sys.exit(1)

    print(f"Auditing: {path}\n")
    errors = audit(path)
    sys.exit(1 if errors else 0)
