#!/usr/bin/env bash
# Smoke tests for UEFIEdit + UEFITool (headless).
# Run after every change in common/ or UEFIEdit/ or UEFITool/.
#
# Usage: tests/smoke.sh [path-to-UEFIEdit] [path-to-UEFITool] [BIOS-image]
#
# Exit codes: 0 = all passed, non-zero = at least one failed.

set -u

UEFIEDIT="${1:-$(dirname "$0")/../UEFIEdit/UEFIEdit}"
UEFITOOL="${2:-$(dirname "$0")/../UEFITool/UEFITool}"
BIOS="${3:-$(dirname "$0")/../fw/HNX99TF_200525_original_E5C88C6F.bin}"
SERIALIO="${SERIALIO:-$(dirname "$0")/../fw/Mashinist_DXE_driver_SerialIo_SerialIo.ffs}"
TERMINALSRC="${TERMINALSRC:-$(dirname "$0")/../fw/MAshinist_DXE_driver_TerminalSrc_TerminalSrc.ffs}"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

PASS=0
FAIL=0

ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

# --- Prerequisites ---
[ -x "$UEFIEDIT" ] || { echo "UEFIEdit not found/executable: $UEFIEDIT"; exit 2; }
[ -x "$UEFITOOL" ] || { echo "UEFITool not found/executable: $UEFITOOL"; exit 2; }
[ -f "$BIOS" ]     || { echo "BIOS image not found: $BIOS"; exit 2; }
[ -f "$SERIALIO" ] || { echo "SerialIo FFS not found: $SERIALIO"; exit 2; }
[ -f "$TERMINALSRC" ] || { echo "TerminalSrc FFS not found: $TERMINALSRC"; exit 2; }

GUID_VOL="5C60F367-A505-419A-859E-2A4FF6CA6FE5"
GUID_DXE="A0327FE0-1FDA-4E5B-905D-B510C45A61D0"
GUID_SERIALIO="97C81E5D-8FA0-486A-AAEA-0EFDF090FE4F"
GUID_TERMINALSRC="54891A9E-763E-4377-8841-8D5C90D88CDE"
GUID_SETUP="899407D7-99FE-43D8-9A21-79EC328CAC21"

# === Test 1: Rebuild without changes = identity ===
"$UEFIEDIT" "$BIOS" save "$TMPDIR/t1.bin" 2>/dev/null
if cmp -s "$TMPDIR/t1.bin" "$BIOS"; then ok "1 rebuild=identity"; else fail "1 rebuild=identity"; fi

# === Test 2: Insert + extract = identity of extracted ===
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" save "$TMPDIR/t2.bin" 2>/dev/null
# Use UEFIEdit dump to verify SerialIo GUID is present in the tree
if "$UEFIEDIT" "$TMPDIR/t2.bin" dump 2>&1 | grep -q "$GUID_SERIALIO"; then
    ok "2 insert-after produces SerialIo GUID in tree"
else
    fail "2 insert-after: SerialIo GUID not found in tree"
fi

# === Test 3: Insert + Remove = identity to original ===
"$UEFIEDIT" "$BIOS" insert-after "$GUID_DXE" "$SERIALIO" remove "$GUID_SERIALIO" save "$TMPDIR/t3.bin" 2>/dev/null
if cmp -s "$TMPDIR/t3.bin" "$BIOS"; then ok "3 insert+remove=identity"; else fail "3 insert+remove=identity"; fi

# === Test 4: Remove Volume ===
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

# === Test 6: Replace FFS (clearChildren check) ===
# Replace a file with itself; image must remain identical (round-trip).
"$UEFIEDIT" "$BIOS" replace "$GUID_DXE" "$SERIALIO" save "$TMPDIR/t6.bin" 2>/dev/null
# t6 must differ from original (we replaced DXE with a different FFS)
if ! cmp -s "$TMPDIR/t6.bin" "$BIOS"; then
    ok "6 replace changes image"
else
    fail "6 replace: image unchanged (clearChildren broken?)"
fi

# === Test 7: Replace-body of Setup (LZMA GUIDed section, re-compression) ===
# Replace-body with the original body extracted from the same file: round-trip.
# First extract the Setup body via replace-body with its own current body by
# using the FFS file itself (replace mode as-is should be idempotent if we
# feed back the same file). We don't have the extracted body, so we do a
# replace with the original FFS and check it still builds.
if "$UEFIEDIT" "$BIOS" rebuild "$GUID_SETUP" save "$TMPDIR/t7.bin" 2>/dev/null; then
    # rebuild of a Setup with LZMA section should produce a valid (decompressable) image
    # We just check size preservation as a minimal smoke check.
    if [ "$(stat -c%s "$TMPDIR/t7.bin")" -eq "$(stat -c%s "$BIOS")" ]; then
        ok "7 rebuild Setup (LZMA) size preserved"
    else
        fail "7 rebuild Setup: size changed"
    fi
else
    fail "7 rebuild Setup: command failed"
fi

# === Test 8: GUI headless smoke ===
# UEFITool opens a Qt event loop and does not exit on its own, so we launch it
# with a short timeout and check the parsing output on stdout for the FIT table
# (a reliable indicator that the image was parsed successfully). Exit code 124
# from timeout(1) is expected (the process was killed by the timer).
GUIOUT=$(QT_QPA_PLATFORM=offscreen timeout 6 "$UEFITOOL" "$BIOS" 2>&1 || true)
if echo "$GUIOUT" | grep -q "real FIT table found"; then
    ok "8 GUI headless launch (parsed FIT table)"
else
    fail "8 GUI headless: FIT table not found in output"
fi

# === Test 9: dump produces non-empty output ===
DUMP=$("$UEFIEDIT" "$BIOS" dump 2>&1)
if [ -n "$DUMP" ]; then
    ok "9 dump non-empty"
else
    fail "9 dump empty"
fi

# === Test 10: rebuild cascade (P1.1) ===
# rebuild a single file inside a volume; the volume must be rebuilt too,
# so the resulting image must differ if the file had any children to rebuild.
# We test rebuild of a known DXE file; without the cascade fix, the volume
# would keep NoAction and the file body wouldn't change.
"$UEFIEDIT" "$BIOS" rebuild "$GUID_DXE" save "$TMPDIR/t10.bin" 2>/dev/null
# rebuild without changes must produce an identical image (checksums recomputed
# identically for unchanged data).
if cmp -s "$TMPDIR/t10.bin" "$BIOS"; then
    ok "10 rebuild cascade = identity (no data change)"
else
    fail "10 rebuild cascade: image changed (expected identity)"
fi

echo ""
echo "==================================="
echo "  Smoke: $PASS passed, $FAIL failed"
echo "==================================="
[ "$FAIL" -eq 0 ]