#pragma once

#include <cstdint>
#include <string>

namespace net {

    std::string sha256_hex( const std::string& value );
    std::uint32_t fair_int( const std::string& server_seed, const std::string& client_seed, std::int64_t nonce, const std::string& game, std::uint32_t max, std::uint32_t cursor = 0 );
    bool verify_commitment( const std::string& server_seed, const std::string& commitment );

}
