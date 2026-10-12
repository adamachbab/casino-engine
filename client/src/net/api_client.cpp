#include "net/api_client.hpp"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <vector>

namespace net {

    std::string response_t::message( ) const {
        if ( !error.empty( ) ) {
            return error;
        }

        if ( body.is_object( ) && body.contains( "error" ) && body[ "error" ].is_string( ) ) {
            return body[ "error" ].get<std::string>( );
        }

        return "HTTP " + std::to_string( status );
    }

    api_client_t::api_client_t( std::wstring host, int port )
        : m_host( std::move( host ) ), m_port( port ) { }

    response_t api_client_t::get( const std::wstring& path ) const {
        return send( L"GET", path, { } );
    }

    response_t api_client_t::post( const std::wstring& path, const nlohmann::json& body ) const {
        return send( L"POST", path, body.dump( ) );
    }

    response_t api_client_t::send( const wchar_t* method, const std::wstring& path, const std::string& payload ) const {
        response_t result;

        HINTERNET session = WinHttpOpen( L"casino-engine/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if ( !session ) {
            result.error = "WinHttpOpen failed";
            return result;
        }

        WinHttpSetTimeouts( session, 5000, 5000, 10000, 10000 );

        HINTERNET connection = WinHttpConnect( session, m_host.c_str( ), static_cast<INTERNET_PORT>( m_port ), 0 );
        HINTERNET request = connection
            ? WinHttpOpenRequest( connection, method, path.c_str( ), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0 )
            : nullptr;

        if ( !request ) {
            result.error = "Unable to reach the server";
        } else {
            std::wstring headers = L"Content-Type: application/json\r\n";

            if ( !m_token.empty( ) ) {
                headers += L"Authorization: Bearer " + std::wstring( m_token.begin( ), m_token.end( ) ) + L"\r\n";
            }

            const BOOL sent = WinHttpSendRequest(
                request, headers.c_str( ), static_cast<DWORD>( -1 ),
                payload.empty( ) ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>( payload.data( ) ),
                static_cast<DWORD>( payload.size( ) ), static_cast<DWORD>( payload.size( ) ), 0 );

            if ( !sent || !WinHttpReceiveResponse( request, nullptr ) ) {
                result.error = "Unable to reach the server";
            } else {
                DWORD status = 0;
                DWORD size = sizeof( status );
                WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX );
                result.status = static_cast<int>( status );

                std::string text;
                DWORD available = 0;

                while ( WinHttpQueryDataAvailable( request, &available ) && available > 0 ) {
                    std::vector<char> chunk( available );
                    DWORD read = 0;

                    if ( !WinHttpReadData( request, chunk.data( ), available, &read ) || read == 0 ) {
                        break;
                    }

                    text.append( chunk.data( ), read );
                }

                result.body = nlohmann::json::parse( text, nullptr, false );

                if ( result.body.is_discarded( ) ) {
                    result.body = nlohmann::json::object( );
                    result.error = "Invalid server response";
                }
            }
        }

        if ( request ) {
            WinHttpCloseHandle( request );
        }

        if ( connection ) {
            WinHttpCloseHandle( connection );
        }

        WinHttpCloseHandle( session );
        return result;
    }

    static std::string random_string( size_t length ) {
        static constexpr char alphabet[ ] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

        std::vector<unsigned char> bytes( length );
        BCryptGenRandom( nullptr, bytes.data( ), static_cast<ULONG>( bytes.size( ) ), BCRYPT_USE_SYSTEM_PREFERRED_RNG );

        std::string value;
        value.reserve( length );

        for ( unsigned char byte : bytes ) {
            value.push_back( alphabet[ byte % 62 ] );
        }

        return value;
    }

    std::string new_request_id( ) {
        return random_string( 24 );
    }

    std::string new_client_seed( ) {
        return random_string( 16 );
    }

}
