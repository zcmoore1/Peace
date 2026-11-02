#!/bin/sh
# Disassemble the code a mw2_aimdump.ps1 report carries, at its real
# addresses: the "code" block and every "xref" block. Needs python3 and
# objdump.
#   sh dev/tools/mw2_aimdump_disasm.sh mw2_aimdump.txt
set -eu
report="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Each block is a header line ("code 00569950" or "xref <field> at <addr>")
# followed by "ADDR: hex hex ..." lines; write each as its own binary.
python3 - "$report" "$work" <<'PY'
import re, sys
report, work = sys.argv[1], sys.argv[2]
blocks, cur = [], None
for line in open(report, encoding="ascii", errors="replace"):
    line = line.rstrip("\r\n")
    if re.match(r"^code [0-9A-F]+$", line) or line.startswith("xref "):
        cur = {"title": line, "start": None, "data": bytearray()}
        blocks.append(cur)
    elif cur is not None and re.match(r"^[0-9A-F]{8}:", line):
        addr, _, rest = line.partition(":")
        if cur["start"] is None:
            cur["start"] = addr
        cur["data"] += bytes(int(h, 16) for h in rest.split())
    else:
        cur = None
for i, b in enumerate(blocks):
    if b["start"] is None:
        continue
    open(f"{work}/{i:03d}.bin", "wb").write(b["data"])
    open(f"{work}/{i:03d}.start", "w").write(b["start"])
    open(f"{work}/{i:03d}.title", "w").write(b["title"])
PY

for bin in "$work"/*.bin; do
	[ -e "$bin" ] || continue
	base="${bin%.bin}"
	echo "==== $(cat "$base.title")"
	objdump -D -b binary -m i386 -M intel --adjust-vma="0x$(cat "$base.start")" "$bin" |
		sed -n '/^ *[0-9a-f]\{1,\}:/p'
done
