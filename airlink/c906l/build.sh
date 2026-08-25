#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../.." && pwd)"
lvgl="$repo/third_party/lvgl-8.3.11"
out="${1:-$script_dir/out}"
cross="${CROSS_COMPILE:-riscv64-unknown-elf-}"
for tool in "${cross}gcc" "${cross}objcopy" "${cross}objdump" "${cross}readelf" "${cross}nm" sha256sum stat strings find sort python3; do
 command -v "$tool" >/dev/null 2>&1 || { echo "ERROR missing $tool" >&2; exit 1; }
done
grep -Fx 'upstream_commit=74d0a816a440eea53e030c4f1af842a94f7ce3d3' "$lvgl/UPSTREAM.txt" >/dev/null
grep -Eq '^#define LVGL_VERSION_MAJOR 8$' "$lvgl/lvgl.h"
grep -Eq '^#define LVGL_VERSION_MINOR 3$' "$lvgl/lvgl.h"
grep -Eq '^#define LVGL_VERSION_PATCH 11$' "$lvgl/lvgl.h"
python3 "$script_dir/check_font_coverage.py"
python3 "$script_dir/test_visual_assets.py"
python3 "$script_dir/test_saver_deadline.py"
python3 "$script_dir/test_saver_status.py"
python3 "$script_dir/test_saver_full_redraw.py"
python3 "$script_dir/test_adc_simple_battery.py"
python3 "$script_dir/test_battery_bars.py"
python3 "$script_dir/test_noto13_font.py"
python3 "$script_dir/test_refresh_safe46.py"
python3 "$script_dir/test_ch347_pinout.py"
python3 "$script_dir/test_ch347_local_switch.py"
python3 "$script_dir/test_ch347_switch_race.py"
rm -rf -- "$out"; mkdir -p "$out/obj"
common=(-march=rv64imafdc -mabi=lp64d -mcmodel=medany -ffreestanding -fno-builtin -fno-common -fno-stack-protector -ffunction-sections -fdata-sections -Os -DLV_CONF_INCLUDE_SIMPLE -I"$script_dir" -I"$script_dir/../ipc" -I"$lvgl")
project=(ipc_smoke display touch ch347 adc1 airlink_ui lv_port mini_libc airlink_fonts)
"${cross}gcc" "${common[@]}" -Wall -Wextra -Werror -c "$script_dir/start.S" -o "$out/obj/start.o"
objects=("$out/obj/start.o")
for source in "${project[@]}"; do
 obj="$out/obj/$source.o"
 "${cross}gcc" "${common[@]}" -std=c11 -Wall -Wextra -Werror -c "$script_dir/$source.c" -o "$obj"
 objects+=("$obj")
done
while IFS= read -r source; do
 rel="${source#"$lvgl"/}"; key="${rel//\//_}"; obj="$out/obj/${key%.c}.o"
 "${cross}gcc" "${common[@]}" -std=c11 -Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers -c "$source" -o "$obj"
 objects+=("$obj")
done < <(
    {
        find "$lvgl/src/core" "$lvgl/src/draw/sw" "$lvgl/src/hal" "$lvgl/src/misc" -type f -name '*.c'
        find "$lvgl/src/draw" "$lvgl/src/font" -maxdepth 1 -type f -name '*.c'
        printf '%s\n' "$lvgl/src/widgets/lv_label.c"
        printf '%s\n' "$lvgl/src/widgets/lv_arc.c"
        printf '%s\n' "$lvgl/src/widgets/lv_img.c"
        printf '%s\n' "$lvgl/src/widgets/lv_line.c"
        printf '%s\n' "$lvgl/src/extra/lv_extra.c"
        printf '%s\n' "$lvgl/src/extra/layouts/flex/lv_flex.c"
    } | sort -u
)
"${cross}gcc" "${common[@]}" -nostdlib -nostartfiles -Wl,--gc-sections -Wl,--build-id=none -Wl,-Map,"$out/r26-lvgl.map" -T "$script_dir/linker.ld" "${objects[@]}" -lgcc -o "$out/r26-lvgl.elf"
"${cross}objcopy" -O binary "$out/r26-lvgl.elf" "$out/r26-lvgl.bin"
"${cross}objdump" -D "$out/r26-lvgl.elf" > "$out/r26-lvgl.disasm.txt"
"${cross}readelf" -a "$out/r26-lvgl.elf" > "$out/r26-lvgl.readelf.txt"
sha256sum "$out/r26-lvgl.bin" > "$out/r26-lvgl.sha256"
size="$(stat -c %s "$out/r26-lvgl.bin")"
if [ "$size" -le 0 ] || [ "$size" -gt $((0x1ec000)) ]; then
 echo "ERROR invalid R27.6.6.23 payload size $size" >&2
 exit 1
fi
firmware_end_hex="$("${cross}nm" -n "$out/r26-lvgl.elf" | awk '$3 == "__firmware_end" { print $1 }')"
stack_bottom_hex="$("${cross}nm" -n "$out/r26-lvgl.elf" | awk '$3 == "__stack_bottom" { print $1 }')"
if [ -z "$firmware_end_hex" ] || [ -z "$stack_bottom_hex" ]; then
 echo "ERROR missing R27.6.4 linker bounds" >&2
 exit 1
fi
firmware_end=$((16#$firmware_end_hex))
stack_bottom=$((16#$stack_bottom_hex))
free_bytes=$((stack_bottom - firmware_end))
minimum_free=$((0x1f0000 / 5))
[ "$free_bytes" -ge "$minimum_free" ] || { echo "ERROR R27.6.6.23 runtime memory reserve below 20%: $free_bytes" >&2; exit 1; }
require(){ strings "$out/r26-lvgl.bin" | grep -F "$1" >/dev/null || { echo "ERROR payload marker missing: $1" >&2; exit 1; }; }
require 'START proto=1 abi=4'
require 'LVGL init=BEGIN version=8.3.11'
require 'LVGL init/version/memory/buffer=PASS'
require 'LVGL html-v2/fonts/black-bg/layout=PASS'
require 'LVGL visual-polish=PURE_BLACK wifi=POLYLINE saver=WIRELESS_DOT+WIRED_USB outer-ring=REMOVED'
require 'LVGL font-small=NOTO13_M2_CONTRAST enhanced=ON wifi-row=CENTERED'
require 'LVGL refresh-pacing=SAFE46 buffer=1x240x240 page=20ms/240ms spinner=16ms saver=33ms model=COALESCED'
require 'GC9A01 SPI parent=187500000 sclk=46875000 baudr=4 SAFE'
require 'LVGL render-owner=SOLE font-advance=PIXELS legacy-scenes=REMOVED'
require 'LVGL mode-transition=HOME_INLINE arc=64px/2px/16ms timeout=3000ms controls=NONBLOCKING'
require 'LVGL wireless-spinner=UNTIL_WIFI_AND_VH_READY wired=IMMEDIATE_READY'
require 'LVGL saver-deadline=30000ms clock=RDTIME reset=TOUCH+GPIOA29+PROVISION_SUCCESS underflow=FIXED'
require 'LVGL saver-status=STATE_AWARE provision=WAIT_PHONE vh=STOPPED+LISTENING+CLIENT_CONNECTED'
require 'LVGL saver-redraw=FULLSCREEN_X2 flush-failure=RETRY'
require 'LVGL battery-ui=4SEG percent=REMOVED status-text=REMOVED layout=CENTERED voltage=2DP hysteresis=20mV invalid=--V'
require 'ADC1 filter=TRIMMED_MEAN+MEDIAN3 scaling=RAW_DIRECT raw847=4100mV valid=2800..4250mV'
require 'LVGL battery-start=C906L_FIRST_SAMPLE invalid-hold=3 clamp-high=4250mV'
require 'LVGL wired-layout=HOME_HEADPHONE+PAGE_FEATURE_GROUP usb-hub-mode-label=REMOVED'
require 'LVGL alignment=battery-bars+mode-badge+CH347+HUB wired-feature=HEADPHONE'
require 'LVGL pixel-fix=battery-fill-y-1 ch347-back=NO_OVERLAP wired-page=ALL_PORTS_OPEN'
require 'LVGL ch347-control=LOCAL_ALWAYS system-lock=REMOVED ownership-monitor=PAUSED_DURING_RESET readback=AUTO_RECOVER'
require 'LVGL ch347-pinout=PIN_ONLY orientation=CW90 layout=2x6 rows=SCREEN_SIDE_TOP colors=5V_RED+GND_GRAY+SIGNAL_BLUE screen-direction=ROUNDED_LABEL tap=RETURN'
require 'LVGL provision-mode-exit=WIRED_FORCE_CLOSE stale-state-guard=ON'
require 'LVGL provision-view=HOTSPOT_ONLY qr=REMOVED tap=RETURN_WIFI hotspot=KEEP_RUNNING'
require 'LVGL mode-wait-copy=STATE_AWARE boot=SYSTEM_STARTING wired=IMMEDIATE_READY wireless=SERVICE_START'
require 'LVGL first-frame/visible/ready'
require ' full_frames='
require ' page_avg_us='
require 'TOUCH init=PASS'
require 'CH347 local-switch mode='
require 'CH347 local-switch reset=80ms PASS elapsed_ms='
require 'CH347 local-switch readback=RECOVERED mismatch=0x'
require 'UI_STATUS update='
require 'WIFI provisioning=HOTSPOT+CAPTIVE_PORTAL ipc=ABI4'
printf 'R27.6.6.23 payload built: %s (%s bytes, runtime-free=%s bytes)\n' "$out/r26-lvgl.bin" "$size" "$free_bytes"
cat "$out/r26-lvgl.sha256"
