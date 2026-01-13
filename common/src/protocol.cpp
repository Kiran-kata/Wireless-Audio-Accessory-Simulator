#include "protocol.h"
#include <chrono>
#include <cstring>
#include <cstddef>

namespace protocol {

void Packet::set_payload(const void* data, uint16_t length) {
    if (length > MAX_PAYLOAD_SIZE) {
        length = MAX_PAYLOAD_SIZE;
    }
    header.payload_length = length;
    if (data && length > 0) {
        memcpy(payload, data, length);
    }
    header.checksum = calculate_checksum();
}

uint16_t Packet::calculate_checksum() const {
    uint32_t sum = 0;
    
    // Checksum the header (excluding checksum field)
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    for (size_t i = 0; i < offsetof(PacketHeader, checksum); i++) {
        sum += header_bytes[i];
    }
    
    // Checksum the payload
    for (uint16_t i = 0; i < header.payload_length; i++) {
        sum += payload[i];
    }
    
    // Return 16-bit checksum with simple folding
    return static_cast<uint16_t>((sum & 0xFFFF) + (sum >> 16));
}

bool Packet::verify_checksum() const {
    return header.checksum == calculate_checksum();
}

const char* packet_type_to_string(PacketType type) {
    switch (type) {
        case PacketType::DISCOVER_REQUEST: return "DISCOVER_REQUEST";
        case PacketType::DISCOVER_RESPONSE: return "DISCOVER_RESPONSE";
        case PacketType::PAIR_REQUEST: return "PAIR_REQUEST";
        case PacketType::PAIR_RESPONSE: return "PAIR_RESPONSE";
        case PacketType::CONNECT_REQUEST: return "CONNECT_REQUEST";
        case PacketType::CONNECT_RESPONSE: return "CONNECT_RESPONSE";
        case PacketType::DISCONNECT: return "DISCONNECT";
        case PacketType::KEEPALIVE: return "KEEPALIVE";
        case PacketType::AUDIO_DATA: return "AUDIO_DATA";
        case PacketType::AUDIO_ACK: return "AUDIO_ACK";
        case PacketType::AUDIO_RETRANSMIT: return "AUDIO_RETRANSMIT";
        case PacketType::BATTERY_STATUS: return "BATTERY_STATUS";
        case PacketType::DIAGNOSTICS: return "DIAGNOSTICS";
        case PacketType::KEY_EXCHANGE: return "KEY_EXCHANGE";
        case PacketType::ENCRYPTED_PACKET: return "ENCRYPTED_PACKET";
        default: return "UNKNOWN";
    }
}

const char* connection_state_to_string(ConnectionState state) {
    switch (state) {
        case ConnectionState::IDLE: return "IDLE";
        case ConnectionState::DISCOVERING: return "DISCOVERING";
        case ConnectionState::PAIRING: return "PAIRING";
        case ConnectionState::CONNECTED: return "CONNECTED";
        case ConnectionState::STREAMING: return "STREAMING";
        case ConnectionState::DISCONNECTING: return "DISCONNECTING";
        case ConnectionState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

uint64_t get_timestamp_us() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

uint32_t get_timestamp_ms() {
    return static_cast<uint32_t>(get_timestamp_us() / 1000);
}

bool serialize_packet(const Packet& packet, uint8_t* buffer, size_t buffer_size, size_t* bytes_written) {
    size_t required_size = packet.total_size();
    if (buffer_size < required_size) {
        return false;
    }
    
    // Copy header
    memcpy(buffer, &packet.header, PACKET_HEADER_SIZE);
    
    // Copy payload
    if (packet.header.payload_length > 0) {
        memcpy(buffer + PACKET_HEADER_SIZE, packet.payload, packet.header.payload_length);
    }
    
    if (bytes_written) {
        *bytes_written = required_size;
    }
    
    return true;
}

bool deserialize_packet(const uint8_t* buffer, size_t buffer_size, Packet* packet) {
    if (buffer_size < PACKET_HEADER_SIZE) {
        return false;
    }
    
    // Copy header
    memcpy(&packet->header, buffer, PACKET_HEADER_SIZE);
    
    // Validate payload length
    if (packet->header.payload_length > MAX_PAYLOAD_SIZE) {
        return false;
    }
    
    size_t required_size = PACKET_HEADER_SIZE + packet->header.payload_length;
    if (buffer_size < required_size) {
        return false;
    }
    
    // Copy payload
    if (packet->header.payload_length > 0) {
        memcpy(packet->payload, buffer + PACKET_HEADER_SIZE, packet->header.payload_length);
    }
    
    // Verify checksum
    return packet->verify_checksum();
}

} // namespace protocol
