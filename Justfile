# dgct/zmk-config-corne build recipes (urob/zmk-config-style).
#
# Requires: just + yq (or `nix develop` / `direnv allow` for the full env).
#
# Usage:
#   just            # list recipes
#   just init       # west init + west update + zephyr export
#   just build      # build all build.yaml targets into firmware/
#   just build left # build all targets whose shield matches "left"
#   just clean      # rm build/ firmware/
#   just update     # west update (refresh modules per west.yml)
#   just flash <port> [side]  # nrfutil flash via serial bootloader
#   just tio <port>           # open serial console
#
# build.yaml schema (already in repo):
#   include:
#     - board: <board>
#       shield: <shield list>
#       artifact-name: <name>     # optional; defaults to <board>-<shield-joined>

config := absolute_path("config")
build  := absolute_path("build")
out    := absolute_path("firmware")
matrix := absolute_path("build.yaml")

# Default: list recipes
default:
    @just --list --unsorted

# Initialise west workspace (run once per clone)
init:
    @echo "==> west init -l {{config}}"
    west init -l {{config}}
    @echo "==> west update"
    west update --narrow --fetch-opt=--filter=blob:none
    @echo "==> west zephyr-export"
    west zephyr-export

# Refresh manifest (after editing config/west.yml)
update:
    west update --narrow --fetch-opt=--filter=blob:none

# Print parsed build matrix
list:
    @yq -o=json '.include' {{matrix}} | jq -r '.[] | "\(.board) \(.shield // "") -> \(.["artifact-name"] // (.board + "-" + (.shield // "" | gsub(" "; "-"))))"'

# Build a single matrix entry. Internal recipe.
_build_single board shield artifact:
    #!/usr/bin/env bash
    set -euo pipefail
    bdir="{{build}}/{{artifact}}"
    echo "==> west build -d ${bdir} -b {{board}} -- -DZMK_CONFIG={{config}} -DSHIELD=\"{{shield}}\""
    if [[ -n "{{shield}}" ]]; then
        west build -d "${bdir}" -b "{{board}}" -s zmk/app -- \
            -DZMK_CONFIG="{{config}}" -DSHIELD="{{shield}}"
    else
        west build -d "${bdir}" -b "{{board}}" -s zmk/app -- \
            -DZMK_CONFIG="{{config}}"
    fi
    mkdir -p "{{out}}"
    if [[ -f "${bdir}/zephyr/zmk.uf2" ]]; then
        cp "${bdir}/zephyr/zmk.uf2" "{{out}}/{{artifact}}.uf2"
        echo "    wrote {{out}}/{{artifact}}.uf2"
    elif [[ -f "${bdir}/zephyr/zmk.hex" ]]; then
        cp "${bdir}/zephyr/zmk.hex" "{{out}}/{{artifact}}.hex"
        echo "    wrote {{out}}/{{artifact}}.hex"
    fi

# Build all targets, optionally filtered by substring match on shield/artifact-name.
build expr="":
    #!/usr/bin/env bash
    set -euo pipefail
    yq -o=json '.include' {{matrix}} | jq -c '.[]' | while read -r entry; do
        board=$(echo "$entry"   | jq -r '.board')
        shield=$(echo "$entry"  | jq -r '.shield // ""')
        artifact=$(echo "$entry" | jq -r --arg b "$board" --arg s "$shield" \
            '.["artifact-name"] // ($b + "-" + ($s | gsub(" "; "-")))')
        if [[ -n "{{expr}}" ]]; then
            if ! echo "${artifact} ${shield}" | grep -qiE -- "{{expr}}"; then
                continue
            fi
        fi
        echo "===== building ${artifact} ====="
        just _build_single "${board}" "${shield}" "${artifact}"
    done
    @echo "==> done. firmware in {{out}}/"

# Remove build artifacts (keeps west modules)
clean:
    rm -rf {{build}} {{out}}

# Remove west modules + artifacts (forces re-init)
clean-all:
    rm -rf {{build}} {{out}} zmk/ modules/ tools/ zephyr/

# Reset bootloader and flash a UF2 via serial (Adafruit nrf52 bootloader).
# Usage: just flash /dev/tty.usbmodem... left
flash port side="":
    #!/usr/bin/env bash
    set -euo pipefail
    file=$(ls {{out}}/*{{side}}*.uf2 2>/dev/null | head -1) || true
    if [[ -z "${file:-}" ]]; then
        echo "no firmware/{{side}}*.uf2 — run 'just build {{side}}' first" >&2
        exit 1
    fi
    echo "==> flashing ${file} via {{port}}"
    # Touch port at 1200 baud to trigger bootloader DFU mode (Adafruit pattern)
    stty -f {{port}} 1200 || true
    sleep 2
    # User then drags the .uf2 to the mounted volume, or use uf2conv if available
    echo "Drag ${file} onto the mounted bootloader volume."

# Open serial console
tio port="/dev/tty.usbmodem*":
    tio {{port}}
