# Test Scenarios for Wireless Audio Accessory Simulator

This document provides test scenarios to validate the system's behavior.

## Manual Testing Scenarios

### Test 1: Basic Connection Flow

**Objective**: Verify normal connection establishment and audio streaming

**Steps**:
1. Start accessory simulator: `./build/accessory_simulator`
2. Wait 2 seconds
3. Start host daemon: `./build/host_daemon`
4. Observe connection sequence

**Expected Output**:
```
Accessory:
- State transition: IDLE → DISCOVERING
- State transition: DISCOVERING → PAIRING
- State transition: PAIRING → CONNECTED
- State transition: CONNECTED → STREAMING
- Audio packets being sent

Host:
- Device discovered
- Pairing successful
- Connection established
- Audio synchronization started
- Audio packets being played
```

**Pass Criteria**:
- ✅ Connection establishes within 2 seconds
- ✅ Audio streaming starts automatically
- ✅ No errors logged
- ✅ Latency < 35ms

---

### Test 2: Connection Loss and Recovery

**Objective**: Verify reconnection logic works correctly

**Steps**:
1. Establish connection (see Test 1)
2. Kill accessory simulator (Ctrl+C)
3. Wait 2 seconds
4. Restart accessory simulator
5. Observe reconnection

**Expected Output**:
```
Host (after accessory killed):
- Keepalive timeout detected
- Connection lost message
- Audio synchronization stopped
- Restarting discovery

Host (after accessory restarted):
- Device rediscovered
- Reconnection successful
- Audio streaming resumed
```

**Pass Criteria**:
- ✅ Host detects connection loss within 5 seconds
- ✅ Reconnection succeeds within 5 seconds of accessory restart
- ✅ Audio resumes automatically

---

### Test 3: Discovery Without Connection

**Objective**: Verify discovery works independently

**Steps**:
1. Start accessory simulator
2. Start host daemon
3. Wait for discovery (5 seconds)
4. Kill both processes before connection completes

**Expected Output**:
```
Accessory:
- Listening for connections
- Received DISCOVER_REQUEST
- Sent DISCOVER_RESPONSE

Host:
- Starting discovery
- Device discovered: AudioSim-XXXX
- List of discovered devices shown
```

**Pass Criteria**:
- ✅ Device discovered within 5 seconds
- ✅ Device information displayed correctly
- ✅ No crashes or errors

---

### Test 4: Telemetry Logging

**Objective**: Verify telemetry data is logged correctly

**Steps**:
1. Establish connection
2. Let run for 30 seconds
3. Check telemetry log file: `/tmp/wireless_audio_telemetry.log` (Linux/macOS) or check console output

**Expected Output**:
```
Log file contains:
- Battery status updates (every 1 second)
- Diagnostics reports (every 5 seconds)
- Timestamps for all entries
- Battery level decreasing over time
```

**Pass Criteria**:
- ✅ Log file created
- ✅ Regular telemetry updates logged
- ✅ Battery level decreases realistically
- ✅ Diagnostics show accurate packet counts

---

### Test 5: Graceful Disconnect

**Objective**: Verify clean shutdown on both sides

**Steps**:
1. Establish connection
2. Press Ctrl+C on host daemon
3. Observe accessory behavior
4. Press Ctrl+C on accessory

**Expected Output**:
```
Host:
- Shutting down message
- Disconnect packet sent
- Clean exit

Accessory:
- Received DISCONNECT
- State transition: STREAMING → DISCONNECTING → IDLE
- Clean exit
```

**Pass Criteria**:
- ✅ No error messages during shutdown
- ✅ All threads terminated cleanly
- ✅ No resource leaks

---

### Test 6: Multiple Discovery Cycles

**Objective**: Verify discovery can be repeated

**Steps**:
1. Start accessory
2. Start host (auto-connects)
3. Disconnect (Ctrl+C on host)
4. Start host again
5. Verify connection re-establishes

**Expected Output**:
```
Second connection should work identically to first
```

**Pass Criteria**:
- ✅ Second connection works
- ✅ Same performance characteristics
- ✅ No state machine errors

---

## Automated Test Script

Create `test_runner.sh`:

```bash
#!/bin/bash

echo "=== Automated Test Suite ==="
echo ""

# Test 1: Build
echo "Test 1: Building project..."
./build.sh > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ Build successful"
else
    echo "❌ Build failed"
    exit 1
fi

# Test 2: Executables exist
echo "Test 2: Checking executables..."
if [ -f "build/accessory_simulator" ] && [ -f "build/host_daemon" ]; then
    echo "✅ Executables found"
else
    echo "❌ Executables missing"
    exit 1
fi

# Test 3: Basic startup (quick test)
echo "Test 3: Basic startup test..."
timeout 5 ./build/accessory_simulator > /tmp/accessory_test.log 2>&1 &
ACC_PID=$!
sleep 1

timeout 10 ./build/host_daemon > /tmp/host_test.log 2>&1 &
HOST_PID=$!
sleep 8

# Check logs for success indicators
if grep -q "STREAMING" /tmp/accessory_test.log && grep -q "Playing audio" /tmp/host_test.log; then
    echo "✅ Connection and streaming successful"
else
    echo "❌ Connection or streaming failed"
    echo "Check logs: /tmp/accessory_test.log and /tmp/host_test.log"
fi

# Cleanup
kill $ACC_PID $HOST_PID 2>/dev/null
sleep 1

echo ""
echo "=== Test Suite Complete ==="
```

---

## Performance Testing

### Latency Measurement

Monitor console output for latency statistics:

```
Expected ranges:
- P50 (median): 25-30ms
- P90: 30-35ms
- P99: 35-45ms
- P99.9: <50ms
```

### Packet Loss Simulation

To simulate packet loss (requires network tools):

```bash
# Linux: Add packet loss with tc (requires root)
sudo tc qdisc add dev lo root netem loss 5%

# Run simulation, observe buffer adaptation

# Remove packet loss
sudo tc qdisc del dev lo root
```

**Expected Behavior**:
- Host increases jitter buffer size
- Latency increases to compensate
- Audio continues playing (may have brief interruptions)
- Buffer size returns to normal after stable period

---

## Debug Output Analysis

### What to Look For

**Successful Operation**:
```
[Accessory] Audio packets sent: 100, 200, 300... (increasing)
[Host] Playing audio - Latency: 28ms (stable around target)
[Host] Diagnostics - Loss: 0.0% (low loss rate)
Battery: 100%, 99%, 98%... (gradual decrease)
```

**Connection Issues**:
```
[Host] ⚠️ Packet loss detected
[Host] Increasing jitter buffer size
[Accessory] Connection lost!
[Accessory] Reconnection attempt #1
```

**Errors to Investigate**:
```
Failed to bind socket (port already in use)
Checksum verification failed (packet corruption)
Keepalive timeout (connection lost)
```

---

## Common Issues and Solutions

### Issue: "Failed to bind socket to port 8888"
**Solution**: Port is already in use. Kill existing process or change port in code.

### Issue: "No devices found"
**Solution**: 
1. Ensure accessory started first
2. Check firewall settings
3. Verify localhost connectivity

### Issue: High latency (>50ms)
**Solution**: 
1. Check system load
2. Verify no other network-intensive processes
3. Review buffer size settings

### Issue: Frequent disconnections
**Solution**:
1. Check network stability
2. Increase keepalive timeout
3. Review system resources

---

## Validation Checklist

Before considering the project complete, verify:

- [ ] Project builds without errors
- [ ] Both executables run without crashes
- [ ] Connection establishes automatically
- [ ] Audio streaming starts and continues
- [ ] Telemetry is logged correctly
- [ ] Reconnection works after disconnect
- [ ] Graceful shutdown works
- [ ] Documentation is accurate
- [ ] Code is well-commented
- [ ] Architecture diagram matches implementation
- [ ] Timing diagram is realistic
- [ ] State machine is correctly implemented
