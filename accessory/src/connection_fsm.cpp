#include "accessory/connection_fsm.h"
#include "accessory/transport.h"
#include "accessory/crypto.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <thread>

namespace accessory {

ConnectionFSM::ConnectionFSM(Transport* transport)
    : transport_(transport)
    , state_(protocol::ConnectionState::IDLE)
    , last_keepalive_time_(0)
    , reconnect_attempts_(0)
    , reconnect_delay_ms_(protocol::RECONNECT_BASE_DELAY_MS)
    , keepalive_running_(false) {
    
    // Generate device ID
    Crypto::generate_random(device_id_, sizeof(device_id_));
    snprintf(device_name_, sizeof(device_name_), "AudioSim-%02X%02X",
             device_id_[0], device_id_[1]);
}

ConnectionFSM::~ConnectionFSM() {
    stop();
}

void ConnectionFSM::start() {
    std::cout << "[Accessory] Starting connection FSM" << std::endl;
    transition_state(protocol::ConnectionState::IDLE);
    start_keepalive_thread();
}

void ConnectionFSM::stop() {
    std::cout << "[Accessory] Stopping connection FSM" << std::endl;
    stop_keepalive_thread();
    transition_state(protocol::ConnectionState::IDLE);
}

void ConnectionFSM::transition_state(protocol::ConnectionState new_state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto old_state = state_.load();
    
    if (old_state == new_state) {
        return;
    }
    
    std::cout << "[Accessory] State transition: "
              << protocol::connection_state_to_string(old_state)
              << " -> "
              << protocol::connection_state_to_string(new_state)
              << std::endl;
    
    state_.store(new_state);
    
    if (state_change_callback_) {
        state_change_callback_(old_state, new_state);
    }
}

void ConnectionFSM::on_discover_request(const protocol::Packet& packet) {
    std::cout << "[Accessory] Received DISCOVER_REQUEST" << std::endl;
    transition_state(protocol::ConnectionState::DISCOVERING);
    send_discover_response();
}

void ConnectionFSM::send_discover_response() {
    protocol::Packet response;
    response.set_type(protocol::PacketType::DISCOVER_RESPONSE);
    response.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    
    protocol::DiscoverPayload payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.device_name, device_name_, sizeof(payload.device_name) - 1);
    memcpy(payload.device_id, device_id_, sizeof(device_id_));
    payload.capabilities = 0x0001;  // Audio streaming capable
    payload.battery_level = 85;     // Simulated battery level
    
    response.set_payload(&payload, sizeof(payload));
    transport_->send_packet(response);
    
    std::cout << "[Accessory] Sent DISCOVER_RESPONSE: " << device_name_ << std::endl;
}

void ConnectionFSM::on_pair_request(const protocol::Packet& packet) {
    std::cout << "[Accessory] Received PAIR_REQUEST" << std::endl;
    transition_state(protocol::ConnectionState::PAIRING);
    send_pair_response(packet);
}

void ConnectionFSM::send_pair_response(const protocol::Packet& request) {
    protocol::Packet response;
    response.set_type(protocol::PacketType::PAIR_RESPONSE);
    response.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    
    protocol::PairPayload payload;
    memcpy(payload.device_id, device_id_, sizeof(device_id_));
    
    // Generate simulated ECDH key pair
    uint8_t private_key[32];
    Crypto::generate_keypair(payload.public_key, private_key);
    Crypto::generate_random(payload.nonce, sizeof(payload.nonce));
    
    response.set_payload(&payload, sizeof(payload));
    transport_->send_packet(response);
    
    std::cout << "[Accessory] Sent PAIR_RESPONSE with key exchange" << std::endl;
}

void ConnectionFSM::on_connect_request(const protocol::Packet& packet) {
    std::cout << "[Accessory] Received CONNECT_REQUEST" << std::endl;
    send_connect_response();
    transition_state(protocol::ConnectionState::CONNECTED);
    reconnect_attempts_ = 0;
    reconnect_delay_ms_ = protocol::RECONNECT_BASE_DELAY_MS;
}

void ConnectionFSM::send_connect_response() {
    protocol::Packet response;
    response.set_type(protocol::PacketType::CONNECT_RESPONSE);
    response.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    response.set_payload(nullptr, 0);
    
    transport_->send_packet(response);
    std::cout << "[Accessory] Sent CONNECT_RESPONSE" << std::endl;
}

void ConnectionFSM::on_disconnect(const protocol::Packet& packet) {
    std::cout << "[Accessory] Received DISCONNECT" << std::endl;
    transition_state(protocol::ConnectionState::DISCONNECTING);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    transition_state(protocol::ConnectionState::IDLE);
}

void ConnectionFSM::on_keepalive(const protocol::Packet& packet) {
    last_keepalive_time_ = protocol::get_timestamp_us();
    
    // Send keepalive response
    protocol::Packet response;
    response.set_type(protocol::PacketType::KEEPALIVE);
    response.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    response.set_payload(nullptr, 0);
    transport_->send_packet(response);
}

void ConnectionFSM::handle_connection_loss() {
    std::cout << "[Accessory] Connection lost! Entering fast-reconnect mode" << std::endl;
    transition_state(protocol::ConnectionState::ERROR);
    attempt_reconnection();
}

void ConnectionFSM::attempt_reconnection() {
    reconnect_attempts_++;
    
    std::cout << "[Accessory] Reconnection attempt #" << reconnect_attempts_
              << " (delay: " << reconnect_delay_ms_ << "ms)" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));
    
    // Exponential backoff
    reconnect_delay_ms_ = std::min(reconnect_delay_ms_ * 2,
                                   static_cast<uint32_t>(protocol::RECONNECT_MAX_DELAY_MS));
    
    transition_state(protocol::ConnectionState::IDLE);
}

void ConnectionFSM::enter_streaming() {
    std::cout << "[Accessory] Entering STREAMING state" << std::endl;
    transition_state(protocol::ConnectionState::STREAMING);
}

void ConnectionFSM::start_keepalive_thread() {
    keepalive_running_.store(true);
    keepalive_thread_ = std::thread(&ConnectionFSM::keepalive_loop, this);
}

void ConnectionFSM::stop_keepalive_thread() {
    keepalive_running_.store(false);
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }
}

void ConnectionFSM::keepalive_loop() {
    last_keepalive_time_ = protocol::get_timestamp_us();
    
    while (keepalive_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(protocol::KEEPALIVE_INTERVAL_MS));
        
        if (is_connected()) {
            uint64_t now = protocol::get_timestamp_us();
            uint64_t elapsed_ms = (now - last_keepalive_time_) / 1000;
            
            if (elapsed_ms > protocol::CONNECTION_TIMEOUT_MS) {
                handle_connection_loss();
            }
        }
    }
}

void ConnectionFSM::enter_discovering() { transition_state(protocol::ConnectionState::DISCOVERING); }
void ConnectionFSM::enter_pairing() { transition_state(protocol::ConnectionState::PAIRING); }
void ConnectionFSM::enter_connected() { transition_state(protocol::ConnectionState::CONNECTED); }
void ConnectionFSM::enter_disconnecting() { transition_state(protocol::ConnectionState::DISCONNECTING); }
void ConnectionFSM::enter_idle() { transition_state(protocol::ConnectionState::IDLE); }
void ConnectionFSM::enter_error() { transition_state(protocol::ConnectionState::ERROR); }

} // namespace accessory
