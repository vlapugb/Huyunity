FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV XDG_RUNTIME_DIR=/tmp/runtime-dir
ENV SDL_VIDEO_DRIVER=x11

# Установка сборочных инструментов, зависимостей и утилит для Web VNC
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    pkg-config \
    python3 \
    python3-pil \
    libgl1-mesa-dev \
    libx11-dev \
    libxext-dev \
    libxcursor-dev \
    libxinerama-dev \
    libxi-dev \
    libxrandr-dev \
    libxss-dev \
    libxxf86vm-dev \
    libwayland-dev \
    libxkbcommon-dev \
    xvfb \
    x11vnc \
    novnc \
    websockify \
    openbox \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Сборка и установка SDL3 из официального релиза для гарантированной нативной поддержки окна
RUN git clone --depth 1 -b release-3.2.8 https://github.com/libsdl-org/SDL.git /tmp/sdl3 && \
    cmake -B /tmp/sdl3/build -S /tmp/sdl3 -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_STATIC=OFF && \
    cmake --build /tmp/sdl3/build --parallel && \
    cmake --install /tmp/sdl3/build && \
    ldconfig && \
    rm -rf /tmp/sdl3

# Создание симлинка index.html для прямого открытия noVNC в браузере без указания vnc.html
RUN ln -sf /usr/share/novnc/vnc.html /usr/share/novnc/index.html

WORKDIR /app

# Копирование исходного кода проекта
COPY . /app

# Сборка движка Huynity, всех примеров и тестов в Release-режиме
RUN cmake -B /app/build -S /app -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DHUYNITY_BUILD_ADAPTERS=ON \
        -DHUYNITY_BUILD_EXAMPLES=ON \
        -DBUILD_TESTING=ON && \
    cmake --build /app/build --parallel

# Порт HTML5 noVNC веб-клиента
EXPOSE 6080

RUN chmod +x /app/tools/docker/entrypoint.sh

ENTRYPOINT ["/app/tools/docker/entrypoint.sh"]
CMD ["web"]
