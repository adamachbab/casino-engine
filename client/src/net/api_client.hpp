#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace net {

    struct response_t {
        int status = 0;
        nlohmann::json body;
        std::string error;

        bool ok( ) const { return status >= 200 && status < 300 && body.value( "success", false ); }
        std::string message( ) const;
    };

    class api_client_t {
    public:
        api_client_t( std::wstring host, int port );

        response_t get( const std::wstring& path ) const;
        response_t post( const std::wstring& path, const nlohmann::json& body ) const;

        void set_token( std::string token ) { m_token = std::move( token ); }
        bool has_token( ) const { return !m_token.empty( ); }
        void clear_token( ) { m_token.clear( ); }

    private:
        response_t send( const wchar_t* method, const std::wstring& path, const std::string& payload ) const;

        std::wstring m_host;
        int m_port;
        std::string m_token;
    };

    std::string new_request_id( );
    std::string new_client_seed( );

}
