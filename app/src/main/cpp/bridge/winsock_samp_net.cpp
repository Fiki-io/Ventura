#include "winsock_samp_net.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <chrono>
#include <cstring>
#include <sstream>

namespace ventura {

std::vector<uint8_t> WinsockSampNet::BuildSampQueryPacket(
    const std::string& ipv4,
    uint16_t port,
    char opcode
) {
    std::vector<uint8_t> pkt;
    pkt.reserve(15);

    // 4-byte Magic "SAMP"
    pkt.push_back(0x53);
    pkt.push_back(0x41);
    pkt.push_back(0x4D);
    pkt.push_back(0x50);

    // 4-byte IPv4 octets
    in_addr addr{};
    if (inet_pton(AF_INET, ipv4.c_str(), &addr) == 1) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&addr.s_addr);
        pkt.push_back(bytes[0]);
        pkt.push_back(bytes[1]);
        pkt.push_back(bytes[2]);
        pkt.push_back(bytes[3]);
    } else {
        pkt.push_back(127);
        pkt.push_back(0);
        pkt.push_back(0);
        pkt.push_back(1);
    }

    // 2-byte Port in Little-Endian
    pkt.push_back(static_cast<uint8_t>(port & 0xFFu));
    pkt.push_back(static_cast<uint8_t>((port >> 8) & 0xFFu));

    // 1-byte Opcode ('i', 'r', 'c', 'd', 'p')
    pkt.push_back(static_cast<uint8_t>(opcode));

    if (opcode == 'p') {
        pkt.push_back(0x56);
        pkt.push_back(0x54);
        pkt.push_back(0x36);
        pkt.push_back(0x34);
    }

    return pkt;
}

SampServerQueryResult WinsockSampNet::QueryServerUdp(
    const std::string& host_or_ip,
    uint16_t port,
    int timeout_ms
) {
    SampServerQueryResult result;
    result.ip = host_or_ip;
    result.port = port;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;

    if (getaddrinfo(host_or_ip.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
        result.error_detail = "Resolusi DNS/IP gagal untuk host: " + host_or_ip;
        return result;
    }

    sockaddr_in target_addr{};
    std::memcpy(&target_addr, res->ai_addr, sizeof(sockaddr_in));
    target_addr.sin_port = htons(port);
    char resolved_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &target_addr.sin_addr, resolved_ip, sizeof(resolved_ip));
    result.ip = std::string(resolved_ip);
    freeaddrinfo(res);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        result.error_detail = "Gagal membuat socket UDP POSIX.";
        return result;
    }

    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<uint8_t> info_pkt = BuildSampQueryPacket(result.ip, port, 'i');
    auto start_time = std::chrono::steady_clock::now();

    ssize_t sent = sendto(
        sock,
        info_pkt.data(),
        info_pkt.size(),
        0,
        reinterpret_cast<const sockaddr*>(&target_addr),
        sizeof(target_addr)
    );

    if (sent != static_cast<ssize_t>(info_pkt.size())) {
        close(sock);
        result.error_detail = "Gagal mengirim paket UDP SAMP.";
        return result;
    }

    uint8_t recv_buf[2048];
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);
    ssize_t recvd = recvfrom(
        sock,
        recv_buf,
        sizeof(recv_buf),
        0,
        reinterpret_cast<sockaddr*>(&from_addr),
        &from_len
    );

    auto end_time = std::chrono::steady_clock::now();
    close(sock);

    if (recvd < 11 || std::memcmp(recv_buf, "SAMP", 4) != 0) {
        result.error_detail = "Server tidak merespons paket UDP SAMP dalam batas waktu.";
        return result;
    }

    result.rtt_ping_ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count()
    );

    size_t pos = 11;
    if (pos + 5 <= static_cast<size_t>(recvd)) {
        result.has_password = (recv_buf[pos] != 0);
        pos += 1;
        std::memcpy(&result.active_players, recv_buf + pos, sizeof(uint16_t));
        pos += 2;
        std::memcpy(&result.max_players, recv_buf + pos, sizeof(uint16_t));
        pos += 2;
    }

    auto read_len_prefixed_str = [&](std::string& out_str) {
        if (pos + 4 <= static_cast<size_t>(recvd)) {
            uint32_t len = 0;
            std::memcpy(&len, recv_buf + pos, sizeof(uint32_t));
            pos += 4;
            if (len <= 512 && pos + len <= static_cast<size_t>(recvd)) {
                out_str.assign(reinterpret_cast<const char*>(recv_buf + pos), len);
                pos += len;
            }
        }
    };

    read_len_prefixed_str(result.hostname);
    read_len_prefixed_str(result.gamemode);
    read_len_prefixed_str(result.language);
    result.reachable = true;
    return result;
}

}  // namespace ventura
