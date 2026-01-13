#include "host/transport.h"
#include <iostream>
#include <cstring>

namespace host {

Transport::Transport()
    : socket_fd_(-1)
    , running_(false)
    , packets_sent_(0)
    , packets_received_(0) {
#ifdef _WIN32
    WSAStartup(MAKEWORD(2, 2), &wsa_data_);
#endif
}

Transport::~Transport() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool Transport::start(const char* accessory_host, uint16_t accessory_port) {
    if (running_.load()) {
        return true;
    }
    
    if (!init_socket()) {
        return false;
    }
    
    // Set accessory address
    memset(&accessory_addr_, 0, sizeof(accessory_addr_));
    accessory_addr_.sin_family = AF_INET;
    accessory_addr_.sin_port = htons(accessory_port);
    
#ifdef _WIN32
    accessory_addr_.sin_addr.s_addr = inet_addr(accessory_host);
#else
    inet_pton(AF_INET, accessory_host, &accessory_addr_.sin_addr);
#endif
    
    std::cout << "[Host] Transport started (connecting to " << accessory_host
              << ":" << accessory_port << ")" << std::endl;
    
    running_.store(true);
    receive_thread_ = std::thread(&Transport::receive_loop, this);
    send_thread_ = std::thread(&Transport::send_loop, this);
    
    return true;
}

void Transport::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "[Host] Stopping transport" << std::endl;
    running_.store(false);
    
    send_cv_.notify_all();
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    
    cleanup_socket();
}

bool Transport::init_socket() {
#ifdef _WIN32
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ == INVALID_SOCKET) {
        std::cerr << "[Host] Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket to non-blocking
    u_long mode = 1;
    ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "[Host] Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
#endif
    
    return true;
}

void Transport::cleanup_socket() {
#ifdef _WIN32
    if (socket_fd_ != INVALID_SOCKET) {
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
    }
#else
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
#endif
}

void Transport::receive_loop() {
    uint8_t buffer[protocol::MAX_PACKET_SIZE];
    sockaddr_in from_addr;
    
#ifdef _WIN32
    int from_len = sizeof(from_addr);
#else
    socklen_t from_len = sizeof(from_addr);
#endif
    
    while (running_.load()) {
        memset(&from_addr, 0, sizeof(from_addr));
        
        int received = recvfrom(socket_fd_, reinterpret_cast<char*>(buffer),
                               sizeof(buffer), 0,
                               reinterpret_cast<sockaddr*>(&from_addr),
                               &from_len);
        
        if (received > 0) {
            // Deserialize packet
            protocol::Packet packet;
            if (protocol::deserialize_packet(buffer, received, &packet)) {
                packets_received_++;
                
                if (packet_callback_) {
                    packet_callback_(packet);
                }
            }
        } else {
            // No data available, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void Transport::send_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(send_mutex_);
        
        // Wait for packets to send
        send_cv_.wait(lock, [this] {
            return !send_queue_.empty() || !running_.load();
        });
        
        if (!running_.load()) {
            break;
        }
        
        while (!send_queue_.empty()) {
            protocol::Packet packet = send_queue_.front();
            send_queue_.pop();
            lock.unlock();
            
            // Serialize packet
            uint8_t buffer[protocol::MAX_PACKET_SIZE];
            size_t bytes_written;
            
            if (protocol::serialize_packet(packet, buffer, sizeof(buffer), &bytes_written)) {
                int sent = sendto(socket_fd_, reinterpret_cast<const char*>(buffer),
                                static_cast<int>(bytes_written), 0,
                                reinterpret_cast<const sockaddr*>(&accessory_addr_),
                                sizeof(accessory_addr_));
                
                if (sent > 0) {
                    packets_sent_++;
                }
            }
            
            lock.lock();
        }
    }
}

bool Transport::send_packet(const protocol::Packet& packet) {
    if (!running_.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(send_mutex_);
    send_queue_.push(packet);
    send_cv_.notify_one();
    
    return true;
}

} // namespace host
