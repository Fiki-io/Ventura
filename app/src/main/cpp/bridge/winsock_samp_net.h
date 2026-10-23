#pragma once

#include "ventura_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ventura {

struct SampServerRule {
    std::string key;
    std::string value;
};

struct SampOnlinePlayer {
    uint8_t id = 0;
    std::string name;
    int32_t score = 0;
    uint32_t ping = 0;
};

struct SampServerQueryResult {
    bool reachable = false;
    std::string ip;
    uint16_t port = 7777;
    bool has_password = false;
    uint16_t active_players = 0;
    uint16_t max_players = 0;
    uint32_t rtt_ping_ms = 0;
    std::string hostname;
    std::string gamemode;
    std::string language;
    std::vector<SampServerRule> rules;
    std::vector<SampOnlinePlayer> players;
    std::string error_detail;
};

class WinsockSampNet {
public:
    static std::vector<uint8_t> BuildSampQueryPacket(
        const std::string& ipv4,
        uint16_t port,
        char opcode
    );

    static SampServerQueryResult QueryServerUdp(
        const std::string& host_or_ip,
        uint16_t port,
        int timeout_ms = 2000
    );
};

}  // namespace ventura
