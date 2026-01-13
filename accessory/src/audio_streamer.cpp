#include "accessory/audio_streamer.h"
#include "accessory/transport.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>

namespace accessory {

AudioStreamer::AudioStreamer(Transport* transport)
    : transport_(transport)
    , streaming_(false)
    , sequence_number_(0)
    , stream_start_time_(0) {
    
    memset(&stats_, 0, sizeof(stats_));
}

AudioStreamer::~AudioStreamer() {
    stop_streaming();
}

void AudioStreamer::start_streaming() {
    if (streaming_.load()) {
        return;
    }
    
    std::cout << "[Accessory] Starting audio streaming" << std::endl;
    streaming_.store(true);
    sequence_number_ = 0;
    stream_start_time_ = protocol::get_timestamp_us();
    
    streaming_thread_ = std::thread(&AudioStreamer::streaming_loop, this);
}

void AudioStreamer::stop_streaming() {
    if (!streaming_.load()) {
        return;
    }
    
    std::cout << "[Accessory] Stopping audio streaming" << std::endl;
    streaming_.store(false);
    
    if (streaming_thread_.joinable()) {
        streaming_thread_.join();
    }
}

void AudioStreamer::streaming_loop() {
    auto next_packet_time = std::chrono::steady_clock::now();
    const auto packet_interval = std::chrono::milliseconds(protocol::AUDIO_PACKET_DURATION_MS);
    
    while (streaming_.load()) {
        auto now = std::chrono::steady_clock::now();
        
        if (now >= next_packet_time) {
            send_audio_packet();
            next_packet_time += packet_interval;
            
            // Catch up if we're behind
            if (next_packet_time < now) {
                next_packet_time = now + packet_interval;
            }
        }
        
        // Sleep until next packet time
        std::this_thread::sleep_until(next_packet_time);
    }
}

void AudioStreamer::send_audio_packet() {
    protocol::Packet packet;
    packet.set_type(protocol::PacketType::AUDIO_DATA);
    packet.set_sequence(sequence_number_++);
    packet.set_timestamp(static_cast<uint32_t>(protocol::get_timestamp_us()));
    packet.set_flags(protocol::FLAG_ACK_REQUIRED);
    
    // Prepare audio payload
    protocol::AudioPayload audio_payload;
    audio_payload.stream_timestamp = static_cast<uint32_t>(
        protocol::get_timestamp_us() - stream_start_time_);
    audio_payload.sample_count = protocol::AUDIO_SAMPLES_PER_PACKET;
    audio_payload.encoding = 0;  // PCM16
    audio_payload.reserved = 0;
    
    // Generate simulated audio data (sine wave)
    uint8_t audio_buffer[protocol::AUDIO_PACKET_SIZE];
    generate_audio_packet(audio_buffer, protocol::AUDIO_PACKET_SIZE);
    
    // Combine payload header and audio data
    size_t total_payload_size = sizeof(audio_payload) + protocol::AUDIO_PACKET_SIZE;
    uint8_t* combined_payload = new uint8_t[total_payload_size];
    memcpy(combined_payload, &audio_payload, sizeof(audio_payload));
    memcpy(combined_payload + sizeof(audio_payload), audio_buffer, protocol::AUDIO_PACKET_SIZE);
    
    packet.set_payload(combined_payload, static_cast<uint16_t>(total_payload_size));
    delete[] combined_payload;
    
    // Send packet
    if (transport_->send_packet(packet)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.packets_sent++;
        
        if (stats_.packets_sent % 100 == 0) {
            std::cout << "[Accessory] Audio packets sent: " << stats_.packets_sent << std::endl;
        }
    }
}

void AudioStreamer::generate_audio_packet(uint8_t* buffer, size_t size) {
    // Generate a simple sine wave (440Hz "A" note)
    constexpr double frequency = 440.0;  // Hz
    constexpr double amplitude = 16000.0;  // Max value for 16-bit audio
    
    int16_t* samples = reinterpret_cast<int16_t*>(buffer);
    size_t num_samples = size / sizeof(int16_t);
    
    static double phase = 0.0;
    const double phase_increment = 2.0 * M_PI * frequency / protocol::AUDIO_SAMPLE_RATE;
    
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = static_cast<int16_t>(amplitude * std::sin(phase));
        phase += phase_increment;
        
        // Wrap phase to prevent numerical drift
        if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
    }
}

AudioStreamer::Stats AudioStreamer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace accessory
