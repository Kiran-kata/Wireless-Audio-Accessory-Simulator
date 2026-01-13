# Connection State Machine Documentation

## Overview

This document describes the connection finite state machine (FSM) that governs the lifecycle of the wireless audio accessory connection, including normal operation, error handling, and recovery scenarios.

## State Diagram

```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    │          Power On / Reset           │
                    │                                     │
                    └───────────────┬─────────────────────┘
                                    │
                                    ▼
                           ┌────────────────┐
                           │                │
                   ┌──────>│      IDLE      │<───────┐
                   │       │                │        │
                   │       └────┬───────────┘        │
                   │            │                    │
                   │            │ DISCOVER_REQUEST   │
                   │            │ received           │
                   │            │                    │
                   │            ▼                    │
                   │       ┌────────────┐            │
                   │       │            │            │
      DISCONNECT   │       │ DISCOVERING│            │
      received     │       │            │            │
      OR timeout   │       └────┬───────┘            │
                   │            │                    │
                   │            │ PAIR_REQUEST       │
                   │            │ received           │
                   │            │                    │
                   │            ▼                    │
                   │       ┌────────────┐            │
                   │       │            │            │
                   │       │  PAIRING   │            │
                   │       │            │            │
                   │       └────┬───────┘            │
                   │            │                    │
                   │            │ CONNECT_REQUEST    │
                   │            │ received           │
                   │            │                    │
                   │            ▼                    │
                   │       ┌────────────┐            │
                   │       │            │            │
                   │       │  CONNECTED │────────────┘
                   │       │            │   Connection
                   │       └────┬───────┘   loss or
                   │            │           error
                   │            │
                   │            │ Start streaming
                   │            │
                   │            ▼
                   │       ┌────────────┐
                   │       │            │
                   │       │ STREAMING  │
                   │       │            │
                   │       └────┬───────┘
                   │            │
                   │            │ DISCONNECT
                   │            │ requested
                   │            │
                   │            ▼
                   │       ┌────────────┐
                   │       │            │
                   └───────│DISCONNECTING│
                           │            │
                           └────────────┘
                                    │
                                    │ Cleanup
                                    │ complete
                                    │
                   ┌────────────────▼────────────────┐
                   │                                  │
                   │            ERROR                 │
                   │     (Connection Loss)            │
                   │                                  │
                   └────┬─────────────────────────────┘
                        │
                        │ Reconnection
                        │ attempt
                        │
                        └────> Back to IDLE
                              (with backoff)
```

## State Descriptions

### IDLE
**Description**: Initial state, no active connection. Waiting for discovery.

**Entry Actions**:
- Clear connection state
- Reset sequence numbers
- Stop all periodic tasks

**Valid Transitions**:
- `IDLE → DISCOVERING`: On receiving DISCOVER_REQUEST

**Characteristics**:
- Low power consumption
- Minimal activity
- Listening for discovery broadcasts

---

### DISCOVERING
**Description**: Device has been discovered by host. Sending device information.

**Entry Actions**:
- Generate DISCOVER_RESPONSE with device info
- Include battery level, capabilities

**Valid Transitions**:
- `DISCOVERING → PAIRING`: On receiving PAIR_REQUEST
- `DISCOVERING → IDLE`: On timeout (no further requests)

**Timeout**: 10 seconds of inactivity returns to IDLE

---

### PAIRING
**Description**: Performing security handshake and key exchange.

**Entry Actions**:
- Generate key pair (simulated ECDH)
- Send PAIR_RESPONSE with public key
- Derive shared secret

**Valid Transitions**:
- `PAIRING → CONNECTED`: On receiving CONNECT_REQUEST
- `PAIRING → IDLE`: On pairing failure or timeout

**Security Operations**:
```
Accessory                           Host
    │                                 │
    │<──── PAIR_REQUEST ──────────────┤
    │      (host_public_key)          │
    │                                 │
    ├─ Generate accessory_key_pair ──┤
    │                                 │
    ├──── PAIR_RESPONSE ─────────────>│
    │      (acc_public_key)           │
    │                                 │
    ├─ Derive shared_secret ──────────┤
    │   = ECDH(acc_priv, host_pub)    │
    │                                 │
    └─ Store session credentials ─────┘
```

---

### CONNECTED
**Description**: Link established, keepalives active. Ready to stream.

**Entry Actions**:
- Start keepalive thread (1 Hz)
- Reset reconnection counters
- Send CONNECT_RESPONSE
- Start telemetry reporting

**Valid Transitions**:
- `CONNECTED → STREAMING`: When streaming starts
- `CONNECTED → DISCONNECTING`: On disconnect request
- `CONNECTED → ERROR`: On keepalive timeout

**Keepalive Mechanism**:
```
Time (seconds)
0         1         2         3         4         5
├─────────┼─────────┼─────────┼─────────┼─────────┤
│         │         │         │         │         │
├──KA─────┼──KA─────┼──KA─────┼──KA─────┼──KA─────┤  (Keepalives sent)
│         │         │         │         │         │
│<───────────── 5s timeout ──────────────>         │
│                                          │       │
│                                    If no KA      │
│                                    received:     │
│                                    → ERROR       │
```

---

### STREAMING
**Description**: Actively streaming audio packets.

**Entry Actions**:
- Start audio streaming thread
- Begin generating audio packets at 10ms intervals
- Set high thread priority

**Active Operations**:
- Audio packet generation: 100 Hz
- Keepalive: 1 Hz
- Battery telemetry: 1 Hz
- Diagnostics telemetry: 0.2 Hz

**Valid Transitions**:
- `STREAMING → CONNECTED`: Stop streaming (but keep connection)
- `STREAMING → DISCONNECTING`: On disconnect request
- `STREAMING → ERROR`: On connection loss

**Resource Usage**:
```
CPU Usage:      2-5%
Thread Priority: HIGH
Memory:         ~5MB
Network BW:     ~800 kbps
```

---

### DISCONNECTING
**Description**: Graceful shutdown in progress.

**Entry Actions**:
- Stop audio streaming
- Stop telemetry
- Send DISCONNECT packet
- Flush buffers

**Valid Transitions**:
- `DISCONNECTING → IDLE`: After cleanup complete

**Cleanup Sequence**:
```
1. Stop audio streaming thread         (wait for thread join)
2. Stop telemetry thread               (wait for thread join)
3. Send DISCONNECT packet              (notify peer)
4. Stop keepalive thread               (wait for thread join)
5. Clear session state                 (free resources)
6. Transition to IDLE                  (ready for next connection)

Total time: ~200ms
```

---

### ERROR
**Description**: Connection lost unexpectedly. Attempting recovery.

**Entry Actions**:
- Log error event
- Stop all streaming/telemetry
- Enter reconnection mode

**Recovery Strategy**:
```
Attempt #   Delay       Action
──────────  ──────────  ────────────────────────────
1           100ms       Quick reconnect (fast-reconnect)
2           200ms       Retry
3           400ms       Retry
4           800ms       Retry
5           1600ms      Retry
6           3200ms      Retry
7+          5000ms      Retry (capped at 5s)

Max Attempts: Unlimited (user intervention to stop)
```

**Valid Transitions**:
- `ERROR → IDLE`: Start reconnection attempt
- `ERROR → DISCOVERING`: If discovery succeeds

**Backoff Algorithm**:
```cpp
uint32_t calculate_backoff(uint32_t attempt) {
    const uint32_t BASE_DELAY = 100;  // ms
    const uint32_t MAX_DELAY = 5000;  // ms
    uint32_t delay = BASE_DELAY * (1 << (attempt - 1));  // Exponential
    return std::min(delay, MAX_DELAY);
}
```

## Event Handling

### Event Matrix

| Current State   | Event                | Action                          | Next State    |
|----------------|----------------------|---------------------------------|---------------|
| IDLE           | DISCOVER_REQUEST     | Send DISCOVER_RESPONSE          | DISCOVERING   |
| DISCOVERING    | PAIR_REQUEST         | Send PAIR_RESPONSE              | PAIRING       |
| DISCOVERING    | Timeout (10s)        | Cleanup                         | IDLE          |
| PAIRING        | CONNECT_REQUEST      | Send CONNECT_RESPONSE           | CONNECTED     |
| PAIRING        | Timeout (5s)         | Cleanup                         | IDLE          |
| CONNECTED      | Start Streaming      | Start audio thread              | STREAMING     |
| CONNECTED      | DISCONNECT           | Graceful shutdown               | DISCONNECTING |
| CONNECTED      | Keepalive Timeout    | Log error, attempt recovery     | ERROR         |
| STREAMING      | DISCONNECT           | Stop streaming, shutdown        | DISCONNECTING |
| STREAMING      | Keepalive Timeout    | Stop streaming, attempt recovery| ERROR         |
| DISCONNECTING  | Cleanup Complete     | Return to idle                  | IDLE          |
| ERROR          | Reconnect Attempt    | Try rediscovery                 | IDLE          |
| Any            | Critical Error       | Emergency cleanup               | ERROR         |

## Concurrency Model

### Thread Interactions

```
┌────────────────────────────────────────────────────────────────┐
│                       Main Thread                               │
│  - State machine management                                     │
│  - State transitions (synchronized)                             │
│  - Event dispatch                                               │
└────┬───────────────────────────────────────────────────────────┘
     │
     ├──> Keepalive Thread
     │      - Sends periodic keepalives (1 Hz)
     │      - Monitors last received keepalive
     │      - Triggers ERROR state on timeout
     │      - Lock: state_mutex_ (brief, for state check)
     │
     ├──> Audio Streaming Thread (only in STREAMING state)
     │      - High priority, real-time
     │      - Generates packets at 10ms intervals
     │      - Independent of state machine
     │      - Stopped on state transition out of STREAMING
     │
     └──> Telemetry Thread (CONNECTED or STREAMING states)
            - Battery reports (1 Hz)
            - Diagnostics (0.2 Hz)
            - Lower priority
            - Lock: data_mutex_ (for telemetry data)
```

### Synchronization

```cpp
class ConnectionFSM {
private:
    std::atomic<ConnectionState> state_;      // Lock-free reads
    std::mutex state_mutex_;                  // For state transitions
    std::mutex data_mutex_;                   // For shared data
    
    void transition_state(ConnectionState new_state) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        // Log transition
        log_transition(state_.load(), new_state);
        
        // Update atomic state
        state_.store(new_state);
        
        // Notify callbacks (if any)
        if (state_callback_) {
            state_callback_(new_state);
        }
    }
};
```

## Error Scenarios and Recovery

### Scenario 1: Sudden Connection Loss

```
Timeline:
0.000s  STREAMING state, audio flowing
0.100s  Network cable unplugged (simulated)
0.200s  Last packet received
1.200s  Keepalive timeout (1s elapsed since last packet)
1.200s  Transition to ERROR state
1.200s  Stop audio streaming
1.300s  Reconnection attempt #1 (100ms delay)
1.400s  Send DISCOVER_REQUEST
1.500s  Timeout (no response)
1.700s  Reconnection attempt #2 (200ms delay)
1.900s  Network restored, DISCOVER_RESPONSE received
2.000s  PAIR_REQUEST/RESPONSE exchange
2.100s  CONNECT_REQUEST/RESPONSE exchange
2.200s  Transition to CONNECTED state
2.700s  Resume STREAMING state
2.800s  Audio flowing again

Total outage: 2.6 seconds (1.2s - 2.8s)
```

### Scenario 2: Intermittent Connection (Packet Loss)

```
State: STREAMING
Symptom: High packet loss rate but connection maintained

Timeline:
0.000s  Normal operation, 0% loss
1.000s  Packet loss begins (5% loss rate)
1.100s  Jitter buffer underrun (host side)
1.100s  Host increases buffer size: 3→4 packets
1.100s  Latency increases: 30ms→40ms
2.000s  Packet loss continues (8% loss rate)
3.000s  Packet loss subsides (2% loss rate)
10.000s 60 seconds of stable operation
70.000s Host decreases buffer size: 4→3 packets
70.000s Latency decreases: 40ms→30ms

State transitions: None (stayed in STREAMING)
Recovery: Automatic buffer adjustment
```

### Scenario 3: Pairing Failure

```
Timeline:
0.000s  IDLE state
0.100s  Receive DISCOVER_REQUEST
0.100s  Transition to DISCOVERING
0.150s  Send DISCOVER_RESPONSE
0.200s  Receive PAIR_REQUEST
0.200s  Transition to PAIRING
0.250s  Generate key pair fails (simulated error)
0.250s  Transition to ERROR
0.250s  Log: "Pairing failed: key generation error"
0.350s  Reconnection attempt #1 (100ms delay)
0.350s  Transition to IDLE
0.400s  Ready for new connection attempt

Recovery time: 250ms
```

## State Machine Implementation

### Pseudo-code

```cpp
class ConnectionFSM {
public:
    void on_event(Event event, const Packet& packet) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        ConnectionState current = state_.load();
        
        switch (current) {
            case IDLE:
                if (event == DISCOVER_REQUEST) {
                    send_discover_response();
                    transition_state(DISCOVERING);
                }
                break;
                
            case DISCOVERING:
                if (event == PAIR_REQUEST) {
                    send_pair_response(packet);
                    transition_state(PAIRING);
                } else if (event == TIMEOUT) {
                    transition_state(IDLE);
                }
                break;
                
            case PAIRING:
                if (event == CONNECT_REQUEST) {
                    send_connect_response();
                    start_keepalive();
                    transition_state(CONNECTED);
                } else if (event == TIMEOUT) {
                    transition_state(IDLE);
                }
                break;
                
            case CONNECTED:
                if (event == START_STREAMING) {
                    start_audio_stream();
                    transition_state(STREAMING);
                } else if (event == DISCONNECT) {
                    transition_state(DISCONNECTING);
                } else if (event == KEEPALIVE_TIMEOUT) {
                    transition_state(ERROR);
                }
                break;
                
            case STREAMING:
                if (event == DISCONNECT) {
                    stop_audio_stream();
                    transition_state(DISCONNECTING);
                } else if (event == KEEPALIVE_TIMEOUT) {
                    stop_audio_stream();
                    transition_state(ERROR);
                }
                break;
                
            case DISCONNECTING:
                if (event == CLEANUP_COMPLETE) {
                    transition_state(IDLE);
                }
                break;
                
            case ERROR:
                if (event == RECONNECT_ATTEMPT) {
                    apply_backoff();
                    transition_state(IDLE);
                }
                break;
        }
    }
    
private:
    void transition_state(ConnectionState new_state) {
        ConnectionState old = state_.exchange(new_state);
        log_transition(old, new_state);
        
        // Entry actions
        switch (new_state) {
            case CONNECTED:
                start_keepalive_thread();
                start_telemetry();
                break;
            case STREAMING:
                start_audio_thread();
                break;
            case ERROR:
                schedule_reconnect();
                break;
            // ... other entry actions
        }
        
        // Exit actions
        switch (old) {
            case STREAMING:
                stop_audio_thread();
                break;
            case CONNECTED:
            case STREAMING:
                stop_keepalive_thread();
                stop_telemetry();
                break;
            // ... other exit actions
        }
    }
};
```

## Testing Scenarios

### Unit Tests

1. **State Transitions**
   - Verify all valid transitions work
   - Verify invalid transitions are rejected
   - Verify entry/exit actions execute

2. **Timeout Handling**
   - Keepalive timeout triggers ERROR
   - Discovery timeout returns to IDLE
   - Pairing timeout returns to IDLE

3. **Reconnection Logic**
   - Exponential backoff works correctly
   - Max delay is capped at 5 seconds
   - Reconnection resets counters on success

### Integration Tests

1. **Normal Flow**
   - IDLE → DISCOVERING → PAIRING → CONNECTED → STREAMING
   - Verify audio flows correctly
   - Clean disconnect returns to IDLE

2. **Connection Loss**
   - Simulate keepalive timeout
   - Verify transition to ERROR
   - Verify reconnection succeeds

3. **Stress Test**
   - Rapid connect/disconnect cycles
   - Multiple connection loss events
   - Verify no resource leaks

## Metrics and Monitoring

### State Durations (Typical)

```
State           Min Duration    Typical Duration    Max Duration
──────────────  ──────────────  ──────────────────  ────────────
IDLE            0ms (transient) Variable            Unlimited
DISCOVERING     50ms            100ms               10s (timeout)
PAIRING         50ms            200ms               5s (timeout)
CONNECTED       500ms           Variable            Until disconnect
STREAMING       1s              Minutes to hours    Until disconnect
DISCONNECTING   50ms            200ms               500ms
ERROR           100ms           1-5s (backoff)      Unlimited
```

### Logged Events

```
[timestamp] [TRANSITION] IDLE → DISCOVERING
[timestamp] [EVENT] Received DISCOVER_REQUEST from host
[timestamp] [ACTION] Sent DISCOVER_RESPONSE (device: AudioSim-A1B2)
[timestamp] [TRANSITION] DISCOVERING → PAIRING
[timestamp] [EVENT] Received PAIR_REQUEST
[timestamp] [ACTION] Generated key pair, sent PAIR_RESPONSE
[timestamp] [TRANSITION] PAIRING → CONNECTED
[timestamp] [ACTION] Started keepalive thread
[timestamp] [TRANSITION] CONNECTED → STREAMING
[timestamp] [ACTION] Started audio streaming thread
[timestamp] [ERROR] Keepalive timeout (5000ms elapsed)
[timestamp] [TRANSITION] STREAMING → ERROR
[timestamp] [ACTION] Stopped audio streaming
[timestamp] [ACTION] Reconnection attempt #1 (delay: 100ms)
[timestamp] [TRANSITION] ERROR → IDLE
```
