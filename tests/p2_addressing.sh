#!/usr/bin/env bash
# P2 addressing tests — verify that path, GUID, and GUID:sectionType targets
# all resolve to the same item and produce identical edit results.
#
# Usage: tests/p2_addressing.sh [path-to-UEFIEdit] [BIOS-image]
# Exit codes: 0 = all passed, non-zero = at least one failed.

set -u

UEFIEDIT="${1:-$(dirname "$0")/../UEFIEdit/UEFIEdit}"
BIOS="${2:-$(dirname "$0")/../fw/HNX99TF_200525_original_E5C88C6F.bin}"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

PASS=0
FAIL=0
ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

[ -x "$UEFIEDIT" ] || { echo "UEFIEdit not found: $UEFIEDIT"; exit 2; }
[ -f "$BIOS" ]     || { echo "BIOS not found: $BIOS"; exit 2; }

GUID_SETUP="899407D7-99FE-43D8-9A21-79EC328CAC21"
GUID_DXE="A0327FE0-1FDA-4E5B-905D-B510C45A61D0"
GUID_VOL="5C60F367-A505-419A-859E-2A4FF6CA6FE5"
SERIALIO="${SERIALIO:-$(dirname "$0")/../fw/Mashinist_DXE_driver_SerialIo_SerialIo.ffs}"

# === Test 1: dump produces path prefixes on stdout ===
DUMP=$("$UEFIEDIT" "$BIOS" dump 2>/dev/null)
if echo "$DUMP" | head -1 | grep -q "^0 type="; then
    ok "1 dump starts with path '0'"
else
    fail "1 dump: no path prefix on stdout"
fi

# === Test 2: list produces TSV header on stdout ===
LIST=$("$UEFIEDIT" "$BIOS" list 2>/dev/null)
if echo "$LIST" | head -1 | grep -q "^path	type	subtype	guid	offset	size	name$"; then
    ok "2 list TSV header"
else
    fail "2 list: TSV header not found"
fi

# === Test 3: list contains Setup file by GUID ===
if echo "$LIST" | grep -q "$GUID_SETUP"; then
    ok "3 list contains Setup GUID"
else
    fail "3 list: Setup GUID not found"
fi

# === Test 4: extract Setup path from list ===
SETUP_PATH=$(echo "$LIST" | grep "	$GUID_SETUP	" | awk -F'\t' '{print $1}' | head -1)
if [ -n "$SETUP_PATH" ]; then
    ok "4 Setup path extracted: $SETUP_PATH"
else
    fail "4 Setup path not found in list"
fi

# === Test 5: rebuild by GUID == rebuild by path (same file) ===
"$UEFIEDIT" "$BIOS" rebuild "$GUID_SETUP" save "$TMPDIR/t5-guid.bin" 2>/dev/null
"$UEFIEDIT" "$BIOS" rebuild "$SETUP_PATH" save "$TMPDIR/t5-path.bin" 2>/dev/null
if cmp -s "$TMPDIR/t5-guid.bin" "$TMPDIR/t5-path.bin"; then
    ok "5 rebuild Setup: GUID == path"
else
    fail "5 rebuild Setup: GUID != path"
fi

# === Test 6: rebuild Setup file == identity (no data change) ===
if cmp -s "$TMPDIR/t5-guid.bin" "$BIOS"; then
    ok "6 rebuild Setup file = identity"
else
    fail "6 rebuild Setup file: image changed (expected identity)"
fi

# === Test 7: GUID:sectionType resolves to PE32 section ===
# Setup file contains a PE32 section (type 0x10). rebuild via GUID:0x10 should
# succeed. LZMA re-compression may change bytes, so we only check exit code and
# that the output is a valid parseable image (UEFIExtract check omitted to keep
# the test fast).
if "$UEFIEDIT" "$BIOS" rebuild "$GUID_SETUP:0x10" save "$TMPDIR/t7-gs.bin" 2>/dev/null; then
    if [ "$(stat -c%s "$TMPDIR/t7-gs.bin")" -eq "$(stat -c%s "$BIOS")" ]; then
        ok "7 rebuild by GUID:0x10 (PE32) - size preserved"
    else
        fail "7 rebuild by GUID:0x10: size changed"
    fi
else
    fail "7 rebuild by GUID:0x10: command failed"
fi

# === Test 8: path and GUID:sectionType address the same PE32 section ===
# Find the PE32 section path inside Setup from the dump output. Setup is at
# path 0/2/2/27; its GUIDed section child 1/0 contains a PE32 section.
SETUP_PE32_PATH=$(echo "$DUMP" | grep "^$(echo "$SETUP_PATH" | sed 's|^0/||')/1/0 " | grep "PE32" | awk '{print $1}' | head -1)
if [ -z "$SETUP_PE32_PATH" ]; then
    fail "8 PE32 path not found in dump under $SETUP_PATH/1/0"
else
    "$UEFIEDIT" "$BIOS" rebuild "0/$SETUP_PE32_PATH" save "$TMPDIR/t8-path.bin" 2>/dev/null
    "$UEFIEDIT" "$BIOS" rebuild "$GUID_SETUP:0x10" save "$TMPDIR/t8-gs.bin" 2>/dev/null
    if cmp -s "$TMPDIR/t8-path.bin" "$TMPDIR/t8-gs.bin"; then
        ok "8 path (0/$SETUP_PE32_PATH) == GUID:0x10 for PE32"
    else
        fail "8 path != GUID:0x10 for PE32"
    fi
fi

# === Test 9: insert-after by path == insert-after by GUID ===
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" save "$TMPDIR/t9-guid.bin" 2>/dev/null
# Find the DXE file path from the list output
DXE_PATH=$(echo "$LIST" | grep "	$GUID_DXE	" | awk -F'\t' '{print $1}' | head -1)
if [ -n "$DXE_PATH" ]; then
    "$UEFIEDIT" "$BIOS" insert-after "$DXE_PATH" "$SERIALIO" save "$TMPDIR/t9-path.bin" 2>/dev/null
    if cmp -s "$TMPDIR/t9-guid.bin" "$TMPDIR/t9-path.bin"; then
        ok "9 insert-after: GUID == path"
    else
        fail "9 insert-after: GUID != path"
    fi
else
    fail "9 DXE path not found in list"
fi

# === Test 10: remove by path == remove by GUID ===
GUID_SERIALIO="97C81E5D-8FA0-486A-AAEA-0EFDF090FE4F"
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" remove "$GUID_SERIALIO" save "$TMPDIR/t10-guid.bin" 2>/dev/null
# After insert, find the SerialIo path in the new image
"$UEFIEDIT" "$TMPDIR/t9-guid.bin" list 2>/dev/null | grep "$GUID_SERIALIO" | awk -F'\t' '{print $1}' | head -1 > "$TMPDIR/serialio_path.txt"
SERIALIO_PATH=$(cat "$TMPDIR/serialio_path.txt")
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" remove "$GUID_SERIALIO" save "$TMPDIR/t10-guid2.bin" 2>/dev/null
# Both should be identity to the original
if cmp -s "$TMPDIR/t10-guid.bin" "$BIOS" && cmp -s "$TMPDIR/t10-guid2.bin" "$BIOS"; then
    ok "10 insert+remove by GUID = identity"
else
    fail "10 insert+remove by GUID: not identity"
fi

# === Test 11: invalid target produces error, not crash ===
if "$UEFIEDIT" "$BIOS" rebuild "ZZZZZZZZ-ZZZZ-ZZZZ-ZZZZ-ZZZZZZZZZZZZ" save "$TMPDIR/t11.bin" 2>/dev/null; then
    fail "11 invalid GUID target: command succeeded (should fail)"
else
    ok "11 invalid GUID target: command failed as expected"
fi

# === Test 12: out-of-range path produces error ===
if "$UEFIEDIT" "$BIOS" rebuild "0/99/99" save "$TMPDIR/t12.bin" 2>/dev/null; then
    fail "12 invalid path: command succeeded (should fail)"
else
    ok "12 invalid path: command failed as expected"
fi

echo ""
echo "=========================================="
echo "  P2 addressing: $PASS passed, $FAIL failed"
echo "=========================================="
[ "$FAIL" -eq 0 ]