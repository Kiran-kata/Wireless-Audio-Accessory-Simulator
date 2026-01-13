#ifndef ACCESSORY_CONNECTION_FSM_H
#define ACCESSORY_CONNECTION_FSM_H

#include "protocol.h"
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

namespace accessory {

class Transport;

class ConnectionFSM {
public:
    using StateChangeCallback = std::function<void(protocol::ConnectionState, protocol::ConnectionState)>;
    
    explicit ConnectionFSM(Transport* transport);
    ~ConnectionFSM();
    
    // State management
    void start();
    void stop();
    protocol::ConnectionState get_state() const { return state_.load(); }
    
    // Event handlers
    void on_discover_request(const protocol::Packet& packet);
    void on_pair_request(const protocol::Packet& packet);
    void on_connect_request(const protocol::Packet& packet);
    void on_disconnect(const protocol::Packet& packet);
    void on_keepalive(const protocol::Packet& packet);
    
    // State transitions
    void enter_discovering();
    void enter_pairing();
    void enter_connected();
    void enter_streaming();
    void enter_disconnecting();
    void enter_idle();
    void enter_error();
    
    // Connection management
    void handle_connection_loss();
    void attempt_reconnection();
    
    // Callbacks
    void set_state_change_callback(StateChangeCallback callback) {
        state_change_callback_ = callback;
    }
    
    bool is_connected() const {
        auto state = state_.load();
        return state == protocol::ConnectionState::CONNECTED ||
               state == protocol::ConnectionState::STREAMING;
    }
    
    bool is_streaming() const {
        return state_.load() == protocol::ConnectionState::STREAMING;
    }
    
private:
    void transition_state(protocol::ConnectionState new_state);
    void send_discover_response();
    void send_pair_response(const protocol::Packet& request);
    void send_connect_response();
    void start_keepalive_thread();
    void stop_keepalive_thread();
    void keepalive_loop();
    
    Transport* transport_;
    std::atomic<protocol::ConnectionState> state_;
    StateChangeCallback state_change_callback_;
    
    // Connection tracking
    uint64_t last_keepalive_time_;
    uint32_t reconnect_attempts_;
    uint32_t reconnect_delay_ms_;
    
    // Device info
    uint8_t device_id_[8];
    char device_name_[32];
    
    // Threading
    std::thread keepalive_thread_;
    std::atomic<bool> keepalive_running_;
    std::mutex state_mutex_;
};

} // namespace accessory

#endif // ACCESSORY_CONNECTION_FSM_H
