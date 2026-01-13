#include "accessory/telemetry.h"
#include "accessory/transport.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

namespace accessory {

Telemetry::Telemetry(Transport* transport)
    : transport_(transport)
    , running_(false)
    , battery_level_(100)
    , charging_(false)
    , voltage_mv_(4200)  // Fully charged Li-ion
    , current_ma_(-150)  // Drawing 150mA
    , temperature_c_(250) {  // 25.0°C
    
    memset(&diagnostics_, 0, sizeof(diagnostics_));
}

Telemetry::~Telemetry() {
    stop();
}

void Telemetry::start() {
    if (running_.load()) {
        return;
    }
    
    std::cout << "[Accessory] Starting telemetry" << std::endl;
    running_.store(true);
    telemetry_thread_ = std::thread(&Telemetry::telemetry_loop, this);
}

void Telemetry::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "[Accessory] Stopping telemetry" << std::endl;
    running_.store(false);
    
    if (telemetry_thread_.joinable()) {
        telemetry_thread_.join();
    }
}

void Telemetry::telemetry_loop() {
    auto last_battery_report = std::chrono::steady_clock::now();
    auto last_diagnostics_report = std::chrono::steady_clock::now();
    auto last_drain_update = std::chrono::steady_clock::now();
    
    const auto battery_report_interval = std::chrono::seconds(1);
    const auto diagnostics_report_interval = std::chrono::seconds(5);
    const auto drain_update_interval = std::chrono::seconds(10);
    
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // Send battery status at 1Hz
        if (now - last_battery_report >= battery_report_interval) {
            send_battery_status();
            last_battery_report = now;
        }
        
        // Send diagnostics at 0.2Hz
        if (now - last_diagnostics_report >= diagnostics_report_interval) {
            send_diagnostics();
            last_diagnostics_report = now;
        }
        
        // Update battery drain simulation
        if (now - last_drain_update >= drain_update_interval) {
            simulate_battery_drain();
            last_drain_update = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Telemetry::send_battery_status() {
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::BATTERY_STATUS);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    
    protocol::BatteryPayload payload;
    payload.level = battery_level_.load();
    payload.charging = charging_.load() ? 1 : 0;
    payload.voltage_mv = voltage_mv_;
    payload.current_ma = current_ma_;
    payload.temperature_c = temperature_c_;
    
    // Estimate time remaining (simplified)
    if (!charging_.load() && current_ma_ < 0) {
        // Assuming 500mAh battery capacity
        const uint32_t battery_capacity_mah = 500;
        uint32_t remaining_mah = (battery_capacity_mah * payload.level) / 100;
        payload.time_remaining_s = (remaining_mah * 3600) / std::abs(current_ma_);
    } else {
        payload.time_remaining_s = 0;
    }
    
    packet.set_payload(&payload, sizeof(payload));
    transport_->send_packet(packet);
    
    if (payload.level <= 10) {
        std::cout << "[Accessory] ⚠️  LOW BATTERY: " << static_cast<int>(payload.level)
                  << "% (" << payload.time_remaining_s / 60 << " min remaining)" << std::endl;
    }
}

void Telemetry::send_diagnostics() {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::DIAGNOSTICS);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    
    // Update transport statistics
    diagnostics_.packets_sent = static_cast<uint32_t>(transport_->get_packets_sent());
    diagnostics_.packets_received = static_cast<uint32_t>(transport_->get_packets_received());
    
    // Simulate RSSI and link quality
    diagnostics_.rssi_dbm = -45;  // Good signal
    diagnostics_.link_quality = 95;  // Excellent
    
    packet.set_payload(&diagnostics_, sizeof(diagnostics_));
    transport_->send_packet(packet);
    
    std::cout << "[Accessory] Diagnostics - Sent: " << diagnostics_.packets_sent
              << ", Received: " << diagnostics_.packets_received
              << ", Lost: " << diagnostics_.packets_lost
              << ", RSSI: " << static_cast<int>(diagnostics_.rssi_dbm) << "dBm" << std::endl;
}

void Telemetry::simulate_battery_drain() {
    if (charging_.load()) {
        // Charging - increase level
        uint8_t level = battery_level_.load();
        if (level < 100) {
            battery_level_.store(level + 1);
            voltage_mv_ = 4000 + (level * 2);  // Simulate voltage rise
            current_ma_ = 500;  // Charging current
        } else {
            current_ma_ = 0;  // Fully charged
        }
    } else {
        // Discharging - decrease level
        uint8_t level = battery_level_.load();
        if (level > 0) {
            battery_level_.store(level - 1);
            voltage_mv_ = 3300 + (level * 9);  // Simulate voltage drop
            current_ma_ = -150;  // Normal discharge
            
            if (level <= 5) {
                std::cout << "[Accessory] 🔋 CRITICAL BATTERY: " << static_cast<int>(level)
                          << "% - Entering low power mode" << std::endl;
                current_ma_ = -50;  // Reduce power consumption
            }
        }
    }
}

void Telemetry::set_battery_level(uint8_t level) {
    if (level > 100) level = 100;
    battery_level_.store(level);
}

void Telemetry::update_diagnostics(const protocol::DiagnosticsPayload& diag) {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    diagnostics_ = diag;
}

} // namespace accessory
