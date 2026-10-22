# The NAS agent of the build farm: a headless jdBasic (Node Bus only) that
# runs tools/nas_agent.jdb. Build from the repo root:
#
#   docker build -f tools/docker/nas_agent/Dockerfile -t jdbasic-nas-agent .
#
# and run it as in compose.yml next to this file.

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY build.sh ./
COPY libs/eigen ./libs/eigen
COPY src ./src
RUN HTTP=0 NET=1 GFX=0 IMGUI=0 NATIVEC=0 ./build.sh && strip build/jdbasic

FROM debian:bookworm-slim
COPY --from=build /src/build/jdbasic /opt/jdbasic/jdbasic
COPY tools/nas_agent.jdb /opt/jdbasic/nas_agent.jdb
ENTRYPOINT ["/opt/jdbasic/jdbasic", "/opt/jdbasic/nas_agent.jdb"]
