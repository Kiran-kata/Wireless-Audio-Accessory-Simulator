#ifndef ACCESSORY_TELEMETRY_H
#define ACCESSORY_TELEMETRY_H

#include "protocol.h"
#include <atomic>
#include <thread>
#include <mutex>

namespace accessory {

class Transport;

class Telemetry {
public:
    explicit Telemetry(Transport* transport);
    ~Telemetry();
    
    // Telemetry control
    void start();
    void stop();
    
    // Battery simulation
    void set_battery_level(uint8_t level);
    uint8_t get_battery_level() const { return battery_level_.load(); }
    
    // Diagnostics
    void update_diagnostics(const protocol::DiagnosticsPayload& diag);
    
private:
    void telemetry_loop();
    void send_battery_status();
    void send_diagnostics();
    void simulate_battery_drain();
    
    Transport* transport_;
    std::atomic<bool> running_;
    std::thread telemetry_thread_;
    
    // Battery state
    std::atomic<uint8_t> battery_level_;
    std::atomic<bool> charging_;
    uint16_t voltage_mv_;
    int16_t current_ma_;
    uint16_t temperature_c_;
    
    // Diagnostics
    std::mutex diag_mutex_;
    protocol::DiagnosticsPayload diagnostics_;
};

} // namespace accessory

#endif // ACCESSORY_TELEMETRY_H
