#include "host/device_manager.h"
#include "host/transport.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

namespace host {

DeviceManager::DeviceManager(Transport* transport)
    : transport_(transport)
    , discovering_(false)
    , connected_(false)
    , keepalive_running_(false) {
    
    memset(&connected_device_, 0, sizeof(connected_device_));
}

DeviceManager::~DeviceManager() {
    stop_discovery();
    disconnect_device();
}

void DeviceManager::start_discovery() {
    if (discovering_.load()) {
        return;
    }
    
    std::cout << "[Host] Starting device discovery..." << std::endl;
    discovering_.store(true);
    
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        discovered_devices_.clear();
    }
    
    discovery_thread_ = std::thread(&DeviceManager::discovery_loop, this);
}

void DeviceManager::stop_discovery() {
    if (!discovering_.load()) {
        return;
    }
    
    std::cout << "[Host] Stopping device discovery" << std::endl;
    discovering_.store(false);
    
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
}

void DeviceManager::discovery_loop() {
    while (discovering_.load()) {
        send_discover_request();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void DeviceManager::send_discover_request() {
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::DISCOVER_REQUEST);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    packet.set_payload(nullptr, 0);
    
    transport_->send_packet(packet);
}

void DeviceManager::on_discover_response(const protocol::Packet& packet) {
    if (packet.header.payload_length < sizeof(protocol::DiscoverPayload)) {
        return;
    }
    
    protocol::DiscoverPayload payload;
    memcpy(&payload, packet.payload, sizeof(payload));
    
    DeviceInfo device;
    device.name = std::string(payload.device_name);
    memcpy(device.device_id, payload.device_id, sizeof(device.device_id));
    device.capabilities = payload.capabilities;
    device.battery_level = payload.battery_level;
    device.paired = false;
    device.connected = false;
    device.last_seen_us = protocol::get_timestamp_us();
    
    // Check if device already discovered
    bool is_new = true;
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (auto& existing : discovered_devices_) {
            if (memcmp(existing.device_id, device.device_id, sizeof(device.device_id)) == 0) {
                existing.last_seen_us = device.last_seen_us;
                is_new = false;
                break;
            }
        }
        
        if (is_new) {
            discovered_devices_.push_back(device);
            std::cout << "[Host] 🔍 Discovered device: " << device.name
                      << " (Battery: " << static_cast<int>(device.battery_level) << "%)" << std::endl;
            
            if (device_discovered_callback_) {
                device_discovered_callback_(device);
            }
        }
    }
}

bool DeviceManager::pair_device(const DeviceInfo& device) {
    std::cout << "[Host] Pairing with device: " << device.name << std::endl;
    send_pair_request(device);
    return true;
}

void DeviceManager::send_pair_request(const DeviceInfo& device) {
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::PAIR_REQUEST);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    
    protocol::PairPayload payload;
    memcpy(payload.device_id, device.device_id, sizeof(device.device_id));
    
    // Generate host key pair (simulated)
    uint8_t private_key[32];
    for (int i = 0; i < 32; i++) {
        private_key[i] = static_cast<uint8_t>(rand() % 256);
        payload.public_key[i] = private_key[i] ^ 0x55;
    }
    
    for (int i = 0; i < 16; i++) {
        payload.nonce[i] = static_cast<uint8_t>(rand() % 256);
    }
    
    packet.set_payload(&payload, sizeof(payload));
    transport_->send_packet(packet);
}

void DeviceManager::on_pair_response(const protocol::Packet& packet) {
    std::cout << "[Host] ✅ Pairing successful" << std::endl;
    
    // Mark device as paired
    if (packet.header.payload_length >= sizeof(protocol::PairPayload)) {
        protocol::PairPayload payload;
        memcpy(&payload, packet.payload, sizeof(payload));
        
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (auto& device : discovered_devices_) {
            if (memcmp(device.device_id, payload.device_id, sizeof(device.device_id)) == 0) {
                device.paired = true;
                break;
            }
        }
    }
}

bool DeviceManager::connect_device(const DeviceInfo& device) {
    if (connected_.load()) {
        std::cout << "[Host] Already connected to a device" << std::endl;
        return false;
    }
    
    std::cout << "[Host] Connecting to device: " << device.name << std::endl;
    connected_device_ = device;
    send_connect_request();
    
    return true;
}

void DeviceManager::send_connect_request() {
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::CONNECT_REQUEST);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    packet.set_payload(nullptr, 0);
    
    transport_->send_packet(packet);
}

void DeviceManager::on_connect_response(const protocol::Packet& packet) {
    std::cout << "[Host] ✅ Connection established" << std::endl;
    connected_.store(true);
    connected_device_.connected = true;
    
    // Start keepalive thread
    keepalive_running_.store(true);
    keepalive_thread_ = std::thread(&DeviceManager::keepalive_loop, this);
    
    if (connection_state_callback_) {
        connection_state_callback_(true);
    }
}

bool DeviceManager::disconnect_device() {
    if (!connected_.load()) {
        return false;
    }
    
    std::cout << "[Host] Disconnecting from device" << std::endl;
    
    // Stop keepalive
    keepalive_running_.store(false);
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }
    
    // Send disconnect packet
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::DISCONNECT);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    packet.set_payload(nullptr, 0);
    transport_->send_packet(packet);
    
    connected_.store(false);
    connected_device_.connected = false;
    
    if (connection_state_callback_) {
        connection_state_callback_(false);
    }
    
    return true;
}

void DeviceManager::on_disconnect(const protocol::Packet& packet) {
    std::cout << "[Host] ❌ Device disconnected" << std::endl;
    
    keepalive_running_.store(false);
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }
    
    connected_.store(false);
    connected_device_.connected = false;
    
    if (connection_state_callback_) {
        connection_state_callback_(false);
    }
}

void DeviceManager::keepalive_loop() {
    while (keepalive_running_.load()) {
        send_keepalive();
        std::this_thread::sleep_for(std::chrono::milliseconds(protocol::KEEPALIVE_INTERVAL_MS));
    }
}

void DeviceManager::send_keepalive() {
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::KEEPALIVE);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    packet.set_payload(nullptr, 0);
    
    transport_->send_packet(packet);
}

std::vector<DeviceInfo> DeviceManager::get_discovered_devices() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    return discovered_devices_;
}

DeviceInfo DeviceManager::get_connected_device() const {
    return connected_device_;
}

} // namespace host
