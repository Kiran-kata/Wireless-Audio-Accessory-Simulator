#include "host/device_manager.h"
#include "host/audio_sync.h"
#include "host/telemetry_processor.h"
#include "host/transport.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    std::cout << "\n[Host] Received signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

int main() {
    std::cout << "=== Wireless Audio Host Daemon ===" << std::endl;
    std::cout << "Host-side daemon for wireless audio accessories" << std::endl;
    std::cout << "===================================\n" << std::endl;
    
    // Install signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create transport layer
    host::Transport transport;
    if (!transport.start("127.0.0.1", 8888)) {
        std::cerr << "[Host] Failed to start transport" << std::endl;
        return 1;
    }
    
    // Create device manager
    host::DeviceManager device_manager(&transport);
    
    // Create audio sync
    host::AudioSync audio_sync(&transport);
    
    // Create telemetry processor
    host::TelemetryProcessor telemetry;
    telemetry.open_log("/tmp/wireless_audio_telemetry.log");
    
    // Set up packet routing
    transport.set_packet_callback([&](const protocol::Packet& packet) {
        switch (packet.header.type) {
            case protocol::PacketType::DISCOVER_RESPONSE:
                device_manager.on_discover_response(packet);
                break;
                
            case protocol::PacketType::PAIR_RESPONSE:
                device_manager.on_pair_response(packet);
                break;
                
            case protocol::PacketType::CONNECT_RESPONSE:
                device_manager.on_connect_response(packet);
                // Start audio sync when connected
                audio_sync.start();
                break;
                
            case protocol::PacketType::DISCONNECT:
                device_manager.on_disconnect(packet);
                audio_sync.stop();
                break;
                
            case protocol::PacketType::AUDIO_DATA:
                audio_sync.on_audio_packet(packet);
                break;
                
            case protocol::PacketType::BATTERY_STATUS:
                telemetry.process_battery_status(packet);
                break;
                
            case protocol::PacketType::DIAGNOSTICS:
                telemetry.process_diagnostics(packet);
                break;
                
            default:
                break;
        }
    });
    
    // Set up connection state callback
    device_manager.set_connection_state_callback([&](bool connected) {
        if (!connected) {
            audio_sync.stop();
            std::cout << "[Host] Connection lost, stopped audio sync" << std::endl;
        }
    });
    
    std::cout << "[Host] Starting device discovery..." << std::endl;
    device_manager.start_discovery();
    
    // Wait for device discovery
    std::cout << "[Host] Waiting for devices (5 seconds)...\n" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    auto devices = device_manager.get_discovered_devices();
    if (devices.empty()) {
        std::cout << "[Host] No devices found. Make sure accessory simulator is running." << std::endl;
        std::cout << "[Host] Continuing to search..." << std::endl;
    } else {
        std::cout << "\n[Host] Found " << devices.size() << " device(s):" << std::endl;
        for (size_t i = 0; i < devices.size(); i++) {
            std::cout << "  " << (i + 1) << ". " << devices[i].name
                      << " (Battery: " << static_cast<int>(devices[i].battery_level) << "%)" << std::endl;
        }
        
        // Auto-connect to first device
        if (!devices.empty()) {
            std::cout << "\n[Host] Auto-connecting to: " << devices[0].name << std::endl;
            device_manager.stop_discovery();
            
            // Pair first
            device_manager.pair_device(devices[0]);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Then connect
            device_manager.connect_device(devices[0]);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    std::cout << "\n[Host] Host daemon running. Press Ctrl+C to exit\n" << std::endl;
    
    // Main loop - print periodic statistics
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(10);
    
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto now = std::chrono::steady_clock::now();
        if (device_manager.is_connected() && (now - last_stats_time >= stats_interval)) {
            auto audio_stats = audio_sync.get_stats();
            auto battery = telemetry.get_latest_battery();
            
            std::cout << "\n[Host] === Status Report ===" << std::endl;
            std::cout << "  Device: " << device_manager.get_connected_device().name << std::endl;
            std::cout << "  Battery: " << static_cast<int>(battery.level) << "%" << std::endl;
            std::cout << "  Audio Packets: RX=" << audio_stats.packets_received
                      << ", Played=" << audio_stats.packets_played
                      << ", Lost=" << audio_stats.packets_dropped << std::endl;
            std::cout << "  Latency: Current=" << audio_stats.current_latency_ms
                      << "ms, Avg=" << audio_stats.avg_latency_ms
                      << "ms, Max=" << audio_stats.max_latency_ms << "ms" << std::endl;
            std::cout << "  Buffer Size: " << static_cast<int>(audio_sync.get_jitter_buffer_size())
                      << " packets (" << (audio_sync.get_jitter_buffer_size() * protocol::AUDIO_PACKET_DURATION_MS)
                      << "ms)" << std::endl;
            std::cout << "========================\n" << std::endl;
            
            last_stats_time = now;
        }
        
        // Recheck for devices if not connected
        if (!device_manager.is_connected() && !device_manager.is_discovering()) {
            std::cout << "[Host] Not connected. Restarting discovery..." << std::endl;
            device_manager.start_discovery();
        }
    }
    
    // Cleanup
    std::cout << "\n[Host] Cleaning up..." << std::endl;
    if (device_manager.is_connected()) {
        device_manager.disconnect_device();
    }
    device_manager.stop_discovery();
    audio_sync.stop();
    telemetry.close_log();
    transport.stop();
    
    std::cout << "[Host] Shutdown complete" << std::endl;
    return 0;
}
