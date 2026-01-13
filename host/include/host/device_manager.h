#ifndef HOST_DEVICE_MANAGER_H
#define HOST_DEVICE_MANAGER_H

#include "protocol.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace host {

class Transport;

struct DeviceInfo {
    std::string name;
    uint8_t device_id[8];
    uint16_t capabilities;
    uint8_t battery_level;
    bool paired;
    bool connected;
    uint64_t last_seen_us;
};

class DeviceManager {
public:
    using DeviceDiscoveredCallback = std::function<void(const DeviceInfo&)>;
    using ConnectionStateCallback = std::function<void(bool)>;
    
    explicit DeviceManager(Transport* transport);
    ~DeviceManager();
    
    // Discovery
    void start_discovery();
    void stop_discovery();
    bool is_discovering() const { return discovering_.load(); }
    
    // Connection management
    bool pair_device(const DeviceInfo& device);
    bool connect_device(const DeviceInfo& device);
    bool disconnect_device();
    bool is_connected() const { return connected_.load(); }
    
    // Device list
    std::vector<DeviceInfo> get_discovered_devices() const;
    DeviceInfo get_connected_device() const;
    
    // Packet handlers
    void on_discover_response(const protocol::Packet& packet);
    void on_pair_response(const protocol::Packet& packet);
    void on_connect_response(const protocol::Packet& packet);
    void on_disconnect(const protocol::Packet& packet);
    
    // Callbacks
    void set_device_discovered_callback(DeviceDiscoveredCallback callback) {
        device_discovered_callback_ = callback;
    }
    
    void set_connection_state_callback(ConnectionStateCallback callback) {
        connection_state_callback_ = callback;
    }
    
private:
    void discovery_loop();
    void send_discover_request();
    void send_pair_request(const DeviceInfo& device);
    void send_connect_request();
    void send_keepalive();
    void keepalive_loop();
    
    Transport* transport_;
    
    // Discovery state
    std::atomic<bool> discovering_;
    std::thread discovery_thread_;
    mutable std::mutex devices_mutex_;
    std::vector<DeviceInfo> discovered_devices_;
    
    // Connection state
    std::atomic<bool> connected_;
    DeviceInfo connected_device_;
    
    // Keepalive
    std::thread keepalive_thread_;
    std::atomic<bool> keepalive_running_;
    
    // Callbacks
    DeviceDiscoveredCallback device_discovered_callback_;
    ConnectionStateCallback connection_state_callback_;
};

} // namespace host

#endif // HOST_DEVICE_MANAGER_H
