#!/usr/bin/env python3
"""Reject missing, unused, or stale sprite definitions before building firmware."""
import re
from generate_theme_art import ASSETS, ASSET_ROOT, ORDER, ROOT, decode_png_rgba, generate


def check():
    expected = {spec[0] for spec in ASSETS.values()}
    actual = {p.name for p in ASSET_ROOT.glob('*.png')}
    if actual != expected:
        raise ValueError(f'Missing PNGs: {expected-actual}; unused PNGs: {actual-expected}')
    header_ids = set(re.findall(r'\bTHEME_ART_(\w+)', (ROOT/'main/theme_art.h').read_text()))
    used_ids = set(re.findall(r'\bTHEME_ART_(\w+)', (ROOT/'main/ui.c').read_text()))
    if header_ids != set(ORDER) or used_ids != set(ORDER):
        raise ValueError(f'Sprite ID mismatch: unrendered={set(ORDER)-used_ids}, undefined={used_ids-set(ORDER)}, header={header_ids ^ set(ORDER)}')
    total = 0
    for name, (filename,w,h) in ASSETS.items():
        rgba,sw,sh = decode_png_rgba(ASSET_ROOT/filename)
        if sw % w or sh % h or sw//w != sh//h:
            raise ValueError(f'{filename}: invalid source size {sw}x{sh}')
        if not any(rgba[3::4]):
            raise ValueError(f'{filename}: fully transparent artwork')
        total += w*h*4
    generate()
    print(f'Asset audit passed: {len(expected)} source PNGs, {len(ORDER)} rendered sprites, {total:,} base pixel bytes')

if __name__ == '__main__':
    check()
