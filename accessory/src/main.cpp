#include "accessory/connection_fsm.h"
#include "accessory/audio_streamer.h"
#include "accessory/telemetry.h"
#include "accessory/transport.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    std::cout << "\n[Accessory] Received signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

int main() {
    std::cout << "=== Wireless Audio Accessory Simulator ===" << std::endl;
    std::cout << "Simulating AirPods-like accessory behavior" << std::endl;
    std::cout << "==========================================\n" << std::endl;
    
    // Install signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create transport layer
    accessory::Transport transport;
    if (!transport.start(8888)) {
        std::cerr << "[Accessory] Failed to start transport" << std::endl;
        return 1;
    }
    
    // Create connection FSM
    accessory::ConnectionFSM connection_fsm(&transport);
    
    // Create audio streamer
    accessory::AudioStreamer audio_streamer(&transport);
    
    // Create telemetry
    accessory::Telemetry telemetry(&transport);
    
    // Set up packet routing
    transport.set_packet_callback([&](const protocol::Packet& packet) {
        switch (packet.header.type) {
            case protocol::PacketType::DISCOVER_REQUEST:
                connection_fsm.on_discover_request(packet);
                break;
                
            case protocol::PacketType::PAIR_REQUEST:
                connection_fsm.on_pair_request(packet);
                break;
                
            case protocol::PacketType::CONNECT_REQUEST:
                connection_fsm.on_connect_request(packet);
                break;
                
            case protocol::PacketType::DISCONNECT:
                connection_fsm.on_disconnect(packet);
                audio_streamer.stop_streaming();
                break;
                
            case protocol::PacketType::KEEPALIVE:
                connection_fsm.on_keepalive(packet);
                break;
                
            default:
                break;
        }
    });
    
    // Set up state change handler
    connection_fsm.set_state_change_callback([&](protocol::ConnectionState old_state,
                                                  protocol::ConnectionState new_state) {
        if (new_state == protocol::ConnectionState::CONNECTED) {
            // Start telemetry when connected
            telemetry.start();
            
            // Auto-transition to streaming after a brief delay
            std::thread([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (connection_fsm.get_state() == protocol::ConnectionState::CONNECTED) {
                    connection_fsm.enter_streaming();
                    audio_streamer.start_streaming();
                }
            }).detach();
        } else if (new_state == protocol::ConnectionState::IDLE ||
                   new_state == protocol::ConnectionState::DISCONNECTING) {
            // Stop streaming and telemetry when disconnected
            audio_streamer.stop_streaming();
            telemetry.stop();
        }
    });
    
    // Start connection FSM
    connection_fsm.start();
    
    std::cout << "[Accessory] Ready and listening for host connections..." << std::endl;
    std::cout << "[Accessory] Press Ctrl+C to exit\n" << std::endl;
    
    // Main loop
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Print periodic status
        if (connection_fsm.is_streaming()) {
            auto stats = audio_streamer.get_stats();
            if (stats.packets_sent > 0 && stats.packets_sent % 100 == 0) {
                std::cout << "[Accessory] Status - State: "
                          << protocol::connection_state_to_string(connection_fsm.get_state())
                          << ", Audio packets: " << stats.packets_sent
                          << ", Battery: " << static_cast<int>(telemetry.get_battery_level()) << "%"
                          << std::endl;
            }
        }
    }
    
    // Cleanup
    std::cout << "\n[Accessory] Cleaning up..." << std::endl;
    audio_streamer.stop_streaming();
    telemetry.stop();
    connection_fsm.stop();
    transport.stop();
    
    std::cout << "[Accessory] Shutdown complete" << std::endl;
    return 0;
}
