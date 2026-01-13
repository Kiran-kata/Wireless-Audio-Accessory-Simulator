#include "host/audio_sync.h"
#include "host/transport.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

namespace host {

AudioSync::AudioSync(Transport* transport)
    : transport_(transport)
    , running_(false)
    , jitter_buffer_size_(protocol::DEFAULT_JITTER_BUFFER_PACKETS)
    , next_play_sequence_(0)
    , stream_start_time_(0)
    , last_packet_time_(0)
    , consecutive_losses_(0) {
    
    memset(&stats_, 0, sizeof(stats_));
}

AudioSync::~AudioSync() {
    stop();
}

void AudioSync::start() {
    if (running_.load()) {
        return;
    }
    
    std::cout << "[Host] Starting audio synchronization (buffer size: "
              << static_cast<int>(jitter_buffer_size_.load()) << " packets)" << std::endl;
    
    running_.store(true);
    next_play_sequence_ = 0;
    stream_start_time_ = protocol::get_timestamp_us();
    last_packet_time_ = stream_start_time_;
    consecutive_losses_ = 0;
    
    sync_thread_ = std::thread(&AudioSync::sync_loop, this);
}

void AudioSync::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "[Host] Stopping audio synchronization" << std::endl;
    running_.store(false);
    buffer_cv_.notify_all();
    
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    jitter_buffer_.clear();
}

void AudioSync::on_audio_packet(const protocol::Packet& packet) {
    uint64_t received_time = protocol::get_timestamp_us();
    
    if (packet.header.payload_length < sizeof(protocol::AudioPayload)) {
        return;
    }
    
    // Parse audio payload
    protocol::AudioPayload audio_payload;
    memcpy(&audio_payload, packet.payload, sizeof(audio_payload));
    
    // Extract audio data
    size_t audio_data_size = packet.header.payload_length - sizeof(protocol::AudioPayload);
    AudioPacketInfo packet_info;
    packet_info.sequence = packet.header.sequence;
    packet_info.stream_timestamp = audio_payload.stream_timestamp;
    packet_info.received_timestamp_us = received_time;
    packet_info.sample_count = audio_payload.sample_count;
    packet_info.audio_data.resize(audio_data_size);
    
    if (audio_data_size > 0) {
        memcpy(packet_info.audio_data.data(),
               packet.payload + sizeof(protocol::AudioPayload),
               audio_data_size);
    }
    
    // Add to jitter buffer
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        jitter_buffer_[packet_info.sequence] = packet_info;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.packets_received++;
        
        // Calculate latency
        uint32_t latency_us = static_cast<uint32_t>(received_time - stream_start_time_) -
                              packet_info.stream_timestamp;
        stats_.current_latency_ms = latency_us / 1000;
        
        if (stats_.current_latency_ms > stats_.max_latency_ms) {
            stats_.max_latency_ms = stats_.current_latency_ms;
        }
        
        // Update average latency
        if (stats_.packets_received > 0) {
            stats_.avg_latency_ms = (stats_.avg_latency_ms * (stats_.packets_received - 1) +
                                    stats_.current_latency_ms) / stats_.packets_received;
        }
    }
    
    last_packet_time_ = received_time;
    buffer_cv_.notify_one();
}

void AudioSync::sync_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        
        // Wait for buffer to fill to desired size before starting playback
        if (next_play_sequence_ == 0) {
            buffer_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return jitter_buffer_.size() >= jitter_buffer_size_.load() || !running_.load();
            });
            
            if (!running_.load()) {
                break;
            }
            
            // Start playback from oldest packet
            if (!jitter_buffer_.empty()) {
                next_play_sequence_ = jitter_buffer_.begin()->first;
                std::cout << "[Host] 🎵 Starting playback from sequence " << next_play_sequence_ << std::endl;
            }
        }
        
        // Check if next packet is available
        auto it = jitter_buffer_.find(next_play_sequence_);
        if (it != jitter_buffer_.end()) {
            // Packet available - play it
            AudioPacketInfo packet_info = it->second;
            jitter_buffer_.erase(it);
            lock.unlock();
            
            play_audio_packet(packet_info);
            next_play_sequence_++;
            consecutive_losses_ = 0;
            
        } else {
            // Packet missing - wait briefly or skip
            lock.unlock();
            
            uint64_t now = protocol::get_timestamp_us();
            uint64_t time_since_last = (now - last_packet_time_) / 1000;  // ms
            
            if (time_since_last > 100) {  // More than 100ms since last packet
                // Packet is late or lost
                std::cout << "[Host] ⚠️  Packet loss detected: sequence " << next_play_sequence_ << std::endl;
                handle_packet_loss(next_play_sequence_);
                next_play_sequence_++;
                consecutive_losses_++;
                
                // Adjust buffer size if many consecutive losses
                if (consecutive_losses_ >= 3) {
                    adjust_buffer_size();
                    consecutive_losses_ = 0;
                }
            } else {
                // Wait a bit longer for the packet
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
}

void AudioSync::play_audio_packet(const AudioPacketInfo& packet_info) {
    // Simulate audio playback (in real system: send to audio device)
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.packets_played++;
    
    if (stats_.packets_played % 100 == 0) {
        std::cout << "[Host] 🔊 Playing audio - Packets: " << stats_.packets_played
                  << ", Latency: " << stats_.current_latency_ms << "ms"
                  << ", Buffer: " << jitter_buffer_.size() << "/" << static_cast<int>(jitter_buffer_size_.load())
                  << std::endl;
    }
}

void AudioSync::handle_packet_loss(uint32_t lost_sequence) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.packets_dropped++;
    
    // Send retransmit request (optional - not implemented in this simulation)
    
    std::cout << "[Host] Packet " << lost_sequence << " lost (total: "
              << stats_.packets_dropped << ")" << std::endl;
}

void AudioSync::adjust_buffer_size() {
    uint8_t current_size = jitter_buffer_size_.load();
    
    if (current_size < protocol::MAX_JITTER_BUFFER_PACKETS) {
        current_size++;
        jitter_buffer_size_.store(current_size);
        
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.buffer_underruns++;
        
        std::cout << "[Host] 📊 Increasing jitter buffer size to " << static_cast<int>(current_size)
                  << " packets (latency now ~" << (current_size * protocol::AUDIO_PACKET_DURATION_MS)
                  << "ms)" << std::endl;
    }
}

void AudioSync::set_jitter_buffer_size(uint8_t packets) {
    if (packets < protocol::MIN_JITTER_BUFFER_PACKETS) {
        packets = protocol::MIN_JITTER_BUFFER_PACKETS;
    }
    if (packets > protocol::MAX_JITTER_BUFFER_PACKETS) {
        packets = protocol::MAX_JITTER_BUFFER_PACKETS;
    }
    
    jitter_buffer_size_.store(packets);
    std::cout << "[Host] Jitter buffer size set to " << static_cast<int>(packets) << " packets" << std::endl;
}

AudioSync::Stats AudioSync::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace host
