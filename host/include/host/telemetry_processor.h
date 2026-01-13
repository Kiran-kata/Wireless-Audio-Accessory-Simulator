#ifndef HOST_TELEMETRY_PROCESSOR_H
#define HOST_TELEMETRY_PROCESSOR_H

#include "protocol.h"
#include <string>
#include <fstream>
#include <mutex>

namespace host {

class TelemetryProcessor {
public:
    TelemetryProcessor();
    ~TelemetryProcessor();
    
    // Open log file
    bool open_log(const std::string& filename);
    void close_log();
    
    // Process telemetry packets
    void process_battery_status(const protocol::Packet& packet);
    void process_diagnostics(const protocol::Packet& packet);
    
    // Get latest data
    protocol::BatteryPayload get_latest_battery() const;
    protocol::DiagnosticsPayload get_latest_diagnostics() const;
    
private:
    void log_message(const std::string& message);
    
    std::ofstream log_file_;
    mutable std::mutex log_mutex_;
    
    protocol::BatteryPayload latest_battery_;
    protocol::DiagnosticsPayload latest_diagnostics_;
    mutable std::mutex data_mutex_;
};

} // namespace host

#endif // HOST_TELEMETRY_PROCESSOR_H
