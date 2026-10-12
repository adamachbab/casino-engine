#pragma once

#include <deque>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/api_client.hpp"

namespace app {

    enum class frame_result_t { none, close, minimize };

    enum class tab_t { lobby, roulette, crash, mines, blackjack, chicken, fairness, history, count };

    struct chip_bet_t {
        std::string type;
        int value = 0;
        int amount = 0;
    };

    class app_t {
    public:
        app_t( std::wstring host, int port );

        frame_result_t render( float width, float height, int title_bar_height, int button_width );

    private:
        void draw_title_bar( float width, int height, int button_width, frame_result_t& result );
        void draw_login( float width, float height );
        void draw_sidebar( float height );
        void draw_content( float width, float height );
        void draw_lobby( );
        void draw_roulette( );
        void draw_roulette_table( );
        void draw_chips( );
        void draw_crash( );
        void draw_mines( );
        void draw_blackjack( );
        void draw_chicken( );
        void draw_fairness( );
        void draw_history( );
        void draw_bet_controls( );
        void draw_result_line( const nlohmann::json& round );

        bool authenticate( const char* path );
        void refresh_state( );
        bool play( const wchar_t* path, nlohmann::json body );
        void apply_round( const nlohmann::json& round );
        void finish_animation( );
        void update_crash( float dt );
        void notify( const std::string& message, bool error );
        std::string verification_text( const nlohmann::json& round ) const;

        void place_bet( const char* type, int value, int amount );
        void remove_bet( const char* type, int value );
        int bet_amount( const char* type, int value ) const;
        long long roulette_total( ) const;

        net::api_client_t m_api;
        tab_t m_tab = tab_t::lobby;
        bool m_logged_in = false;

        char m_username[ 32 ] = { };
        char m_password[ 128 ] = { };
        std::string m_login_error;

        std::string m_name;
        long long m_balance = 0;
        long long m_pending_balance = 0;
        float m_shown_balance = 0.0f;

        std::string m_commitment;
        long long m_nonce = 0;
        char m_client_seed[ 64 ] = { };
        int m_bet = 100;

        nlohmann::json m_last_round;
        nlohmann::json m_active_mines;
        nlohmann::json m_active_blackjack;
        nlohmann::json m_active_chicken;
        nlohmann::json m_active_crash;
        float m_crash_clock = 0.0f;
        float m_crash_poll = 0.0f;
        std::deque<nlohmann::json> m_history;

        std::vector<chip_bet_t> m_roulette_bets;
        std::vector<chip_bet_t> m_last_roulette_bets;
        std::vector<chip_bet_t> m_bet_stack;
        int m_chip = 2;

        int m_crash_target = 1;
        int m_mines_count = 1;
        int m_chicken_risk = 0;
        float m_chicken_x = 0.0f;

        float m_anim_time = 0.0f;
        float m_anim_duration = 0.0f;
        bool m_animating = false;
        nlohmann::json m_anim_round;

        std::string m_toast;
        bool m_toast_error = false;
        float m_toast_time = 0.0f;
    };

}
