#include "accessory/transport.h"
#include <iostream>
#include <cstring>

namespace accessory {

Transport::Transport()
    : socket_fd_(-1)
    , host_connected_(false)
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

bool Transport::start(uint16_t port) {
    if (running_.load()) {
        return true;
    }
    
    if (!init_socket()) {
        return false;
    }
    
    // Bind to port
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Accessory] Failed to bind socket to port " << port << std::endl;
        cleanup_socket();
        return false;
    }
    
    std::cout << "[Accessory] Transport started on port " << port << std::endl;
    
    running_.store(true);
    receive_thread_ = std::thread(&Transport::receive_loop, this);
    send_thread_ = std::thread(&Transport::send_loop, this);
    
    return true;
}

void Transport::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "[Accessory] Stopping transport" << std::endl;
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
        std::cerr << "[Accessory] Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket to non-blocking
    u_long mode = 1;
    ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "[Accessory] Failed to create socket" << std::endl;
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
            // Save host address for sending responses
            if (!host_connected_) {
                host_addr_ = from_addr;
                host_connected_ = true;
                std::cout << "[Accessory] Host connected" << std::endl;
            }
            
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
            if (!host_connected_) {
                // Can't send without host address
                send_queue_.pop();
                continue;
            }
            
            protocol::Packet packet = send_queue_.front();
            send_queue_.pop();
            lock.unlock();
            
            // Serialize packet
            uint8_t buffer[protocol::MAX_PACKET_SIZE];
            size_t bytes_written;
            
            if (protocol::serialize_packet(packet, buffer, sizeof(buffer), &bytes_written)) {
                int sent = sendto(socket_fd_, reinterpret_cast<const char*>(buffer),
                                static_cast<int>(bytes_written), 0,
                                reinterpret_cast<const sockaddr*>(&host_addr_),
                                sizeof(host_addr_));
                
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

} // namespace accessory
