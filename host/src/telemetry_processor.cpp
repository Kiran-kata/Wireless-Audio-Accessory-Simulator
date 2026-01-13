#include "host/telemetry_processor.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <ctime>

namespace host {

TelemetryProcessor::TelemetryProcessor() {
    memset(&latest_battery_, 0, sizeof(latest_battery_));
    memset(&latest_diagnostics_, 0, sizeof(latest_diagnostics_));
}

TelemetryProcessor::~TelemetryProcessor() {
    close_log();
}

bool TelemetryProcessor::open_log(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
    
    log_file_.open(filename, std::ios::out | std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "[Host] Failed to open telemetry log: " << filename << std::endl;
        return false;
    }
    
    std::cout << "[Host] Telemetry log opened: " << filename << std::endl;
    log_message("=== Telemetry Log Started ===");
    
    return true;
}

void TelemetryProcessor::close_log() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    if (log_file_.is_open()) {
        log_message("=== Telemetry Log Ended ===");
        log_file_.close();
    }
}

void TelemetryProcessor::log_message(const std::string& message) {
    if (!log_file_.is_open()) {
        return;
    }
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    
    log_file_ << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
              << "." << std::setfill('0') << std::setw(3) << ms.count()
              << " | " << message << std::endl;
    log_file_.flush();
}

void TelemetryProcessor::process_battery_status(const protocol::Packet& packet) {
    if (packet.header.payload_length < sizeof(protocol::BatteryPayload)) {
        return;
    }
    
    protocol::BatteryPayload battery;
    memcpy(&battery, packet.payload, sizeof(battery));
    
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        latest_battery_ = battery;
    }
    
    // Log battery status
    std::stringstream ss;
    ss << "BATTERY: "
       << static_cast<int>(battery.level) << "% | "
       << battery.voltage_mv << "mV | "
       << battery.current_ma << "mA | "
       << (battery.temperature_c / 10.0) << "°C | ";
    
    if (battery.charging) {
        ss << "CHARGING";
    } else {
        uint32_t hours = battery.time_remaining_s / 3600;
        uint32_t minutes = (battery.time_remaining_s % 3600) / 60;
        ss << hours << "h " << minutes << "m remaining";
    }
    
    log_message(ss.str());
    
    // Console output for important events
    if (battery.level <= 10) {
        std::cout << "[Host] ⚠️  ACCESSORY LOW BATTERY: " << static_cast<int>(battery.level) << "%" << std::endl;
    }
}

void TelemetryProcessor::process_diagnostics(const protocol::Packet& packet) {
    if (packet.header.payload_length < sizeof(protocol::DiagnosticsPayload)) {
        return;
    }
    
    protocol::DiagnosticsPayload diag;
    memcpy(&diag, packet.payload, sizeof(diag));
    
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        latest_diagnostics_ = diag;
    }
    
    // Calculate packet loss rate
    uint32_t total_expected = diag.packets_received + diag.packets_lost;
    float loss_rate = 0.0f;
    if (total_expected > 0) {
        loss_rate = (100.0f * diag.packets_lost) / total_expected;
    }
    
    // Log diagnostics
    std::stringstream ss;
    ss << "DIAG: "
       << "TX=" << diag.packets_sent << " "
       << "RX=" << diag.packets_received << " "
       << "LOST=" << diag.packets_lost << " (" << std::fixed << std::setprecision(1) << loss_rate << "%) "
       << "RSSI=" << static_cast<int>(diag.rssi_dbm) << "dBm "
       << "LQ=" << static_cast<int>(diag.link_quality) << "% "
       << "LAT=" << diag.avg_latency_us / 1000.0f << "ms";
    
    log_message(ss.str());
    
    // Console output
    std::cout << "[Host] 📊 Diagnostics - "
              << "Packets: " << diag.packets_sent << "/" << diag.packets_received
              << ", Loss: " << loss_rate << "%"
              << ", RSSI: " << static_cast<int>(diag.rssi_dbm) << "dBm"
              << ", Link Quality: " << static_cast<int>(diag.link_quality) << "%"
              << std::endl;
}

protocol::BatteryPayload TelemetryProcessor::get_latest_battery() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return latest_battery_;
}

protocol::DiagnosticsPayload TelemetryProcessor::get_latest_diagnostics() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return latest_diagnostics_;
}

} // namespace host
