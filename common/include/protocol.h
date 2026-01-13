#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <string>

namespace protocol {

// Protocol version
constexpr uint16_t PROTOCOL_VERSION = 0x0100;  // 1.0

// Packet types
enum class PacketType : uint8_t {
    // Connection management
    DISCOVER_REQUEST = 0x01,
    DISCOVER_RESPONSE = 0x02,
    PAIR_REQUEST = 0x10,
    PAIR_RESPONSE = 0x11,
    CONNECT_REQUEST = 0x12,
    CONNECT_RESPONSE = 0x13,
    DISCONNECT = 0x14,
    KEEPALIVE = 0x15,
    
    // Audio streaming
    AUDIO_DATA = 0x20,
    AUDIO_ACK = 0x21,
    AUDIO_RETRANSMIT = 0x22,
    
    // Telemetry
    BATTERY_STATUS = 0x30,
    DIAGNOSTICS = 0x31,
    
    // Security
    KEY_EXCHANGE = 0x40,
    ENCRYPTED_PACKET = 0x41
};

// Connection states
enum class ConnectionState : uint8_t {
    IDLE = 0,
    DISCOVERING = 1,
    PAIRING = 2,
    CONNECTED = 3,
    STREAMING = 4,
    DISCONNECTING = 5,
    ERROR = 0xFF
};

// Audio configuration
constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;  // Hz
constexpr uint16_t AUDIO_PACKET_DURATION_MS = 10;  // 10ms packets
constexpr uint16_t AUDIO_SAMPLES_PER_PACKET = (AUDIO_SAMPLE_RATE * AUDIO_PACKET_DURATION_MS) / 1000;  // 480 samples
constexpr uint16_t AUDIO_BYTES_PER_SAMPLE = 2;  // 16-bit PCM
constexpr uint16_t AUDIO_PACKET_SIZE = AUDIO_SAMPLES_PER_PACKET * AUDIO_BYTES_PER_SAMPLE;  // 960 bytes

// Latency constraints
constexpr uint16_t TARGET_LATENCY_MS = 30;
constexpr uint16_t MAX_LATENCY_MS = 50;

// Buffer configuration
constexpr uint8_t MIN_JITTER_BUFFER_PACKETS = 2;
constexpr uint8_t MAX_JITTER_BUFFER_PACKETS = 4;
constexpr uint8_t DEFAULT_JITTER_BUFFER_PACKETS = 3;

// Timing
constexpr uint16_t KEEPALIVE_INTERVAL_MS = 1000;
constexpr uint16_t CONNECTION_TIMEOUT_MS = 5000;
constexpr uint16_t RECONNECT_BASE_DELAY_MS = 100;
constexpr uint16_t RECONNECT_MAX_DELAY_MS = 5000;

// Packet header (common to all packets)
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t version;           // Protocol version
    PacketType type;            // Packet type
    uint8_t flags;              // Flags (encrypted, priority, etc.)
    uint32_t sequence;          // Sequence number
    uint32_t timestamp_us;      // Microsecond timestamp
    uint16_t payload_length;    // Payload length in bytes
    uint16_t checksum;          // Simple checksum
};
#pragma pack(pop)

constexpr size_t PACKET_HEADER_SIZE = sizeof(PacketHeader);
constexpr size_t MAX_PACKET_SIZE = 2048;
constexpr size_t MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

// Flag bits
constexpr uint8_t FLAG_ENCRYPTED = 0x01;
constexpr uint8_t FLAG_PRIORITY = 0x02;
constexpr uint8_t FLAG_ACK_REQUIRED = 0x04;
constexpr uint8_t FLAG_RETRANSMIT = 0x08;

// Discover response payload
#pragma pack(push, 1)
struct DiscoverPayload {
    char device_name[32];
    uint8_t device_id[8];       // Unique device identifier
    uint16_t capabilities;       // Capability flags
    uint8_t battery_level;       // 0-100%
};
#pragma pack(pop)

// Pair request/response
#pragma pack(push, 1)
struct PairPayload {
    uint8_t device_id[8];
    uint8_t public_key[32];     // Simulated ECDH public key
    uint8_t nonce[16];          // Random nonce
};
#pragma pack(pop)

// Audio data packet
#pragma pack(push, 1)
struct AudioPayload {
    uint32_t stream_timestamp;   // Stream time in microseconds
    uint16_t sample_count;       // Number of samples in this packet
    uint8_t encoding;            // 0=PCM16, 1=AAC, etc.
    uint8_t reserved;
    // Followed by audio data
};
#pragma pack(pop)

// Battery status
#pragma pack(push, 1)
struct BatteryPayload {
    uint8_t level;              // 0-100%
    uint8_t charging;           // 0=not charging, 1=charging
    uint16_t voltage_mv;        // Battery voltage in millivolts
    int16_t current_ma;         // Current draw in milliamps
    uint16_t temperature_c;     // Temperature in 0.1°C units
    uint32_t time_remaining_s;  // Estimated time remaining (seconds)
};
#pragma pack(pop)

// Diagnostics payload
#pragma pack(push, 1)
struct DiagnosticsPayload {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;
    uint32_t packets_retransmitted;
    uint32_t crc_errors;
    int8_t rssi_dbm;            // Signal strength
    uint8_t link_quality;       // 0-100%
    uint16_t avg_latency_us;
    uint16_t max_latency_us;
};
#pragma pack(pop)

// Complete packet structure
struct Packet {
    PacketHeader header;
    uint8_t payload[MAX_PAYLOAD_SIZE];
    
    Packet() {
        memset(this, 0, sizeof(Packet));
        header.version = PROTOCOL_VERSION;
    }
    
    size_t total_size() const {
        return PACKET_HEADER_SIZE + header.payload_length;
    }
    
    void set_type(PacketType t) {
        header.type = t;
    }
    
    void set_sequence(uint32_t seq) {
        header.sequence = seq;
    }
    
    void set_timestamp(uint32_t ts_us) {
        header.timestamp_us = ts_us;
    }
    
    void set_flags(uint8_t flags) {
        header.flags = flags;
    }
    
    void set_payload(const void* data, uint16_t length);
    uint16_t calculate_checksum() const;
    bool verify_checksum() const;
};

// Utility functions
const char* packet_type_to_string(PacketType type);
const char* connection_state_to_string(ConnectionState state);
uint64_t get_timestamp_us();
uint32_t get_timestamp_ms();

// Packet serialization helpers
bool serialize_packet(const Packet& packet, uint8_t* buffer, size_t buffer_size, size_t* bytes_written);
bool deserialize_packet(const uint8_t* buffer, size_t buffer_size, Packet* packet);

} // namespace protocol

#endif // PROTOCOL_H
