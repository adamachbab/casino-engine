#include "net/fairness.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <stdexcept>
#include <vector>

namespace net {

    static std::vector<unsigned char> hash( const std::vector<unsigned char>& key, const std::string& data, bool hmac ) {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE handle = nullptr;
        std::vector<unsigned char> output( 32 );

        if ( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0 ) != 0 ) {
            throw std::runtime_error( "BCryptOpenAlgorithmProvider failed" );
        }

        BCryptCreateHash( algorithm, &handle, nullptr, 0,
            hmac ? const_cast<unsigned char*>( key.data( ) ) : nullptr,
            hmac ? static_cast<ULONG>( key.size( ) ) : 0, 0 );
        BCryptHashData( handle, reinterpret_cast<unsigned char*>( const_cast<char*>( data.data( ) ) ), static_cast<ULONG>( data.size( ) ), 0 );
        BCryptFinishHash( handle, output.data( ), static_cast<ULONG>( output.size( ) ), 0 );
        BCryptDestroyHash( handle );
        BCryptCloseAlgorithmProvider( algorithm, 0 );

        return output;
    }

    static std::vector<unsigned char> from_hex( const std::string& value ) {
        std::vector<unsigned char> bytes;

        for ( size_t index = 0; index + 1 < value.size( ); index += 2 ) {
            bytes.push_back( static_cast<unsigned char>( std::stoi( value.substr( index, 2 ), nullptr, 16 ) ) );
        }

        return bytes;
    }

    std::string sha256_hex( const std::string& value ) {
        static constexpr char digits[ ] = "0123456789abcdef";
        std::string text;

        for ( unsigned char byte : hash( { }, value, false ) ) {
            text.push_back( digits[ byte >> 4 ] );
            text.push_back( digits[ byte & 15 ] );
        }

        return text;
    }

    std::uint32_t fair_int( const std::string& server_seed, const std::string& client_seed, std::int64_t nonce, const std::string& game, std::uint32_t max, std::uint32_t cursor ) {
        const auto key = from_hex( server_seed );
        const std::uint64_t limit = ( 0x100000000ull / max ) * max;

        for ( std::uint32_t attempt = 0; attempt < 1024; ++attempt ) {
            const std::string message = client_seed + ":" + std::to_string( nonce ) + ":" + game + ":" + std::to_string( cursor + attempt );
            const auto mac = hash( key, message, true );
            const std::uint64_t value = ( std::uint64_t( mac[ 0 ] ) << 24 ) | ( mac[ 1 ] << 16 ) | ( mac[ 2 ] << 8 ) | mac[ 3 ];

            if ( value < limit ) {
                return static_cast<std::uint32_t>( value % max );
            }
        }

        throw std::runtime_error( "Unable to draw an unbiased result" );
    }

    bool verify_commitment( const std::string& server_seed, const std::string& commitment ) {
        return !server_seed.empty( ) && sha256_hex( server_seed ) == commitment;
    }

}
