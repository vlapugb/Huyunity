#!/usr/bin/env bash
set -euo pipefail

HUYNITY_DIR="/app"
BUILD_DIR="${HUYNITY_DIR}/build"
OUTPUT_DIR="${HUYNITY_DIR}/output"
mkdir -p "${OUTPUT_DIR}"

EXAMPLE="${EXAMPLE:-game_scene}"
RESOLUTION="${RESOLUTION:-800x600}"

resolve_binary() {
    local target="$1"
    case "$target" in
        hello_cube|cube)
            echo "${BUILD_DIR}/examples/hello_cube/huynity_hello_cube"
            ;;
        game_scene|arena|orbit_arena)
            echo "${BUILD_DIR}/examples/game_scene/huynity_game_scene"
            ;;
        renderer_gallery|gallery)
            echo "${BUILD_DIR}/examples/renderer_gallery/huynity_renderer_gallery"
            ;;
        window_demo|demo)
            echo "${BUILD_DIR}/examples/window_demo/huynity_window_demo"
            ;;
        objectlib)
            echo "${BUILD_DIR}/examples/objectlib/huynity_objectlib_example"
            ;;
        *)
            if [[ -x "${BUILD_DIR}/examples/${target}/huynity_${target}" ]]; then
                echo "${BUILD_DIR}/examples/${target}/huynity_${target}"
            elif [[ -x "$target" ]]; then
                echo "$target"
            else
                echo ""
            fi
            ;;
    esac
}

cmd="${1:-web}"

case "$cmd" in
    test|tests|check)
        echo "=========================================="
        echo "Running Huynity CTest Test Suite..."
        echo "=========================================="
        exec ctest --test-dir "${BUILD_DIR}" --output-on-failure
        ;;

    screenshot)
        target="${2:-hello_cube}"
        out_path="${3:-${OUTPUT_DIR}/${target}.ppm}"
        binary="$(resolve_binary "$target")"
        if [[ -z "$binary" || ! -x "$binary" ]]; then
            echo "Error: Unknown or missing binary for target '${target}'" >&2
            exit 1
        fi
        echo "Generating screenshot for '${target}' -> ${out_path}..."
        "$binary" --screenshot "$out_path"
        if python3 -c "import PIL" >/dev/null 2>&1; then
            png_path="${out_path%.*}.png"
            python3 -c "from PIL import Image; Image.open('${out_path}').save('${png_path}')"
            echo "Saved PNG to ${png_path}"
        fi
        ;;

    run|exec)
        target="${2:-hello_cube}"
        shift 2 || true
        binary="$(resolve_binary "$target")"
        if [[ -z "$binary" || ! -x "$binary" ]]; then
            echo "Error: Unknown or missing binary for target '${target}'" >&2
            exit 1
        fi
        exec "$binary" "$@"
        ;;

    web|interactive)
        target="${2:-${EXAMPLE}}"
        binary="$(resolve_binary "$target")"
        if [[ -z "$binary" || ! -x "$binary" ]]; then
            echo "Error: Unknown or missing binary for target '${target}'" >&2
            exit 1
        fi

        echo "=========================================================="
        echo "Starting Huynity Interactive Web Stream (noVNC)"
        echo "Target Example: ${target}"
        echo "Display Resolution: ${RESOLUTION}"
        echo "Open your browser at: http://localhost:6080"
        echo "=========================================================="

        export XDG_RUNTIME_DIR="/tmp/runtime-dir"
        mkdir -p -m 0700 "${XDG_RUNTIME_DIR}"
        export DISPLAY=:99
        export SDL_VIDEO_DRIVER=x11

        Xvfb :99 -screen 0 "${RESOLUTION}x24" -ac +extension GLX +render -noreset >/dev/null 2>&1 &
        sleep 1

        openbox >/dev/null 2>&1 &
        x11vnc -display :99 -forever -nopw -shared -rfbport 5900 -listen 127.0.0.1 -quiet >/dev/null 2>&1 &
        websockify --web=/usr/share/novnc 0.0.0.0:6080 localhost:5900 >/dev/null 2>&1 &

        echo "Container ready. Running ${binary} --continuous ..."
        exec "$binary" --continuous
        ;;

    *)
        exec "$@"
        ;;
esac
