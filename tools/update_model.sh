#!/usr/bin/env bash
# Generate the sound-alert network and import the trained model into the firmware.
# ST's generated code and runtime are proprietary, so they are not in this repo: run this once
# after cloning, and again after every retrain.
#
#   tools/update_model.sh [training output folder]          (default training/out_open)
#
# 1. runs ST Edge AI Core on model_int8.tflite -> Core/AI/network*.c/.h
# 2. copies model_config.h, model_tables.c, test_vector.h (written by train.py) -> Core/AI/
# 3. copies the ST AI runtime library + headers -> Middlewares/ST/AI/ (first time / new version)
# Then rebuild and flash. Run "-ai test" on the serial console to check it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/training/out_open}"
STEDGEAI_ROOT="${STEDGEAI_ROOT:-$HOME/ST/stedgeai/10.2.0}"
STEDGEAI="$STEDGEAI_ROOT/Utilities/linux/stedgeai"

for f in model_int8.tflite model_config.h model_tables.c test_vector.h; do
    [[ -f "$OUT/$f" ]] || { echo "missing $OUT/$f - run train.py first"; exit 1; }
done
[[ -x "$STEDGEAI" ]] || { echo "ST Edge AI Core not found at $STEDGEAI (set STEDGEAI_ROOT)"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "== generating network code"
"$STEDGEAI" generate --model "$OUT/model_int8.tflite" --target stm32l4 --name network \
    --output "$TMP" --workspace "$TMP/ws" < /dev/null > "$TMP/log.txt" 2>&1 || true
[[ -f "$TMP/network.c" ]] || { cat "$TMP/log.txt"; echo "stedgeai failed"; exit 1; }
grep -E "weights \(ro\)|activations \(rw\)|macc " "$TMP/log.txt" || true

mkdir -p "$ROOT/Core/AI"
cp "$TMP"/network.c "$TMP"/network.h "$TMP"/network_config.h \
   "$TMP"/network_data.c "$TMP"/network_data.h \
   "$TMP"/network_data_params.c "$TMP"/network_data_params.h "$ROOT/Core/AI/"
cp "$OUT"/model_config.h "$OUT"/model_tables.c "$OUT"/test_vector.h "$ROOT/Core/AI/"
cp "$OUT"/labels.txt "$ROOT/Core/AI/labels.txt" 2>/dev/null || true

echo "== runtime library"
mkdir -p "$ROOT/Middlewares/ST/AI/Lib" "$ROOT/Middlewares/ST/AI/Inc"
cp "$STEDGEAI_ROOT"/Middlewares/ST/AI/Inc/*.h "$ROOT/Middlewares/ST/AI/Inc/"
cp "$STEDGEAI_ROOT"/Middlewares/ST/AI/Lib/GCC/ARMCortexM4/NetworkRuntime*_CM4_GCC.a \
   "$ROOT/Middlewares/ST/AI/Lib/NetworkRuntime_CM4_GCC.a"
# One function in the 10.2 runtime asks for 64 KB alignment, which leaves a ~64 KB hole
# in flash. 32-byte alignment is plenty for code on the Cortex-M4.
for sec in $(arm-none-eabi-objdump -h "$ROOT/Middlewares/ST/AI/Lib/NetworkRuntime_CM4_GCC.a" \
             | awk '/2\*\*([6-9]|1[0-9])/{print $2}' | sort -u); do
    echo "   reducing alignment of $sec"
    arm-none-eabi-objcopy --set-section-alignment "$sec=32" "$ROOT/Middlewares/ST/AI/Lib/NetworkRuntime_CM4_GCC.a"
done
cp "$STEDGEAI_ROOT"/Middlewares/ST/AI/LICENSE* "$ROOT/Middlewares/ST/AI/" 2>/dev/null || true

echo "== classes: $(tr '\n' ' ' < "$OUT/labels.txt" 2>/dev/null)"
echo "Done. Rebuild: cmake --build build/Debug   then flash, and run '-ai test' on the serial console."
