<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# L2 counters

Registry of named L2 counters (gNB and UE) with periodic export to a JSON file,
and a Telegraf/InfluxDB/Grafana stack to plot them.

## API

See `l2_counters.h`. Counters are identified by `(group, name)` and are either
monotonic (`L2C_COUNTER`) or instantaneous (`L2C_GAUGE`). Setters/getters are
lock-free and accept `NULL`, so call sites need no check when the export is disabled.

## Configuration

Section `l2_counters`, also settable from the command line:

| parameter   | default                    |                          |
|-------------|----------------------------|--------------------------|
| `enabled`   | `0`                        | enable the JSON export   |
| `period_ms` | `1000`                     | export period            |
| `path`      | `l2_counters_<node>.json`  | output file (`gNB`/`UE`) |

```
--l2_counters.enabled 1 --l2_counters.period_ms 1000 --l2_counters.path /tmp/l2_counters/gnb.json
```

The file is replaced atomically (written to `<path>.tmp`, then renamed):

```json
{
  "node": "gNB",
  "timestamp_ms": 1793377027267,
  "period_ms": 1000,
  "counters": {
    "mac0": {
      "dl_tx_bytes": 2500000,
      "dl_tx_rbs": 16000,
      "ul_rx_bytes": 500000,
      "ul_sched_rbs": 6000
    }
  }
}
```

MAC counters (group `mac<instance>`):

| node | counter        | meaning                                   |
|------|----------------|-------------------------------------------|
| gNB  | `dl_tx_bytes`  | bytes of new (non-retransmitted) DL TBs   |
| gNB  | `dl_tx_rbs`    | RBs of new DL TBs                         |
| gNB  | `ul_rx_bytes`  | bytes of UL TBs received from PHY         |
| gNB  | `ul_sched_rbs` | RBs of newly scheduled UL TBs             |
| UE   | `dl_rx_bytes`  | bytes of DL MAC PDUs received             |
| UE   | `ul_tx_bytes`  | bytes of UL TBs sent to PHY               |

## Grafana

```bash
mkdir -p /tmp/l2_counters
cd common/utils/l2_counters/monitoring
docker compose up -d
```

Grafana: <http://localhost:3001>, dashboard *OAI → OAI L2 counters*. Viewing needs no
login; to edit, sign in as admin/admin.
Telegraf reads `/tmp/l2_counters/*.json` (override with `L2C_DATA_DIR`) every
second (`TELEGRAF_INTERVAL`) and writes measurement `l2_counters` with tags
`node`, `group` and one field per counter into InfluxDB (bucket `l2_counters`,
org `oai`). Throughput panels are `derivative()` of the counters; a step of the
system clock (e.g. WSL time resync) shows up as a spike there.

## Run in SA over the ZMQ radio

Build with the ZMQ radio driver (needs `libzmq3-dev`):

```bash
cd build
cmake .. -GNinja -DOAI_ZMQ=ON
ninja nr-softmodem nr-uesoftmodem oai_zmqdevif
```

Start the core network (OAI CN5G docker compose, AMF at 192.168.70.132) and the
monitoring stack, then from `build/`:

```bash
sudo ./nr-softmodem -O ../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf \
  --gNBs.[0].min_rxtxtime 6 \
  --device.name oai_zmqdevif --zmq.[0].tx_channels tcp://127.0.0.1:4556 --zmq.[0].rx_channels tcp://127.0.0.1:4557 \
  --l2_counters.enabled 1 --l2_counters.path /tmp/l2_counters/gnb.json

sudo ./nr-uesoftmodem -O ../targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.conf --uicc0.imsi 001010000000001 \
  -r 106 --numerology 1 --band 78 -C 3619200000 --ssb 516 \
  --device.name oai_zmqdevif --zmq.[0].tx_channels tcp://127.0.0.1:4557 --zmq.[0].rx_channels tcp://127.0.0.1:4556 \
  --l2_counters.enabled 1 --l2_counters.path /tmp/l2_counters/ue.json
```

The IMSI must exist in the core database. Generate traffic once the PDU session
is up, e.g. `ping -I oaitun_ue1 192.168.70.135` or iperf3 against `oai-ext-dn`.
