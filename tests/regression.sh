#!/usr/bin/env bash
# Regression tests for UEFIEdit — covers the scenarios from AGENTS.md tests 1-7
# plus the P1 bugfixes (clearChildren, LZMA re-compression, rebuild cascade).
#
# Usage: tests/regression.sh [path-to-UEFIEdit] [BIOS-image] [extractor]
#
# Exit codes: 0 = all passed, non-zero = at least one failed.

set -u

UEFIEDIT="${1:-$(dirname "$0")/../UEFIEdit/UEFIEdit}"
BIOS="${2:-$(dirname "$0")/../fw/HNX99TF_200525_original_E5C88C6F.bin}"
SERIALIO="${SERIALIO:-$(dirname "$0")/../fw/Mashinist_DXE_driver_SerialIo_SerialIo.ffs}"
TERMINALSRC="${TERMINALSRC:-$(dirname "$0")/../fw/MAshinist_DXE_driver_TerminalSrc_TerminalSrc.ffs}"
EXTRACTOR="${3:-}"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

PASS=0
FAIL=0
ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

[ -x "$UEFIEDIT" ] || { echo "UEFIEdit not found: $UEFIEDIT"; exit 2; }
[ -f "$BIOS" ]     || { echo "BIOS not found: $BIOS"; exit 2; }
[ -f "$SERIALIO" ] || { echo "SerialIo FFS not found: $SERIALIO"; exit 2; }

GUID_VOL="5C60F367-A505-419A-859E-2A4FF6CA6FE5"
GUID_DXE="A0327FE0-1FDA-4E5B-905D-B510C45A61D0"
GUID_SERIALIO="97C81E5D-8FA0-486A-AAEA-0EFDF090FE4F"
GUID_SETUP="899407D7-99FE-43D8-9A21-79EC328CAC21"

# Prepare a modified Setup FFS: flip one byte in the body to verify clearChildren.
"$UEFIEDIT" "$BIOS" dump >/dev/null 2>&1
python3 - "$TMPDIR" "$BIOS" <<'PY'
import sys, struct, os
tmpdir = sys.argv[1]
bios = sys.argv[2]
data = bytearray(open(bios,'rb').read())
# Setup FFS GUID 899407D7-99FE-43D8-9A21-79EC328CAC21 in mixed-endian
# (Data1 LE, Data2 LE, Data3 LE, Data4 raw)
target = bytes([0xD7,0x07,0x94,0x89,0xFE,0x99,0xD8,0x43,0x9A,0x21,0x79,0xEC,0x32,0x8C,0xAC,0x21])
pos = data.find(target)
assert pos != -1, "Setup FFS not found"
# Header is 24 bytes (EFI_FFS_FILE_HEADER). Body starts at pos+24.
# Flip the 7th body byte (offset 0x1E from FFS start).
data[pos + 30] ^= 0xFF
# Write the full FFS (header+body) to test_ffs3.bin from the MODIFIED data
size24 = data[pos+20] | (data[pos+21]<<8) | (data[pos+22]<<16)
ffs_size = size24 if size24 != 0xFFFFFF else 0  # large file not handled here
if ffs_size == 0:
    # fallback: read ExtendedSize
    pass
ffs = bytes(data[pos:pos+ffs_size])
open(os.path.join(tmpdir,'test_ffs3.bin'),'wb').write(ffs)
# Restore the byte to get the ORIGINAL body for the replace-body round-trip test
data[pos + 30] ^= 0xFF
body = bytes(data[pos+24:pos+ffs_size])
open(os.path.join(tmpdir,'setup_ffs_body.bin'),'wb').write(body)
print(f"Setup FFS at 0x{pos:X}, size {ffs_size}, body {len(body)}")
PY

# === Test 1: Rebuild without changes = identity ===
"$UEFIEDIT" "$BIOS" save "$TMPDIR/t1.bin" 2>/dev/null
if cmp -s "$TMPDIR/t1.bin" "$BIOS"; then ok "1 rebuild=identity"; else fail "1 rebuild=identity"; fi

# === Test 2: Insert + verify GUID in tree ===
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" save "$TMPDIR/t2.bin" 2>/dev/null
if "$UEFIEDIT" "$TMPDIR/t2.bin" dump 2>&1 | grep -q "$GUID_SERIALIO"; then
    ok "2 insert-after: SerialIo GUID in tree"
else
    fail "2 insert-after: SerialIo GUID not in tree"
fi

# === Test 3: Insert + Remove = identity ===
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" remove "$GUID_SERIALIO" save "$TMPDIR/t3.bin" 2>/dev/null
if cmp -s "$TMPDIR/t3.bin" "$BIOS"; then ok "3 insert+remove=identity"; else fail "3 insert+remove=identity"; fi

# === Test 4: Remove Volume (size preserved) ===
if "$UEFIEDIT" "$BIOS" remove "$GUID_VOL" save "$TMPDIR/t4.bin" 2>/dev/null; then
    if [ "$(stat -c%s "$TMPDIR/t4.bin")" -eq "$(stat -c%s "$BIOS")" ]; then
        ok "4 remove volume (size preserved)"
    else
        fail "4 remove volume: size changed"
    fi
else
    fail "4 remove volume: command failed"
fi

# === Test 5: Chain of operations ===
if "$UEFIEDIT" "$BIOS" \
    insert "$GUID_VOL" "$SERIALIO" \
    insert-after "$GUID_DXE" "$TERMINALSRC" \
    remove "$GUID_SERIALIO" \
    save "$TMPDIR/t5.bin" 2>/dev/null; then
    ok "5 chain of operations"
else
    fail "5 chain of operations"
fi

# === Test 6: Replace FFS with modified body (clearChildren check) ===
# test_ffs3.bin = original Setup FFS with one byte flipped in body.
"$UEFIEDIT" "$BIOS" replace "$GUID_SETUP" "$TMPDIR/test_ffs3.bin" save "$TMPDIR/t6.bin" 2>/dev/null
# The image MUST differ from the original (the body byte was changed).
if cmp -s "$TMPDIR/t6.bin" "$BIOS"; then
    fail "6 replace: image unchanged (clearChildren broken)"
else
    # Verify the flipped byte is present in the output at the Setup offset.
    python3 - "$TMPDIR/t6.bin" "$BIOS" <<'PY'
import sys
t6 = open(sys.argv[1],'rb').read()
orig = open(sys.argv[2],'rb').read()
target = bytes([0xD7,0x07,0x94,0x89,0xFE,0x99,0xD8,0x43,0x9A,0x21,0x79,0xEC,0x32,0x8C,0xAC,0x21])
pos = t6.find(target)
if pos == -1:
    print("FAIL: 6 Setup GUID not found in t6")
    sys.exit(1)
b_t6 = t6[pos+30]
b_orig = orig[pos+30]
if b_t6 != b_orig:
    print(f"PASS: 6 byte differs (orig={b_orig:02X} t6={b_t6:02X})")
    sys.exit(0)
else:
    print(f"FAIL: 6 byte identical ({b_t6:02X})")
    sys.exit(1)
PY
    if [ $? -eq 0 ]; then ok "6 replace changes byte (clearChildren)"; else fail "6 replace: byte not changed"; fi
fi

# === Test 7: Replace-body of Setup (LZMA GUIDed section) round-trip ===
# Replace-body with the original body. The builder must re-compress the LZMA
# GUIDed section and produce an image identical to the original.
if [ -f "$TMPDIR/setup_ffs_body.bin" ]; then
    "$UEFIEDIT" "$BIOS" replace-body "$GUID_SETUP" "$TMPDIR/setup_ffs_body.bin" save "$TMPDIR/t7.bin" 2>/dev/null
    if cmp -s "$TMPDIR/t7.bin" "$BIOS"; then
        ok "7 replace-body Setup LZMA round-trip = identity"
    else
        fail "7 replace-body Setup LZMA: image changed (re-compression not idempotent)"
    fi
else
    fail "7 setup_ffs_body.bin not prepared"
fi

# === Test 8: Rebuild cascade (P1.1) ===
# rebuild a single file; the volume must be rebuilt too. Without the cascade
# fix, the volume would keep NoAction and the file body wouldn't be written.
# Rebuilding unchanged data must produce an identical image.
"$UEFIEDIT" "$BIOS" rebuild "$GUID_DXE" save "$TMPDIR/t8.bin" 2>/dev/null
if cmp -s "$TMPDIR/t8.bin" "$BIOS"; then
    ok "8 rebuild cascade = identity (no data change)"
else
    fail "8 rebuild cascade: image changed (expected identity)"
fi

# === Test 9: Rebuild Setup (LZMA section) = identity ===
"$UEFIEDIT" "$BIOS" rebuild "$GUID_SETUP" save "$TMPDIR/t9.bin" 2>/dev/null
if cmp -s "$TMPDIR/t9.bin" "$BIOS"; then
    ok "9 rebuild Setup LZMA = identity"
else
    fail "9 rebuild Setup LZMA: image changed (expected identity)"
fi

echo ""
echo "=========================================="
echo "  Regression: $PASS passed, $FAIL failed"
echo "=========================================="
[ "$FAIL" -eq 0 ]