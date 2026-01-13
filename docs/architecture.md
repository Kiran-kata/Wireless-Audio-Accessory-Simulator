# System Architecture

## Overview

This document describes the architecture of the Wireless Audio Accessory Simulator, which mimics the behavior of AirPods-like wireless audio devices with a focus on real-time constraints, connection management, and audio streaming.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          HOST SIDE (macOS/Linux)                     │
│                                                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                      Host Daemon Process                      │   │
│  │                                                                │   │
│  │  ┌───────────────┐  ┌──────────────┐  ┌──────────────────┐  │   │
│  │  │Device Manager │  │  Audio Sync  │  │Telemetry         │  │   │
│  │  │               │  │              │  │Processor         │  │   │
│  │  │ • Discovery   │  │ • Jitter     │  │                  │  │   │
│  │  │ • Pairing     │  │   Buffer     │  │ • Battery        │  │   │
│  │  │ • Connection  │  │ • Sync       │  │ • Diagnostics    │  │   │
│  │  │ • State Mgmt  │  │ • Playback   │  │ • Logging        │  │   │
│  │  └───────┬───────┘  └──────┬───────┘  └────────┬─────────┘  │   │
│  │          │                  │                    │            │   │
│  │          └──────────────────┴────────────────────┘            │   │
│  │                             │                                 │   │
│  │                    ┌────────▼────────┐                        │   │
│  │                    │   Transport     │                        │   │
│  │                    │  (UDP Socket)   │                        │   │
│  │                    └────────┬────────┘                        │   │
│  └─────────────────────────────┼─────────────────────────────────┘   │
└────────────────────────────────┼─────────────────────────────────────┘
                                 │
                         ┌───────▼────────┐
                         │ Wireless Link  │
                         │ (Simulated BT) │
                         └───────┬────────┘
                                 │
┌────────────────────────────────┼─────────────────────────────────────┐
│                                │           ACCESSORY SIDE             │
│  ┌─────────────────────────────▼─────────────────────────────────┐   │
│  │                   Accessory Simulator Process                  │   │
│  │             (Embedded Firmware Simulation - C/C++)             │   │
│  │                                                                 │   │
│  │  ┌───────────────┐  ┌──────────────┐  ┌──────────────────┐   │   │
│  │  │Connection FSM │  │Audio Streamer│  │  Telemetry       │   │   │
│  │  │               │  │              │  │                  │   │   │
│  │  │ • State       │  │ • Generation │  │ • Battery Sim    │   │   │
│  │  │   Machine     │  │ • Packetize  │  │ • Status Report  │   │   │
│  │  │ • Reconnect   │  │ • Timing     │  │ • Diagnostics    │   │   │
│  │  │ • Keepalive   │  │              │  │                  │   │   │
│  │  └───────┬───────┘  └──────┬───────┘  └────────┬─────────┘   │   │
│  │          │                  │                    │             │   │
│  │          └──────────────────┴────────────────────┘             │   │
│  │                             │                                  │   │
│  │                    ┌────────▼────────┐                         │   │
│  │                    │   Transport     │                         │   │
│  │                    │  (UDP Socket)   │                         │   │
│  │                    └─────────────────┘                         │   │
│  │                                                                 │   │
│  │  ┌──────────────┐                                              │   │
│  │  │    Crypto    │  (Simulated encryption/key exchange)         │   │
│  │  └──────────────┘                                              │   │
│  └─────────────────────────────────────────────────────────────────   │
└─────────────────────────────────────────────────────────────────────┘
```

## Component Details

### Host Side Components

#### Device Manager
**Responsibility**: Discovery, pairing, and connection lifecycle management

**Key Functions**:
- Broadcasts DISCOVER_REQUEST packets periodically
- Maintains list of discovered devices
- Handles pairing handshake (simulated key exchange)
- Manages connection state transitions
- Sends keepalive packets to maintain connection

**State Flow**:
```
IDLE → DISCOVERING → PAIRING → CONNECTED → STREAMING
  ↑                                            ↓
  └────────────────DISCONNECT──────────────────┘
```

#### Audio Sync
**Responsibility**: Audio stream synchronization and playback

**Key Features**:
- **Jitter Buffer**: Adaptive buffer (2-4 packets) to handle network jitter
- **Packet Reordering**: Maintains sequence order
- **Loss Detection**: Identifies missing packets via sequence gaps
- **Latency Tracking**: Monitors end-to-end latency
- **Buffer Adaptation**: Increases buffer size on consecutive losses

**Algorithm**:
1. Buffer incoming audio packets by sequence number
2. Wait until buffer reaches target size before starting playback
3. Play packets in sequence order
4. Detect and handle packet loss
5. Adjust buffer size based on loss patterns

#### Telemetry Processor
**Responsibility**: Ingest and log telemetry data

**Features**:
- Processes battery status updates
- Processes diagnostic reports (packet stats, RSSI, link quality)
- Maintains time-series log file
- Alerts on critical events (low battery, high loss rate)

### Accessory Side Components

#### Connection FSM
**Responsibility**: Connection state machine and protocol management

**States**:
- `IDLE`: No connection, waiting for discovery
- `DISCOVERING`: Responding to discovery requests
- `PAIRING`: Performing key exchange
- `CONNECTED`: Link established, sending keepalives
- `STREAMING`: Actively streaming audio
- `DISCONNECTING`: Graceful shutdown
- `ERROR`: Connection loss, attempting recovery

**Recovery Logic**:
- Detects connection loss via keepalive timeout
- Implements exponential backoff (100ms → 5000ms)
- Fast-reconnect mode for known hosts

#### Audio Streamer
**Responsibility**: Generate and transmit audio packets

**Timing**:
- Generates packets at fixed 10ms intervals
- Uses high-resolution timers for precise scheduling
- Maintains sequence numbers and stream timestamps
- Simulates audio data (sine wave @ 440Hz)

**Real-time Considerations**:
- Priority scheduling for streaming thread
- Zero-copy packet assembly where possible
- Pre-allocated buffers to avoid runtime allocation

#### Telemetry
**Responsibility**: Report device status

**Metrics Reported**:
- Battery: level, voltage, current, temperature, time remaining
- Diagnostics: packet counts, loss rate, RSSI, latency

**Update Rates**:
- Battery: 1 Hz during streaming, 0.1 Hz idle
- Diagnostics: 0.2 Hz
- Simulated battery drain: Level decreases every 10 seconds

#### Crypto Module
**Responsibility**: Security operations (simulated)

**Operations**:
- Key generation (simulated ECDH)
- Shared secret derivation
- AES-128 encryption/decryption (simplified)
- HMAC computation

*Note: In production, use hardware crypto or libraries like mbedTLS*

## Protocol Layer

### Transport
- **Medium**: UDP sockets (simulating Bluetooth L2CAP)
- **Port**: 8888 (accessory listens, host connects)
- **Packet Format**: See protocol.h for detailed format

### Packet Types

| Category | Packet Type | Direction | Purpose |
|----------|------------|-----------|---------|
| Connection | DISCOVER_REQUEST | Host → Acc | Find devices |
| | DISCOVER_RESPONSE | Acc → Host | Announce presence |
| | PAIR_REQUEST | Host → Acc | Initiate pairing |
| | PAIR_RESPONSE | Acc → Host | Complete pairing |
| | CONNECT_REQUEST | Host → Acc | Establish connection |
| | CONNECT_RESPONSE | Acc → Host | Confirm connection |
| | DISCONNECT | Bidirectional | Terminate connection |
| | KEEPALIVE | Bidirectional | Maintain connection |
| Audio | AUDIO_DATA | Acc → Host | Audio samples |
| | AUDIO_ACK | Host → Acc | Acknowledge receipt |
| | AUDIO_RETRANSMIT | Host → Acc | Request retransmission |
| Telemetry | BATTERY_STATUS | Acc → Host | Battery information |
| | DIAGNOSTICS | Acc → Host | Link diagnostics |
| Security | KEY_EXCHANGE | Bidirectional | Key negotiation |
| | ENCRYPTED_PACKET | Bidirectional | Encrypted data |

## Data Flow Examples

### Connection Establishment
```
Host                                    Accessory
  │                                         │
  ├──── DISCOVER_REQUEST ──────────────────>│
  │                                         │
  │<──────── DISCOVER_RESPONSE ─────────────┤
  │                                         │
  ├──── PAIR_REQUEST ──────────────────────>│
  │      (host public key)                  │
  │                                         │
  │<──────── PAIR_RESPONSE ──────────────────┤
  │      (accessory public key)             │
  │                                         │
  ├──── CONNECT_REQUEST ────────────────────>│
  │                                         │
  │<──────── CONNECT_RESPONSE ───────────────┤
  │                                         │
  ├<────── KEEPALIVE ──────────────────────>┤
  │       (every 1 second)                  │
```

### Audio Streaming
```
Accessory                               Host
  │                                      │
  ├──── AUDIO_DATA (seq=1) ─────────────>│
  │                                      │ [Buffer]
  ├──── AUDIO_DATA (seq=2) ─────────────>│
  │                                      │ [Buffer]
  ├──── AUDIO_DATA (seq=3) ─────────────>│
  │                                      │ [Buffer full, start playback]
  ├──── AUDIO_DATA (seq=4) ─────────────>│
  │                                      │ [Play seq=1]
  ├──── AUDIO_DATA (seq=5) ─────────────>│
  │                                      │ [Play seq=2]
  │                                      │
  │ [Packet seq=6 lost]                 │
  ├──── AUDIO_DATA (seq=7) ─────────────>│
  │                                      │ [Detect gap, wait for seq=6]
  │                                      │ [Timeout, mark lost]
  │                                      │ [Play seq=4, 5, 7...]
```

## Threading Model

### Accessory Side
- **Main Thread**: Initialization and coordination
- **Transport RX Thread**: Receives packets from network
- **Transport TX Thread**: Sends packets to network
- **Audio Streaming Thread**: Generates audio packets at 10ms intervals
- **Telemetry Thread**: Sends periodic telemetry
- **Keepalive Thread**: Connection monitoring and keepalive

### Host Side
- **Main Thread**: Initialization and status reporting
- **Transport RX Thread**: Receives packets from network
- **Transport TX Thread**: Sends packets to network
- **Discovery Thread**: Periodic device discovery
- **Audio Sync Thread**: Jitter buffer management and playback
- **Keepalive Thread**: Connection monitoring and keepalive

## Memory Management

### Buffer Pools
- Audio packets: Pre-allocated fixed-size buffers
- Protocol packets: Maximum size defined (2048 bytes)
- Jitter buffer: Map-based storage for out-of-order packets

### Zero-Copy Paths
- Packet serialization: Direct memory copy when possible
- Audio data: Minimal copying between layers

## Performance Characteristics

| Metric | Target | Typical | Worst Case |
|--------|--------|---------|------------|
| Audio Latency | <30ms | 25-30ms | 50ms (max buffer) |
| Reconnection Time | <500ms | 200-400ms | 5s (after backoff) |
| Packet Loss Tolerance | 10% | Graceful degradation | Increased latency |
| CPU Usage (Host) | <5% | 2-3% | - |
| CPU Usage (Accessory) | <5% | 2-3% | - |
| Memory (Host) | <20MB | ~10MB | - |
| Memory (Accessory) | <10MB | ~5MB | - |

## Porting to Real Hardware

### Replacing Transport Layer
```cpp
// Current: UDP socket simulation
class SimulatedBluetooth : public Transport { ... };

// Real hardware: Bluetooth stack
class ESP32Bluetooth : public Transport {
    esp_bd_addr_t peer_addr;
    uint16_t conn_handle;
    // Implement using ESP-IDF Bluetooth APIs
};
```

### RTOS Integration
```cpp
// Current: std::thread
std::thread audio_thread;

// Zephyr RTOS
K_THREAD_DEFINE(audio_thread_id, AUDIO_STACK_SIZE,
                audio_thread_entry, NULL, NULL, NULL,
                AUDIO_THREAD_PRIORITY, 0, 0);
```

### Hardware Crypto
```cpp
// Current: Simulated crypto
Crypto::encrypt_aes128(...);

// Hardware accelerated (e.g., ESP32)
mbedtls_aes_context aes_ctx;
mbedtls_aes_setkey_enc(&aes_ctx, key, 128);
mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_ENCRYPT, ...);
```

## Extensibility

### Adding New Packet Types
1. Define packet type in `protocol.h`
2. Add payload structure if needed
3. Implement handler in both host and accessory
4. Update packet routing in main loops

### Custom Audio Codecs
```cpp
class AudioCodec {
public:
    virtual void encode(const int16_t* pcm, uint8_t* compressed) = 0;
    virtual void decode(const uint8_t* compressed, int16_t* pcm) = 0;
};

class AACCodec : public AudioCodec { ... };
class OpusCodec : public AudioCodec { ... };
```

### Enhanced Security
- Implement proper ECDH using mbedTLS
- Add certificate-based authentication
- Implement key rotation
- Add replay attack protection
