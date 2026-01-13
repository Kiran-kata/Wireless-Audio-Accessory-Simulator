#ifndef HOST_TRANSPORT_H
#define HOST_TRANSPORT_H

#include "protocol.h"
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace host {

class Transport {
public:
    using PacketCallback = std::function<void(const protocol::Packet&)>;
    
    Transport();
    ~Transport();
    
    // Connection management
    bool start(const char* accessory_host = "127.0.0.1", uint16_t accessory_port = 8888);
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Packet transmission
    bool send_packet(const protocol::Packet& packet);
    
    // Packet reception callback
    void set_packet_callback(PacketCallback callback) {
        packet_callback_ = callback;
    }
    
    // Statistics
    uint64_t get_packets_sent() const { return packets_sent_.load(); }
    uint64_t get_packets_received() const { return packets_received_.load(); }
    
private:
    void receive_loop();
    void send_loop();
    bool init_socket();
    void cleanup_socket();
    
    // Socket
#ifdef _WIN32
    SOCKET socket_fd_;
    WSADATA wsa_data_;
#else
    int socket_fd_;
#endif
    sockaddr_in accessory_addr_;
    
    // Threading
    std::thread receive_thread_;
    std::thread send_thread_;
    std::atomic<bool> running_;
    
    // Send queue
    std::queue<protocol::Packet> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    
    // Packet callback
    PacketCallback packet_callback_;
    
    // Statistics
    std::atomic<uint64_t> packets_sent_;
    std::atomic<uint64_t> packets_received_;
};

} // namespace host

#endif // HOST_TRANSPORT_H
