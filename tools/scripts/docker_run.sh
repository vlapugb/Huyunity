#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
cd "$ROOT_DIR"

# Определяем доступную утилиту контейнеров (docker или podman)
CONTAINER_TOOL=""
if command -v docker >/dev/null 2>&1; then
    CONTAINER_TOOL="docker"
elif command -v podman >/dev/null 2>&1; then
    CONTAINER_TOOL="podman"
else
    echo "Error: Neither 'docker' nor 'podman' found in PATH." >&2
    exit 1
fi

COMPOSE_TOOL=""
if "$CONTAINER_TOOL" compose version >/dev/null 2>&1; then
    COMPOSE_TOOL="$CONTAINER_TOOL compose"
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE_TOOL="docker-compose"
elif command -v podman-compose >/dev/null 2>&1; then
    COMPOSE_TOOL="podman-compose"
fi

cmd="${1:-web}"

case "$cmd" in
    build)
        echo "Building Huynity container image with ${CONTAINER_TOOL}..."
        $CONTAINER_TOOL build -t huynity:latest .
        ;;

    test)
        echo "Running CTest in container..."
        if ! $CONTAINER_TOOL image inspect huynity:latest >/dev/null 2>&1; then
            $CONTAINER_TOOL build -t huynity:latest .
        fi
        $CONTAINER_TOOL run --rm huynity:latest test
        ;;

    web|interactive)
        target="${2:-game_scene}"
        echo "Starting ${target} with interactive Web view (noVNC)..."
        echo "Open your browser at: http://localhost:6080"
        if ! $CONTAINER_TOOL image inspect huynity:latest >/dev/null 2>&1; then
            $CONTAINER_TOOL build -t huynity:latest .
        fi
        $CONTAINER_TOOL run --rm -p 6080:6080 -e "EXAMPLE=${target}" -v "${ROOT_DIR}/output:/app/output:z" huynity:latest web
        ;;

    screenshot)
        target="${2:-hello_cube}"
        out_file="${3:-/app/output/${target}.png}"
        mkdir -p "${ROOT_DIR}/output"
        echo "Generating screenshot for ${target} in container..."
        if ! $CONTAINER_TOOL image inspect huynity:latest >/dev/null 2>&1; then
            $CONTAINER_TOOL build -t huynity:latest .
        fi
        $CONTAINER_TOOL run --rm -v "${ROOT_DIR}/output:/app/output:z" huynity:latest screenshot "${target}" "${out_file}"
        ;;

    cube|hello_cube)
        $0 web hello_cube
        ;;

    arena|third_person|third_person_arena)
        $0 web third_person_arena
        ;;

    corridor|first_person|first_person_corridor)
        $0 web first_person_corridor
        ;;

    gallery)
        $0 web renderer_gallery
        ;;

    *)
        if [[ -n "$COMPOSE_TOOL" && -f "docker-compose.yml" ]]; then
            $COMPOSE_TOOL "$@"
        else
            $CONTAINER_TOOL run --rm -it huynity:latest "$@"
        fi
        ;;
esac
