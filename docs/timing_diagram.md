# Audio Packet Timing Diagram

## Overview

This document provides detailed timing diagrams for audio packet transmission, buffering, and playback, illustrating the latency characteristics and real-time constraints of the system.

## End-to-End Audio Pipeline

```
┌──────────────────────────────────────────────────────────────────────┐
│                         ACCESSORY SIDE                                │
├──────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  Audio                 Packetization          Network                │
│  Generation            & Encoding             Transmission           │
│     │                      │                      │                  │
│     ▼                      ▼                      ▼                  │
│  ┌─────┐              ┌────────┐            ┌─────────┐             │
│  │ PCM │─────────────>│ Packet │───────────>│ TX Queue│             │
│  │ Gen │  10ms chunks │ Builder│   Seq #    │  (UDP)  │             │
│  └─────┘              └────────┘            └────┬────┘             │
│                                                   │                  │
│                                             ~1ms network latency     │
│                                                   │                  │
└───────────────────────────────────────────────────┼──────────────────┘
                                                    │
┌───────────────────────────────────────────────────┼──────────────────┐
│                         HOST SIDE                 │                  │
├───────────────────────────────────────────────────┼──────────────────┤
│                                                   ▼                  │
│  Network              Jitter Buffer            Playback              │
│  Reception            (Reordering)             Engine                │
│     │                      │                      │                  │
│     ▼                      ▼                      ▼                  │
│  ┌─────────┐          ┌─────────┐           ┌─────────┐            │
│  │ RX Queue│─────────>│  Buffer │──────────>│ Audio   │            │
│  │  (UDP)  │  Deser.  │ 2-4 pkt │  In-order │ Device  │            │
│  └─────────┘          └─────────┘           └─────────┘            │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘

Total Latency Budget:
  Generation:        ~0ms   (pre-generated)
  Packetization:     <1ms   (memory copy)
  TX Queue:          <1ms   (UDP send)
  Network:           1-5ms  (simulated Bluetooth)
  RX Processing:     <1ms   (deserialize)
  Jitter Buffer:     20-40ms (2-4 packets @ 10ms each)
  Playback:          ~0ms   (stream to device)
  ────────────────────────────────────────────────
  Total:             25-50ms (target: <30ms typical)
```

## Detailed Packet Timeline

### Successful Transmission (No Loss)

```
Time →
0ms          10ms         20ms         30ms         40ms         50ms

Accessory:
│            │            │            │            │            │
├─Gen Pkt 0──┼─Gen Pkt 1──┼─Gen Pkt 2──┼─Gen Pkt 3──┼─Gen Pkt 4──┼
│  TX────┐   │  TX────┐   │  TX────┐   │  TX────┐   │  TX────┐   │
│        │   │        │   │        │   │        │   │        │   │
│        └───┼────────└───┼────────└───┼────────└───┼────────└───┼
│            │            │            │            │            │

Network:
│   ~2ms latency per packet                                      │
│            │            │            │            │            │
│     ┌──────┼────┌───────┼────┌───────┼────┌───────┼────┌──────┼
│     │      │    │       │    │       │    │       │    │      │

Host:
│     │      │    │       │    │       │    │       │    │      │
│     RX Pkt 0    │RX Pkt 1   │RX Pkt 2   │RX Pkt 3   │RX Pkt 4
│     │ Buffer    ││ Buffer    ││ Buffer   ││ Buffer   ││ Buffer
│     │ [0]       ││ [0,1]     ││ [0,1,2]  ││ [1,2,3]  ││ [2,3,4]
│     │           ││           ││ FULL!    ││          ││
│     │           ││           │└─Play 0───┼└─Play 1───┼└─Play 2
│     │           ││           │            │           │
│     └───────────┴┴───────────┴────────────┴───────────┴────────

Buffer State:
Packet:  0    1    2    3    4    5
State:   [Buf][Buf][Buf][Play][Play][...]
Size:    1    2    3    3    3    3
```

### Packet Loss Scenario

```
Time →
0ms          10ms         20ms         30ms         40ms         50ms

Accessory:
│            │            │            │            │            │
├─Gen Pkt 0──┼─Gen Pkt 1──┼─Gen Pkt 2──┼─Gen Pkt 3──┼─Gen Pkt 4──┼
│  TX────┐   │  TX────┐   │  TX────✗   │  TX────┐   │  TX────┐   │
│        │   │        │   │   LOST!    │        │   │        │   │
│        └───┼────────└───┼────────────┼────────└───┼────────└───┼
│            │            │            │            │            │

Host:
│            │            │            │            │            │
│     RX Pkt 0    RX Pkt 1    [wait]   │    RX Pkt 3   RX Pkt 4  │
│     │ Buffer    │ Buffer    │ Buffer │    │ Buffer   │ Buffer  │
│     │ [0]       │ [0,1]     │ [0,1]  │    │ [0,1,3]  │ [1,3,4] │
│     │           │           │ FULL!  │    │          │         │
│     │           │           └─Play 0─┼────└─Play 1───┼         │
│     │           │                    │    GAP!       │         │
│     │           │                    │    Wait 20ms──┼         │
│     │           │                    │    for Pkt 2  │         │
│     │           │                    │               └─Timeout │
│     │           │                    │                 Drop 2  │
│     │           │                    │                 Play 3  │
│     │           │                    │                         │
│                                      │                         │
│                         Detect Loss──┴─────Increase Buffer─────┤
│                         Adjust: 3 → 4 packets                  │

Result: 
- Packet 2 lost, detected at t=50ms
- Brief silence/interpolation during gap
- Buffer size increased to 4 packets (40ms latency)
- More robust against future losses
```

## Latency Components Breakdown

### Per-Packet Processing Time

```
┌────────────────────────────────────────────────────────────────┐
│                    ACCESSORY PROCESSING                         │
├───────────────┬────────────────────────────────────────────────┤
│ Audio Gen     │ ░░░░░░░░░░ 0.1ms (sine wave generation)       │
│ Serialize     │ ░░░░ 0.05ms (memcpy to packet buffer)         │
│ Checksum      │ ░░ 0.02ms (simple additive checksum)          │
│ Enqueue       │ ░ 0.01ms (queue push + notify)                │
│ Network TX    │ ░░░░░░░░░░░░░░░░░░░░░ 0.5ms (UDP send)        │
├───────────────┼────────────────────────────────────────────────┤
│ TOTAL         │ ███████████████████ 0.68ms                    │
└───────────────┴────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    NETWORK TRANSIT                              │
├───────────────┬────────────────────────────────────────────────┤
│ Loopback      │ ░░░░░ <1ms (localhost UDP)                    │
│ Real BT       │ ░░░░░░░░░░░░░░░░░░░░░░░░░░ 2-5ms (typical)    │
│ Congested     │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 5-20ms     │
└───────────────┴────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    HOST PROCESSING                              │
├───────────────┬────────────────────────────────────────────────┤
│ Network RX    │ ░░░░░░░░░░░░░░░░░░░░░ 0.3ms (UDP recv)        │
│ Deserialize   │ ░░░░ 0.05ms (memcpy from buffer)              │
│ Checksum      │ ░░ 0.02ms (verify)                            │
│ Buffer Insert │ ░░ 0.01ms (map insert)                        │
│ Playback Prep │ ░░░░ 0.05ms (buffer lookup)                   │
├───────────────┼────────────────────────────────────────────────┤
│ TOTAL         │ ████████████████ 0.43ms                       │
└───────────────┴────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    JITTER BUFFER DELAY                          │
├───────────────┬────────────────────────────────────────────────┤
│ 2 packets     │ ████████████████████ 20ms (minimum)           │
│ 3 packets     │ ██████████████████████████████ 30ms (default) │
│ 4 packets     │ ████████████████████████████████████████ 40ms │
└───────────────┴────────────────────────────────────────────────┘
```

## Real-Time Constraints

### Audio Streaming Thread (Accessory)

```
Thread Priority: HIGH (real-time priority on RTOS)
Period: 10ms (100 Hz)
Deadline: MUST complete within 10ms to maintain stream

Timeline per period:
0ms    1ms    2ms    3ms    4ms    5ms    6ms    7ms    8ms    9ms   10ms
│      │      │      │      │      │      │      │      │      │      │
├─Wake─┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼─Sleep┤
│      │                                                         │      │
│      └─Gen Audio─┬─Packetize─┬─Send─┬────────[Idle]───────────┘      │
│      (0.1ms)     │  (0.1ms)  │(0.5ms)                                │
│                  └───────────┴───────┘                                │
│                   Total: 0.7ms                                        │
│                   Margin: 9.3ms (93% idle)                            │
│                                                                       │
└─Next Period─────────────────────────────────────────────────────────>│

Jitter Tolerance: ±0.5ms
- Earlier: Packet sent early (acceptable)
- Later: Introduces latency jitter (buffered by host)
- >10ms late: Missed deadline, affects audio quality
```

### Playback Thread (Host)

```
Thread Priority: MEDIUM-HIGH
Period: Triggered by packet arrival + buffer ready
Deadline: Soft real-time (best effort)

Event-driven processing:
┌─Packet Arrives─────────────────────────────────────────────────┐
│                                                                 │
│  ┌─────────┐    ┌──────────┐    ┌────────┐    ┌──────────┐   │
│  │ Receive │───>│  Buffer  │───>│ Check  │───>│Playback? │   │
│  │ Packet  │    │  Insert  │    │ Seq #  │    │  Ready   │   │
│  └─────────┘    └──────────┘    └────────┘    └─────┬────┘   │
│                                                       │         │
│                                                  ┌────▼────┐   │
│                                             YES  │  Play   │   │
│                                           ┌─────┤  Packet │   │
│                                           │     └─────────┘   │
│                                           │                   │
│                                           │  ┌─────────────┐  │
│                                           └─>│ Next in Seq │  │
│                                              └─────────────┘  │
└─────────────────────────────────────────────────────────────┘

Processing time budget per packet: <1ms
Total time from RX to audio device: <2ms
```

## Synchronization Strategy

### Clock Drift Compensation

```
Accessory Clock                     Host Clock
(Stream time)                       (Playback time)

t=0ms     ────────────────────────> t=25ms (buffered start)
│                                   │
│ Pkt 0 (0ms)                      │ Play Pkt 0 (25ms)
│                                   │
t=10ms    ────────────────────────> t=35ms
│ Pkt 1 (10ms)                     │ Play Pkt 1 (35ms)
│                                   │
t=20ms    ────────────────────────> t=45ms
│ Pkt 2 (20ms)                     │ Play Pkt 2 (45ms)
│                                   │

Drift detection:
- Track delta between stream_timestamp and playback_timestamp
- If delta grows: Accessory clock faster → drop packets
- If delta shrinks: Host clock faster → insert silence
- Acceptable drift: ±5ms before correction
```

### Adaptive Buffer Sizing

```
Packet Loss Rate          Buffer Size        Latency Trade-off
─────────────────────────────────────────────────────────────
0-1%                      2 packets          20ms (best for gaming)
1-5%                      3 packets          30ms (default)
5-10%                     4 packets          40ms (congested network)
>10%                      Disconnect         Connection unusable

Adjustment Algorithm:
1. Start with 3-packet buffer (30ms latency)
2. Monitor consecutive packet losses
3. If 3+ consecutive losses:
   → Increase buffer size
   → Log event
4. If 0 losses for 60 seconds AND buffer > 3:
   → Decrease buffer size
   → Gradual reduction
```

## Performance Measurements

### Latency Distribution (Simulated)

```
Latency (ms)
 50 │                                                      ┌──┐
    │                                                      │  │
 40 │                                              ┌──┐    │  │
    │                                              │  │    │  │
 30 │                                      ┌──┐    │  │    │  │
    │                              ┌──┐    │  │    │  │    │  │
 20 │                      ┌──┐    │  │    │  │    │  │    │  │
    │              ┌──┐    │  │    │  │    │  │    │  │    │  │
 10 │      ┌──┐    │  │    │  │    │  │    │  │    │  │    │  │
    │  ┌──┐│  │    │  │    │  │    │  │    │  │    │  │    │  │
  0 └──┴──┴┴──┴────┴──┴────┴──┴────┴──┴────┴──┴────┴──┴────┴──┴──
     0%  5%  10%   20%   30%   40%   50%   60%   70%   80%   90%
                        Percentile

P50 (median):    28ms
P90:             32ms
P99:             45ms
P99.9:           50ms (max buffer)
```

### Throughput Analysis

```
Audio Format:        PCM 16-bit, 48kHz
Samples per packet:  480 (10ms @ 48kHz)
Bytes per packet:    960 (480 samples × 2 bytes)
Packets per second:  100
Audio bitrate:       768 kbps (960 bytes × 8 × 100)

With protocol overhead:
Payload:            960 bytes
Protocol header:    16 bytes
UDP header:         8 bytes
IP header:          20 bytes
Total per packet:   1004 bytes
Network bitrate:    803.2 kbps

Efficiency: 768 / 803.2 = 95.6% (good!)
```

## Debugging Timeline Example

### Connection Loss and Recovery

```
Time     Event                          Accessory State    Host State
───────  ──────────────────────────────  ────────────────  ─────────────
0.000s   Connection established         STREAMING         STREAMING
0.100s   Audio flowing normally         STREAMING         STREAMING
1.250s   [NETWORK DISRUPTION]           STREAMING         STREAMING
1.260s   Host keepalive timeout         STREAMING         ERROR
1.260s   Host: "Connection lost"        STREAMING         RECONNECTING
1.350s   Accessory keepalive timeout    ERROR             RECONNECTING
1.350s   Accessory: "Link lost"         ERROR             RECONNECTING
1.450s   Accessory: Attempt #1 (100ms)  DISCOVERING       DISCOVERING
1.460s   DISCOVER_REQUEST sent          DISCOVERING       DISCOVERING
1.462s   DISCOVER_RESPONSE received     DISCOVERING       DISCOVERING
1.550s   PAIR_REQUEST/RESPONSE          PAIRING           PAIRING
1.650s   CONNECT_REQUEST/RESPONSE       CONNECTED         CONNECTED
1.700s   Resume audio streaming         STREAMING         STREAMING
1.750s   Audio flowing again            STREAMING         STREAMING

Total outage: 500ms (1.25s - 1.75s)
Recovery time: 450ms from first detection
```

## Optimization Opportunities

### Zero-Copy Audio Path
```
Current:
  Gen buffer → Copy to packet → Copy to TX queue → Send

Optimized:
  Gen directly into TX buffer → Send (saves 2 copies)
  Savings: ~0.1ms per packet
```

### Batch Processing
```
Current:
  Process packets one at a time

Optimized:
  Process multiple packets in single wake-up
  Trade-off: Increased latency for higher throughput
  Use case: Non-interactive streaming (music playback)
```

### Hardware Offload
```
Software encoding:    1-2ms per packet
Hardware codec:       0.1ms per packet
Savings:             0.9-1.9ms (significant for <30ms budget)
```
