# Образ seabattle: сборка в debian:trixie, запуск в debian:trixie-slim от непривилегированного пользователя.
# Состояния на диске нет: комнаты живут в памяти процесса. Пример запуска: docker-compose.yml.
FROM debian:trixie AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr \
    && cmake --build /build -j"$(nproc)" \
    && DESTDIR=/out cmake --install /build --strip

FROM debian:trixie-slim
RUN useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin seabattle
COPY --from=build /out/usr/bin/seabattle /usr/bin/seabattle
COPY --from=build /out/usr/share/seabattle /usr/share/seabattle
COPY --from=build /out/usr/share/doc/seabattle /usr/share/doc/seabattle
USER seabattle
EXPOSE 8090
ENTRYPOINT ["/usr/bin/seabattle", "--host", "0.0.0.0", "--port", "8090"]
