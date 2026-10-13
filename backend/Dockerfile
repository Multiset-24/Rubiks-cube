# syntax=docker/dockerfile:1
#
# Multi-stage build for the Zen Cube API.
#   build:   compile, run the test suite, precompute the corner pattern database
#   runtime: slim image with the binary, the data file and a non-root user
#
# Build from the repository root (the engine lives outside backend/):
#   docker build -f backend/Dockerfile -t zen-cube-api .

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake ninja-build libssl-dev libpq-dev libgtest-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY engine ./engine
COPY backend ./backend
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    # A failing test fails the image build, so a broken commit never deploys.
    # (The full 88M-state database check is skipped here: the next step builds it.)
    && ./build/backend/zencube_tests --gtest_brief=1 --gtest_filter='-CornerDatabase.FullBuild*' \
    && ./build/backend/src/server/zencube_server --build-tables --data-dir /out/data

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libpq5 libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home zencube
WORKDIR /app
COPY --from=build /src/build/backend/src/server/zencube_server /app/zencube_server
COPY --from=build /out/data /app/data
ARG APP_VERSION=dev
ENV APP_VERSION=${APP_VERSION} \
    DATA_DIR=/app/data \
    PORT=8080
USER zencube
EXPOSE 8080
CMD ["/app/zencube_server"]
