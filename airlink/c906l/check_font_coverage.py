#!/usr/bin/env python3
import ast
import re
from pathlib import Path
root=Path(__file__).resolve().parent
covered=set((root/'airlink_fonts.chars.txt').read_text(encoding='utf-8'))
source=(root/'airlink_ui.c').read_text(encoding='utf-8')
required=set()
for literal in re.findall(r'"(?:[^"\\]|\\.)*"', source):
    try: text=ast.literal_eval(literal)
    except (SyntaxError,ValueError): continue
    required.update(text)
missing=sorted(ch for ch in required if ord(ch)>=32 and ch not in covered)
if missing: raise SystemExit('R27.5 font coverage missing: '+''.join(missing))
print(f'R27.5 font coverage: PASS ({len(required)} required, {len(covered)} glyphs)')
