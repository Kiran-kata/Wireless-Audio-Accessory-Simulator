#ifndef ACCESSORY_AUDIO_STREAMER_H
#define ACCESSORY_AUDIO_STREAMER_H

#include "protocol.h"
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace accessory {

class Transport;

class AudioStreamer {
public:
    explicit AudioStreamer(Transport* transport);
    ~AudioStreamer();
    
    // Streaming control
    void start_streaming();
    void stop_streaming();
    bool is_streaming() const { return streaming_.load(); }
    
    // Audio generation (simulated)
    void generate_audio_packet(uint8_t* buffer, size_t size);
    
    // Statistics
    struct Stats {
        uint64_t packets_sent;
        uint64_t packets_acked;
        uint64_t retransmissions;
        uint32_t avg_latency_us;
        uint32_t max_latency_us;
    };
    
    Stats get_stats() const;
    
private:
    void streaming_loop();
    void send_audio_packet();
    
    Transport* transport_;
    std::atomic<bool> streaming_;
    std::thread streaming_thread_;
    
    // Sequence tracking
    uint32_t sequence_number_;
    uint64_t stream_start_time_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

} // namespace accessory

#endif // ACCESSORY_AUDIO_STREAMER_H
