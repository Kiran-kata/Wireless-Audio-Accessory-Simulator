#ifndef HOST_AUDIO_SYNC_H
#define HOST_AUDIO_SYNC_H

#include "protocol.h"
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>

namespace host {

class Transport;

struct AudioPacketInfo {
    uint32_t sequence;
    uint32_t stream_timestamp;
    uint64_t received_timestamp_us;
    uint16_t sample_count;
    std::vector<uint8_t> audio_data;
};

class AudioSync {
public:
    explicit AudioSync(Transport* transport);
    ~AudioSync();
    
    // Synchronization control
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Packet handling
    void on_audio_packet(const protocol::Packet& packet);
    
    // Buffer configuration
    void set_jitter_buffer_size(uint8_t packets);
    uint8_t get_jitter_buffer_size() const { return jitter_buffer_size_.load(); }
    
    // Statistics
    struct Stats {
        uint64_t packets_received;
        uint64_t packets_played;
        uint64_t packets_dropped;
        uint64_t packets_late;
        uint64_t buffer_underruns;
        uint32_t current_latency_ms;
        uint32_t avg_latency_ms;
        uint32_t max_latency_ms;
    };
    
    Stats get_stats() const;
    
private:
    void sync_loop();
    void play_audio_packet(const AudioPacketInfo& packet_info);
    void handle_packet_loss(uint32_t lost_sequence);
    void adjust_buffer_size();
    
    Transport* transport_;
    std::atomic<bool> running_;
    std::thread sync_thread_;
    
    // Jitter buffer
    std::map<uint32_t, AudioPacketInfo> jitter_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    std::atomic<uint8_t> jitter_buffer_size_;
    uint32_t next_play_sequence_;
    
    // Timing
    uint64_t stream_start_time_;
    uint64_t last_packet_time_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_;
    uint32_t consecutive_losses_;
};

} // namespace host

#endif // HOST_AUDIO_SYNC_H
