# NAS agent

`tools/nas_agent.jdb` in a container: a headless jdBasic (Node Bus only, no
graphics, HTTP or native compiler) that reports a NAS to `tools/farm_dash.jdb`
with its load, memory, CPU temperature, uptime and volumes.

Build the image from the repo root on any x86-64 Docker host, and export it
for a NAS without a registry:

```bash
docker build -f tools/docker/nas_agent/Dockerfile -t jdbasic-nas-agent .
docker save jdbasic-nas-agent | gzip > jdbasic-nas-agent.tar.gz
```

On the NAS, import `jdbasic-nas-agent.tar.gz` (Synology Container Manager:
Image, Import; UGREEN Docker: Image, Add, Import), then create a project
from `compose.yml` with the key and the name filled in and one line under
`volumes` per volume. The container needs `network_mode: host`: the Node Bus
finds its peers by multicast and broadcast, which do not leave Docker's
bridge network.

The dashboard shows a NAS as `NAS` with its uptime, volumes and temperature;
Sync, Build, Restart and the farm gate leave it out.
