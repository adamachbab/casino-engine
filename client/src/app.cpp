#include "app.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include <imgui.h>

#include "net/fairness.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app {

    namespace {

        constexpr float pi = 3.14159265f;
        constexpr int wheel_order[ 37 ] = { 0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10, 5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26 };
        constexpr const char* tab_names[ ] = { "Lobby", "Roulette", "Crash", "Mines", "Blackjack", "Chicken", "Fairness", "History" };
        constexpr const char* tab_subtitles[ ] = {
            "Choose a table",
            "European single zero, inside and outside bets",
            "Set a target, the curve decides",
            "Reveal safe tiles, cash out anytime",
            "Single deck, double, split and insurance",
            "Cross the lanes, compounding multiplier",
            "Verify every round yourself",
            "Your last settled rounds"
        };
        constexpr int chip_values[ ] = { 10, 50, 100, 500, 1000, 5000 };
        constexpr const char* chip_labels[ ] = { "10", "50", "100", "500", "1K", "5K" };
        constexpr double crash_targets[ ] = { 1.5, 2.0, 5.0, 10.0 };
        constexpr int mines_counts[ ] = { 3, 5, 8 };
        constexpr const char* chicken_risks[ ] = { "safe", "spicy", "wild" };
        const ImVec4 felt_black = ImVec4( 0.125f, 0.137f, 0.165f, 1.0f );

        bool is_red( int number ) {
            static constexpr int red[ ] = { 1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36 };
            return std::find( std::begin( red ), std::end( red ), number ) != std::end( red );
        }

        ImVec4 number_color( int number ) {
            return number == 0 ? ui::palette::green : ( is_red( number ) ? ui::palette::red : felt_black );
        }

        ImVec4 status_color( const std::string& status ) {
            if ( status == "won" || status == "cashed_out" || status == "blackjack" ) {
                return ui::palette::green;
            }

            if ( status == "lost" || status == "bust" ) {
                return ui::palette::red;
            }

            return ui::palette::muted;
        }

        std::string text_of( const nlohmann::json& value, const char* key ) {
            return value.is_object( ) && value.contains( key ) && value[ key ].is_string( ) ? value[ key ].get<std::string>( ) : std::string( );
        }

        double number_of( const nlohmann::json& value, const char* key, double fallback ) {
            return value.is_object( ) && value.contains( key ) && value[ key ].is_number( ) ? value[ key ].get<double>( ) : fallback;
        }

        float crash_curve( float seconds ) {
            return std::exp( seconds * 0.12f );
        }

        float crash_multiplier( float seconds ) {
            return std::floor( crash_curve( seconds ) * 100.0f ) / 100.0f;
        }

        std::string signed_coins( long long value ) {
            return ( value > 0 ? "+" : "" ) + ui::format_coins( value );
        }

        void heading( const char* text, float size ) {
            ImGui::PushFont( ui::fonts::bold, size );
            ImGui::TextUnformatted( text );
            ImGui::PopFont( );
        }

    }

    app_t::app_t( std::wstring host, int port )
        : m_api( std::move( host ), port ) {
        strncpy_s( m_client_seed, net::new_client_seed( ).c_str( ), _TRUNCATE );
    }

    frame_result_t app_t::render( float width, float height, int title_bar_height, int button_width ) {
        frame_result_t result = frame_result_t::none;
        const float dt = ImGui::GetIO( ).DeltaTime;

        if ( m_animating ) {
            m_anim_time += dt;

            if ( m_anim_time >= m_anim_duration ) {
                m_animating = false;
                finish_animation( );
            }
        }

        m_shown_balance = ui::approach( m_shown_balance, static_cast<float>( m_balance ), 9.0f, dt );

        if ( std::fabs( m_shown_balance - static_cast<float>( m_balance ) ) < 0.5f ) {
            m_shown_balance = static_cast<float>( m_balance );
        }

        if ( m_toast_time > 0.0f ) {
            m_toast_time -= dt;
        }

        if ( m_logged_in ) {
            update_crash( dt );
        }

        ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
        ImGui::SetNextWindowSize( ImVec2( width, height ) );
        ImGui::Begin( "root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar );

        draw_title_bar( width, title_bar_height, button_width, result );

        const float body_height = height - static_cast<float>( title_bar_height );
        ImGui::SetCursorPos( ImVec2( 0, static_cast<float>( title_bar_height ) ) );

        if ( !m_logged_in ) {
            draw_login( width, body_height );
        } else {
            draw_sidebar( body_height );
            ImGui::SameLine( 0, 0 );
            draw_content( width - 220.0f, body_height );
        }

        if ( m_toast_time > 0.0f && !m_toast.empty( ) ) {
            ImDrawList* draw = ImGui::GetForegroundDrawList( );
            const ImVec2 size = ImGui::CalcTextSize( m_toast.c_str( ) );
            const float alpha = ui::clamp01( m_toast_time * 3.0f );
            const float slide = ( 1.0f - ui::ease_out_cubic( ui::clamp01( ( 3.0f - m_toast_time ) * 4.0f ) ) ) * 16.0f;
            const ImVec2 min( width - size.x - 52.0f, height - size.y - 46.0f + slide );
            const ImVec2 max( width - 22.0f, height - 22.0f + slide );
            const ImVec4 accent = m_toast_error ? ui::palette::red : ui::palette::green;

            draw->AddRectFilled( min, max, ui::to_u32( ui::palette::surface, alpha ), 8.0f );
            draw->AddRect( min, max, ui::to_u32( ui::palette::border, alpha ), 8.0f );
            draw->AddCircleFilled( ImVec2( min.x + 14.0f, ( min.y + max.y ) * 0.5f ), 3.0f, ui::to_u32( accent, alpha ) );
            draw->AddText( ImVec2( min.x + 26.0f, min.y + 12.0f ), ui::to_u32( ui::palette::text, alpha ), m_toast.c_str( ) );
        }

        ImGui::End( );
        return result;
    }

    void app_t::draw_title_bar( float width, int height, int button_width, frame_result_t& result ) {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const float bar = static_cast<float>( height );

        draw->AddRectFilled( ImVec2( 0, 0 ), ImVec2( width, bar ), ui::to_u32( ui::palette::sidebar ) );
        draw->AddLine( ImVec2( 0, bar - 1.0f ), ImVec2( width, bar - 1.0f ), ui::to_u32( ui::palette::border, 0.7f ) );
        draw->AddRectFilled( ImVec2( 16.0f, bar * 0.5f - 6.0f ), ImVec2( 28.0f, bar * 0.5f + 6.0f ), ui::to_u32( ui::palette::accent ), 3.0f );
        draw->AddText( ui::fonts::bold, 13.0f, ImVec2( 38.0f, bar * 0.5f - 8.0f ), ui::to_u32( ui::palette::text, 0.85f ), "casino-engine" );

        const ImVec2 button( static_cast<float>( button_width ), bar - 1.0f );
        const float minimize_x = width - button_width * 2.0f;
        const float close_x = width - static_cast<float>( button_width );

        ImGui::SetCursorPos( ImVec2( minimize_x, 0 ) );
        const bool minimize = ImGui::InvisibleButton( "##minimize", button );
        const bool minimize_hovered = ImGui::IsItemHovered( );
        ImGui::SetCursorPos( ImVec2( close_x, 0 ) );
        const bool close = ImGui::InvisibleButton( "##close", button );
        const bool close_hovered = ImGui::IsItemHovered( );

        if ( minimize_hovered ) {
            draw->AddRectFilled( ImVec2( minimize_x, 0 ), ImVec2( close_x, bar - 1.0f ), ui::to_u32( ui::palette::surface_hover ) );
        }

        if ( close_hovered ) {
            draw->AddRectFilled( ImVec2( close_x, 0 ), ImVec2( width, bar - 1.0f ), IM_COL32( 196, 43, 28, 255 ) );
        }

        const float cy = bar * 0.5f;
        const float mx = minimize_x + button_width * 0.5f;
        const float cx = close_x + button_width * 0.5f;
        const ImU32 glyph = ui::to_u32( ui::palette::text, 0.85f );
        const ImU32 close_glyph = close_hovered ? IM_COL32_WHITE : glyph;

        draw->AddLine( ImVec2( mx - 5.0f, cy ), ImVec2( mx + 5.0f, cy ), glyph, 1.0f );
        draw->AddLine( ImVec2( cx - 5.0f, cy - 5.0f ), ImVec2( cx + 5.0f, cy + 5.0f ), close_glyph, 1.0f );
        draw->AddLine( ImVec2( cx + 5.0f, cy - 5.0f ), ImVec2( cx - 5.0f, cy + 5.0f ), close_glyph, 1.0f );

        if ( close ) {
            result = frame_result_t::close;
        } else if ( minimize ) {
            result = frame_result_t::minimize;
        }
    }

    void app_t::draw_login( float width, float height ) {
        ImDrawList* back = ImGui::GetWindowDrawList( );
        const ImVec2 area_min( 0.0f, 40.0f );
        const ImVec2 area_max( width, 40.0f + height );

        back->PushClipRect( area_min, area_max, true );
        back->AddCircleFilled( ImVec2( width * 0.22f, 40.0f + height * 0.25f ), 260.0f, ui::to_u32( ui::palette::accent, 0.06f ), 96 );
        back->AddCircleFilled( ImVec2( width * 0.82f, 40.0f + height * 0.85f ), 300.0f, ui::to_u32( ui::palette::purple, 0.05f ), 96 );
        back->PopClipRect( );

        const ImVec2 size( 380.0f, 470.0f );
        const ImVec2 position( ( width - size.x ) * 0.5f, 40.0f + ( height - size.y ) * 0.5f );
        const ImVec2 window_pos = ImGui::GetWindowPos( );
        const ImVec2 shadow( window_pos.x + position.x, window_pos.y + position.y );

        back->AddRectFilled( ImVec2( shadow.x + 4.0f, shadow.y + 14.0f ), ImVec2( shadow.x + size.x - 4.0f, shadow.y + size.y + 10.0f ), IM_COL32( 0, 0, 0, 70 ), 18.0f );

        ImGui::SetCursorPos( position );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 36.0f, 34.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 16.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 9.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 14.0f, 11.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 10.0f, 8.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, ui::palette::surface );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, ui::palette::background );
        ImGui::BeginChild( "login", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 top = ImGui::GetWindowPos( );
            const ImVec2 logo( top.x + size.x * 0.5f, top.y + 62.0f );

            draw->AddRectFilled( ImVec2( logo.x - 26.0f, logo.y - 26.0f ), ImVec2( logo.x + 26.0f, logo.y + 26.0f ), ui::to_u32( ui::palette::accent ), 14.0f );
            draw->AddCircle( logo, 13.0f, IM_COL32_WHITE, 32, 2.5f );
            draw->AddCircleFilled( logo, 4.0f, IM_COL32_WHITE, 16 );

            const char* title = "Welcome to casino-engine";
            const char* subtitle = "Sign in to continue. Virtual coins only.";
            const ImVec2 title_size = ui::fonts::bold->CalcTextSizeA( 20.0f, FLT_MAX, 0.0f, title );
            const ImVec2 subtitle_size = ui::fonts::regular->CalcTextSizeA( 13.0f, FLT_MAX, 0.0f, subtitle );

            draw->AddText( ui::fonts::bold, 20.0f, ImVec2( top.x + ( size.x - title_size.x ) * 0.5f, top.y + 104.0f ), ui::to_u32( ui::palette::text ), title );
            draw->AddText( ui::fonts::regular, 13.0f, ImVec2( top.x + ( size.x - subtitle_size.x ) * 0.5f, top.y + 132.0f ), ui::to_u32( ui::palette::muted ), subtitle );
            ImGui::Dummy( ImVec2( 0, 130.0f ) );
        }

        ImGui::SetNextItemWidth( -1 );
        ImGui::InputTextWithHint( "##username", "Username", m_username, sizeof( m_username ) );
        ImGui::SetNextItemWidth( -1 );
        const bool submit = ImGui::InputTextWithHint( "##password", "Password", m_password, sizeof( m_password ), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue );

        ImGui::Dummy( ImVec2( 0, 6.0f ) );

        if ( ui::primary_button( "Sign in", ImVec2( -1, 42.0f ), ui::palette::accent ) || submit ) {
            authenticate( "login" );
        }

        if ( ImGui::Button( "Create account", ImVec2( -1, 42.0f ) ) ) {
            authenticate( "register" );
        }

        if ( !m_login_error.empty( ) ) {
            ImGui::Dummy( ImVec2( 0, 2.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ui::palette::red );
            ImGui::TextWrapped( "%s", m_login_error.c_str( ) );
            ImGui::PopStyleColor( );
        }

        ImGui::EndChild( );
        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 5 );
    }

    void app_t::draw_sidebar( float height ) {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12.0f, 16.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8.0f, 2.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, ui::palette::sidebar );
        ImGui::BeginChild( "sidebar", ImVec2( 220.0f, height ), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 window_pos = ImGui::GetWindowPos( );
        draw->AddLine( ImVec2( window_pos.x + 219.5f, window_pos.y ), ImVec2( window_pos.x + 219.5f, window_pos.y + height ), ui::to_u32( ui::palette::border, 0.7f ) );

        const auto section = [ ]( const char* label ) {
            ImGui::Dummy( ImVec2( 0, 12.0f ) );
            ImGui::SetCursorPosX( 24.0f );
            ImGui::PushFont( ui::fonts::bold, 11.0f );
            ImGui::PushStyleColor( ImGuiCol_Text, ui::palette::muted );
            ImGui::TextUnformatted( label );
            ImGui::PopStyleColor( );
            ImGui::PopFont( );
            ImGui::Dummy( ImVec2( 0, 2.0f ) );
        };

        const auto item = [ & ]( int index ) {
            if ( ui::tab_button( tab_names[ index ], static_cast<int>( m_tab ) == index, ImVec2( 196.0f, 32.0f ) ) ) {
                m_tab = static_cast<tab_t>( index );
            }
        };

        item( 0 );
        section( "GAMES" );

        for ( int index = 1; index <= 5; ++index ) {
            item( index );
        }

        section( "PLAYER" );
        item( 6 );
        item( 7 );

        ImGui::SetCursorPosY( height - 72.0f );
        const ImVec2 card = ImGui::GetCursorScreenPos( );
        draw->AddRectFilled( card, ImVec2( card.x + 196.0f, card.y + 54.0f ), ui::to_u32( ui::palette::surface, 0.8f ), 10.0f );

        const ImVec2 avatar( card.x + 27.0f, card.y + 27.0f );
        const std::string initial = m_name.empty( ) ? std::string( "?" ) : std::string( 1, static_cast<char>( std::toupper( static_cast<unsigned char>( m_name[ 0 ] ) ) ) );
        const ImVec2 initial_size = ui::fonts::bold->CalcTextSizeA( 14.0f, FLT_MAX, 0.0f, initial.c_str( ) );
        draw->AddCircleFilled( avatar, 15.0f, ui::to_u32( ui::palette::accent ), 32 );
        draw->AddText( ui::fonts::bold, 14.0f, ImVec2( avatar.x - initial_size.x * 0.5f, avatar.y - initial_size.y * 0.5f ), IM_COL32_WHITE, initial.c_str( ) );
        draw->AddText( ui::fonts::bold, 14.0f, ImVec2( card.x + 52.0f, card.y + 9.0f ), ui::to_u32( ui::palette::text ), m_name.c_str( ) );

        ImGui::SetCursorScreenPos( ImVec2( card.x + 48.0f, card.y + 29.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 4.0f, 0.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_Text, ui::palette::muted );
        if ( ImGui::Button( "Sign out", ImVec2( 0, 18.0f ) ) ) {
            m_api.post( L"/v1/auth/logout", nlohmann::json::object( ) );
            m_api.clear_token( );
            m_logged_in = false;
            m_animating = false;
            m_history.clear( );
            m_roulette_bets.clear( );
            m_active_crash = nlohmann::json( );
        }
        ImGui::PopStyleColor( 4 );
        ImGui::PopStyleVar( );

        ImGui::EndChild( );
        ImGui::PopStyleColor( );
        ImGui::PopStyleVar( 3 );
    }

    void app_t::draw_content( float width, float height ) {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 32.0f, 24.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, ui::palette::background );
        ImGui::BeginChild( "content", ImVec2( width, height ), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar );

        heading( tab_names[ static_cast<int>( m_tab ) ], 24.0f );
        ui::card_label( tab_subtitles[ static_cast<int>( m_tab ) ] );
        const float after_header = ImGui::GetCursorPosY( );

        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const std::string balance = ui::format_coins( std::llround( m_shown_balance ) );
            const ImVec2 balance_size = ui::fonts::bold->CalcTextSizeA( 16.0f, FLT_MAX, 0.0f, balance.c_str( ) );
            const float pill_width = balance_size.x + 48.0f;
            const ImVec2 pill_local( width - 32.0f - pill_width, 26.0f );
            const ImVec2 window_pos = ImGui::GetWindowPos( );
            const ImVec2 pill( window_pos.x + pill_local.x, window_pos.y + pill_local.y );
            const ImVec2 pill_max( pill.x + pill_width, pill.y + 34.0f );

            draw->AddRectFilled( pill, pill_max, ui::to_u32( ui::palette::surface ), 17.0f );
            draw->AddRect( pill, pill_max, ui::to_u32( ui::palette::border ), 17.0f );
            draw->AddCircleFilled( ImVec2( pill.x + 18.0f, pill.y + 17.0f ), 6.0f, ui::to_u32( ui::palette::gold ), 24 );
            draw->AddCircle( ImVec2( pill.x + 18.0f, pill.y + 17.0f ), 3.5f, IM_COL32( 0, 0, 0, 70 ), 24, 1.0f );
            draw->AddText( ui::fonts::bold, 16.0f, ImVec2( pill.x + 32.0f, pill.y + ( 34.0f - balance_size.y ) * 0.5f ), ui::to_u32( ui::palette::text ), balance.c_str( ) );

            ImGui::SetCursorPos( ImVec2( pill_local.x - 118.0f, pill_local.y ) );
            ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 17.0f );
            if ( ImGui::Button( "Daily bonus", ImVec2( 108.0f, 34.0f ) ) ) {
                const net::response_t response = m_api.post( L"/v1/casino/daily", nlohmann::json::object( ) );

                if ( response.ok( ) ) {
                    m_balance = response.body.value( "balance", m_balance );
                    m_pending_balance = m_balance;
                    notify( "Daily bonus claimed", false );
                } else {
                    notify( response.message( ), true );
                }
            }
            ImGui::PopStyleVar( );
        }

        ImGui::SetCursorPos( ImVec2( 32.0f, after_header ) );
        ImGui::Dummy( ImVec2( 0, 14.0f ) );

        switch ( m_tab ) {
        case tab_t::lobby: draw_lobby( ); break;
        case tab_t::roulette: draw_roulette( ); break;
        case tab_t::crash: draw_crash( ); break;
        case tab_t::mines: draw_mines( ); break;
        case tab_t::blackjack: draw_blackjack( ); break;
        case tab_t::chicken: draw_chicken( ); break;
        case tab_t::fairness: draw_fairness( ); break;
        case tab_t::history: draw_history( ); break;
        default: break;
        }

        ImGui::EndChild( );
        ImGui::PopStyleColor( );
        ImGui::PopStyleVar( );
    }

    void app_t::draw_lobby( ) {
        static constexpr const char* descriptions[ ] = {
            "Straight up 35:1, dozens, columns, red/black, even/odd, low/high",
            "Pick a target multiplier before launch",
            "5x5 board with 3, 5 or 8 mines",
            "Double down, split up to four hands, insurance",
            "Five lanes, three risk levels"
        };
        static constexpr const char* glyphs[ ] = { "R", "C", "M", "B", "H" };
        static const ImVec4 colors[ ] = { ui::palette::red, ui::palette::accent, ui::palette::green, ui::palette::purple, ui::palette::gold };

        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const float avail = ImGui::GetContentRegionAvail( ).x;
        const ImVec2 hero = ImGui::GetCursorScreenPos( );
        const ImVec2 hero_max( hero.x + avail, hero.y + 128.0f );

        draw->AddRectFilled( hero, hero_max, ui::to_u32( ui::palette::surface ), 14.0f );
        draw->PushClipRect( hero, hero_max, true );
        draw->AddCircleFilled( ImVec2( hero_max.x - 90.0f, hero.y + 30.0f ), 150.0f, ui::to_u32( ui::palette::accent, 0.10f ), 64 );
        draw->AddCircleFilled( ImVec2( hero_max.x - 240.0f, hero_max.y + 40.0f ), 110.0f, ui::to_u32( ui::palette::purple, 0.08f ), 64 );
        draw->PopClipRect( );
        draw->AddRect( hero, hero_max, ui::to_u32( ui::palette::border ), 14.0f );

        const std::string welcome = "Welcome back, " + m_name;
        const std::string rounds = std::to_string( m_history.size( ) ) + " recent rounds";
        draw->AddText( ui::fonts::bold, 24.0f, ImVec2( hero.x + 28.0f, hero.y + 28.0f ), ui::to_u32( ui::palette::text ), welcome.c_str( ) );
        draw->AddText( ui::fonts::regular, 15.0f, ImVec2( hero.x + 28.0f, hero.y + 64.0f ), ui::to_u32( ui::palette::muted ), "Pick a table. Every result is provably fair and verifiable." );
        draw->AddText( ui::fonts::regular, 13.0f, ImVec2( hero.x + 28.0f, hero.y + 90.0f ), ui::to_u32( ui::palette::muted, 0.7f ), rounds.c_str( ) );

        ImGui::Dummy( ImVec2( avail, 146.0f ) );

        const float gap = 16.0f;
        const float card_width = std::floor( ( avail - gap * 2.0f ) / 3.0f );
        const float card_height = 164.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos( );

        for ( int index = 0; index < 5; ++index ) {
            const ImVec2 min( origin.x + ( index % 3 ) * ( card_width + gap ), origin.y + ( index / 3 ) * ( card_height + gap ) );

            ImGui::SetCursorScreenPos( min );
            ImGui::PushID( index );
            const bool clicked = ImGui::InvisibleButton( "card", ImVec2( card_width, card_height ) );
            const bool hovered = ImGui::IsItemHovered( );
            float& blend = *ImGui::GetStateStorage( )->GetFloatRef( ImGui::GetID( "hover" ), 0.0f );
            ImGui::PopID( );

            blend = ui::approach( blend, hovered ? 1.0f : 0.0f, 12.0f, ImGui::GetIO( ).DeltaTime );

            if ( clicked ) {
                m_tab = static_cast<tab_t>( index + 1 );
            }

            const float lift = blend * 3.0f;
            const ImVec2 top( min.x, min.y - lift );
            const ImVec2 bottom( min.x + card_width, min.y + card_height - lift );

            draw->AddRectFilled( ImVec2( top.x + 2.0f, top.y + 8.0f ), ImVec2( bottom.x - 2.0f, bottom.y + 6.0f ), IM_COL32( 0, 0, 0, static_cast<int>( 70.0f * blend ) ), 14.0f );
            draw->AddRectFilled( top, bottom, ui::to_u32( ui::palette::surface ), 14.0f );
            draw->AddRectFilled( top, bottom, ui::to_u32( ui::palette::surface_hover, 0.5f * blend ), 14.0f );
            draw->AddRect( top, bottom, ui::to_u32( ui::palette::border ), 14.0f );

            const ImVec2 icon( top.x + 20.0f, top.y + 20.0f );
            const ImVec2 glyph_size = ui::fonts::bold->CalcTextSizeA( 18.0f, FLT_MAX, 0.0f, glyphs[ index ] );
            draw->AddRectFilled( icon, ImVec2( icon.x + 40.0f, icon.y + 40.0f ), ui::to_u32( colors[ index ], 0.18f ), 10.0f );
            draw->AddText( ui::fonts::bold, 18.0f, ImVec2( icon.x + ( 40.0f - glyph_size.x ) * 0.5f, icon.y + ( 40.0f - glyph_size.y ) * 0.5f ), ui::to_u32( colors[ index ] ), glyphs[ index ] );

            const ImVec2 play_size = ui::fonts::bold->CalcTextSizeA( 13.0f, FLT_MAX, 0.0f, "Play" );
            draw->AddText( ui::fonts::bold, 13.0f, ImVec2( bottom.x - 20.0f - play_size.x, top.y + 32.0f ), ui::to_u32( colors[ index ], 0.35f + 0.65f * blend ), "Play" );

            draw->AddText( ui::fonts::bold, 17.0f, ImVec2( top.x + 20.0f, top.y + 78.0f ), ui::to_u32( ui::palette::text ), tab_names[ index + 1 ] );
            draw->AddText( ui::fonts::regular, 13.0f, ImVec2( top.x + 20.0f, top.y + 104.0f ), ui::to_u32( ui::palette::muted ), descriptions[ index ], nullptr, card_width - 40.0f );
        }

        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( ImVec2( avail, card_height * 2.0f + gap ) );
    }

    void app_t::draw_bet_controls( ) {
        ui::card_label( "Bet amount" );
        ImGui::SetNextItemWidth( 170.0f );
        ImGui::InputInt( "##bet", &m_bet, 0, 0 );
        m_bet = std::clamp( m_bet, 1, 1000000 );

        ImGui::SameLine( 0, 6.0f );
        if ( ImGui::Button( "1/2", ImVec2( 42.0f, 0 ) ) ) { m_bet = std::max( 1, m_bet / 2 ); }
        ImGui::SameLine( 0, 6.0f );
        if ( ImGui::Button( "2x", ImVec2( 42.0f, 0 ) ) ) { m_bet = std::min( 1000000, m_bet * 2 ); }
        ImGui::SameLine( 0, 6.0f );
        if ( ImGui::Button( "Max", ImVec2( 46.0f, 0 ) ) ) { m_bet = static_cast<int>( std::clamp( m_balance, 1ll, 1000000ll ) ); }
    }

    void app_t::draw_result_line( const nlohmann::json& round ) {
        if ( !round.is_object( ) || m_animating || text_of( round, "status" ) == "playing" ) {
            return;
        }

        const long long change = round.value( "coin_change", 0ll );
        ImGui::PushStyleColor( ImGuiCol_Text, status_color( text_of( round, "status" ) ) );
        ImGui::Text( "%s   %s", text_of( round, "message" ).c_str( ), signed_coins( change ).c_str( ) );
        ImGui::PopStyleColor( );
    }

    void app_t::place_bet( const char* type, int value, int amount ) {
        if ( m_animating || roulette_total( ) + amount > 1000000 ) {
            return;
        }

        for ( auto& bet : m_roulette_bets ) {
            if ( bet.type == type && bet.value == value ) {
                bet.amount += amount;
                m_bet_stack.push_back( { type, value, amount } );
                return;
            }
        }

        m_roulette_bets.push_back( { type, value, amount } );
        m_bet_stack.push_back( { type, value, amount } );
    }

    void app_t::remove_bet( const char* type, int value ) {
        if ( m_animating ) {
            return;
        }

        std::erase_if( m_roulette_bets, [ & ]( const chip_bet_t& bet ) { return bet.type == type && bet.value == value; } );
        std::erase_if( m_bet_stack, [ & ]( const chip_bet_t& bet ) { return bet.type == type && bet.value == value; } );
    }

    int app_t::bet_amount( const char* type, int value ) const {
        for ( const auto& bet : m_roulette_bets ) {
            if ( bet.type == type && bet.value == value ) {
                return bet.amount;
            }
        }

        return 0;
    }

    long long app_t::roulette_total( ) const {
        long long total = 0;

        for ( const auto& bet : m_roulette_bets ) {
            total += bet.amount;
        }

        return total;
    }

    void app_t::draw_roulette( ) {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 origin = ImGui::GetCursorScreenPos( );
        const float radius = 122.0f;
        const ImVec2 center( origin.x + radius + 6.0f, origin.y + radius + 6.0f );
        const float segment = 2.0f * pi / 37.0f;

        const bool has_result = text_of( m_anim_round, "game" ) == "roulette";
        int result_index = 0;

        if ( has_result ) {
            const int number = m_anim_round.value( "number", 0 );
            result_index = static_cast<int>( std::find( std::begin( wheel_order ), std::end( wheel_order ), number ) - std::begin( wheel_order ) );
        }

        const bool spinning = m_animating && has_result;
        const float t = spinning ? ui::clamp01( m_anim_time / m_anim_duration ) : 1.0f;
        const float progress = ui::ease_out_cubic( t );
        const float final_rotation = -( result_index + 0.5f ) * segment;
        const float rotation = has_result ? final_rotation - ( 1.0f - progress ) * 5.0f * 2.0f * pi : 0.0f;

        draw->AddCircleFilled( center, radius + 6.0f, ui::to_u32( ui::palette::surface ), 96 );

        for ( int index = 0; index < 37; ++index ) {
            const int number = wheel_order[ index ];
            const float start = rotation + index * segment - pi * 0.5f;

            draw->PathLineTo( center );
            draw->PathArcTo( center, radius, start, start + segment, 6 );
            draw->PathFillConvex( ui::to_u32( number_color( number ) ) );
            draw->AddLine( center, ImVec2( center.x + std::cos( start ) * radius, center.y + std::sin( start ) * radius ), ui::to_u32( ui::palette::background, 0.5f ), 1.0f );

            const float middle = start + segment * 0.5f;
            const std::string label = std::to_string( number );
            const ImVec2 text_size = ui::fonts::bold->CalcTextSizeA( 12.0f, FLT_MAX, 0.0f, label.c_str( ) );
            draw->AddText( ui::fonts::bold, 12.0f, ImVec2( center.x + std::cos( middle ) * radius * 0.86f - text_size.x * 0.5f, center.y + std::sin( middle ) * radius * 0.86f - text_size.y * 0.5f ), IM_COL32( 255, 255, 255, 230 ), label.c_str( ) );
        }

        draw->AddCircleFilled( center, radius * 0.70f, ui::to_u32( ui::palette::background ), 96 );
        draw->AddCircle( center, radius * 0.70f, ui::to_u32( ui::palette::border ), 96, 1.0f );
        draw->AddCircle( center, radius + 6.0f, ui::to_u32( ui::palette::border ), 96, 1.0f );

        if ( has_result ) {
            const float ball_angle = -pi * 0.5f + ( 1.0f - progress ) * 8.0f * 2.0f * pi;
            const float ball_radius = radius * ( 0.74f + 0.22f * ( 1.0f - ui::ease_out_cubic( ui::clamp01( t * 1.4f ) ) ) );
            draw->AddCircleFilled( ImVec2( center.x + std::cos( ball_angle ) * ball_radius, center.y + std::sin( ball_angle ) * ball_radius ), 5.0f, IM_COL32( 245, 247, 250, 255 ), 20 );
        }

        draw->AddTriangleFilled( ImVec2( center.x - 6.0f, center.y - radius - 14.0f ), ImVec2( center.x + 6.0f, center.y - radius - 14.0f ), ImVec2( center.x, center.y - radius - 4.0f ), ui::to_u32( ui::palette::gold ) );

        if ( has_result && !m_animating ) {
            const int number = m_anim_round.value( "number", 0 );
            const std::string label = std::to_string( number );
            const ImVec2 size = ui::fonts::bold->CalcTextSizeA( 40.0f, FLT_MAX, 0.0f, label.c_str( ) );
            draw->AddCircleFilled( center, 34.0f, ui::to_u32( number_color( number ) ), 48 );
            draw->AddText( ui::fonts::bold, 40.0f, ImVec2( center.x - size.x * 0.5f, center.y - size.y * 0.5f ), IM_COL32_WHITE, label.c_str( ) );
        }

        ImGui::Dummy( ImVec2( radius * 2.0f + 12.0f, radius * 2.0f + 12.0f ) );
        ImGui::SameLine( 0, 36.0f );
        ImGui::BeginGroup( );

        ui::card_label( "Total bet" );
        heading( ui::format_coins( roulette_total( ) ).c_str( ), 24.0f );
        ImGui::Dummy( ImVec2( 0, 4.0f ) );
        draw_chips( );
        ImGui::Dummy( ImVec2( 0, 4.0f ) );

        ImGui::BeginDisabled( m_animating );
        if ( ImGui::Button( "Undo", ImVec2( 76.0f, 32.0f ) ) && !m_bet_stack.empty( ) ) {
            const chip_bet_t last = m_bet_stack.back( );
            m_bet_stack.pop_back( );

            for ( auto& bet : m_roulette_bets ) {
                if ( bet.type == last.type && bet.value == last.value ) {
                    bet.amount -= last.amount;
                }
            }

            std::erase_if( m_roulette_bets, [ ]( const chip_bet_t& bet ) { return bet.amount <= 0; } );
        }
        ImGui::SameLine( 0, 6.0f );
        if ( ImGui::Button( "Clear", ImVec2( 76.0f, 32.0f ) ) ) {
            m_roulette_bets.clear( );
            m_bet_stack.clear( );
        }
        ImGui::SameLine( 0, 6.0f );
        if ( ImGui::Button( "2x##bets", ImVec2( 60.0f, 32.0f ) ) && roulette_total( ) * 2 <= 1000000 ) {
            for ( auto& bet : m_roulette_bets ) {
                bet.amount *= 2;
            }
            m_bet_stack.clear( );
        }
        ImGui::SameLine( 0, 6.0f );
        ImGui::BeginDisabled( m_last_roulette_bets.empty( ) );
        if ( ImGui::Button( "Rebet", ImVec2( 76.0f, 32.0f ) ) ) {
            m_roulette_bets = m_last_roulette_bets;
            m_bet_stack.clear( );
        }
        ImGui::EndDisabled( );

        ImGui::Dummy( ImVec2( 0, 4.0f ) );
        ImGui::BeginDisabled( m_roulette_bets.empty( ) );
        if ( ui::primary_button( m_animating ? "Spinning..." : "Spin", ImVec2( 318.0f, 42.0f ), ui::palette::accent ) ) {
            nlohmann::json bets = nlohmann::json::array( );

            for ( const auto& bet : m_roulette_bets ) {
                nlohmann::json entry = { { "type", bet.type }, { "amount", bet.amount } };

                if ( bet.type == "straight" || bet.type == "dozen" || bet.type == "column" ) {
                    entry[ "value" ] = bet.value;
                }

                bets.push_back( entry );
            }

            if ( play( L"/v1/casino/roulette", { { "bets", bets }, { "client_seed", m_client_seed } } ) ) {
                m_last_roulette_bets = m_roulette_bets;
            }
        }
        ImGui::EndDisabled( );
        ImGui::EndDisabled( );

        if ( has_result ) {
            draw_result_line( m_anim_round );
        }

        ui::card_label( "Left click places a chip, right click removes the bet." );
        ImGui::EndGroup( );

        ImGui::Dummy( ImVec2( 0, 8.0f ) );
        draw_roulette_table( );
    }

    void app_t::draw_chips( ) {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        static const ImVec4 chip_colors[ ] = {
            ImVec4( 0.55f, 0.58f, 0.62f, 1.0f ), ui::palette::red, ui::palette::accent,
            ui::palette::green, ui::palette::purple, ui::palette::gold
        };

        for ( int index = 0; index < 6; ++index ) {
            if ( index ) {
                ImGui::SameLine( 0, 8.0f );
            }

            ImGui::PushID( index );
            const ImVec2 min = ImGui::GetCursorScreenPos( );
            if ( ImGui::InvisibleButton( "chip", ImVec2( 46.0f, 46.0f ) ) ) {
                m_chip = index;
            }
            const bool hovered = ImGui::IsItemHovered( );
            ImGui::PopID( );

            const bool selected = m_chip == index;
            const ImVec2 center( min.x + 23.0f, min.y + 23.0f - ( selected ? 3.0f : 0.0f ) );

            draw->AddCircleFilled( center, 20.0f, ui::to_u32( chip_colors[ index ], selected || hovered ? 1.0f : 0.7f ), 40 );
            draw->AddCircle( center, 15.0f, IM_COL32( 255, 255, 255, 110 ), 40, 1.5f );

            if ( selected ) {
                draw->AddCircle( center, 23.0f, ui::to_u32( ui::palette::text, 0.8f ), 40, 1.5f );
            }

            const ImVec2 size = ui::fonts::bold->CalcTextSizeA( 12.0f, FLT_MAX, 0.0f, chip_labels[ index ] );
            draw->AddText( ui::fonts::bold, 12.0f, ImVec2( center.x - size.x * 0.5f, center.y - size.y * 0.5f ), IM_COL32_WHITE, chip_labels[ index ] );
        }
    }

    void app_t::draw_roulette_table( ) {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 origin = ImGui::GetCursorScreenPos( );
        const float w = std::floor( ( ImGui::GetContentRegionAvail( ).x - 13.0f * 4.0f ) / 14.0f );
        const float h = 40.0f;
        const float g = 4.0f;
        const bool revealed = !m_animating && text_of( m_anim_round, "game" ) == "roulette";
        const int win = revealed ? m_anim_round.value( "number", -1 ) : -1;

        const auto cell = [ & ]( ImVec2 min, ImVec2 size, const std::string& label, const ImVec4& fill, const char* type, int value, bool highlight ) {
            ImGui::SetCursorScreenPos( min );
            ImGui::PushID( type );
            ImGui::PushID( value );
            ImGui::InvisibleButton( "cell", size );
            const bool hovered = ImGui::IsItemHovered( ) && !m_animating;

            if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) ) {
                place_bet( type, value, chip_values[ m_chip ] );
            }

            if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) ) {
                remove_bet( type, value );
            }

            ImGui::PopID( );
            ImGui::PopID( );

            const ImVec2 max( min.x + size.x, min.y + size.y );
            const int amount = bet_amount( type, value );

            draw->AddRectFilled( min, max, ui::to_u32( fill, hovered ? 1.0f : 0.82f ), 5.0f );

            if ( hovered ) {
                draw->AddRect( min, max, ui::to_u32( ui::palette::text, 0.35f ), 5.0f );
            }

            if ( highlight ) {
                draw->AddRect( min, max, ui::to_u32( ui::palette::gold ), 5.0f, 0, 2.0f );
            }

            const ImVec2 text_size = ui::fonts::bold->CalcTextSizeA( 14.0f, FLT_MAX, 0.0f, label.c_str( ) );
            draw->AddText( ui::fonts::bold, 14.0f, ImVec2( min.x + ( size.x - text_size.x ) * 0.5f, min.y + ( size.y - text_size.y ) * 0.5f ), ui::to_u32( ui::palette::text, 0.92f ), label.c_str( ) );

            if ( amount > 0 ) {
                const ImVec2 chip( max.x - 10.0f, min.y + 10.0f );
                draw->AddCircleFilled( chip, 8.0f, ui::to_u32( ui::palette::gold ), 24 );
                draw->AddCircle( chip, 5.5f, IM_COL32( 255, 255, 255, 140 ), 24, 1.0f );

                if ( hovered ) {
                    ImGui::SetTooltip( "%s on %s", ui::format_coins( amount ).c_str( ), label.c_str( ) );
                }
            }
        };

        const float grid_x = origin.x + w + g;

        cell( origin, ImVec2( w, h * 3.0f + g * 2.0f ), "0", ui::palette::green, "straight", 0, win == 0 );

        for ( int column = 0; column < 12; ++column ) {
            for ( int row = 0; row < 3; ++row ) {
                const int number = column * 3 + ( 3 - row );
                cell( ImVec2( grid_x + column * ( w + g ), origin.y + row * ( h + g ) ), ImVec2( w, h ), std::to_string( number ), number_color( number ), "straight", number, win == number );
            }
        }

        for ( int row = 0; row < 3; ++row ) {
            const int value = 3 - row;
            const bool hit = win > 0 && ( ( win - 1 ) % 3 ) + 1 == value;
            cell( ImVec2( grid_x + 12.0f * ( w + g ), origin.y + row * ( h + g ) ), ImVec2( w, h ), "2:1", ui::palette::surface_hover, "column", value, hit );
        }

        static constexpr const char* dozens[ ] = { "1st 12", "2nd 12", "3rd 12" };
        const float dozen_y = origin.y + 3.0f * ( h + g );

        for ( int dozen = 0; dozen < 3; ++dozen ) {
            const bool hit = win > 0 && ( win - 1 ) / 12 == dozen;
            cell( ImVec2( grid_x + dozen * 4.0f * ( w + g ), dozen_y ), ImVec2( w * 4.0f + g * 3.0f, h ), dozens[ dozen ], ui::palette::surface_hover, "dozen", dozen + 1, hit );
        }

        struct outside_t { const char* label; const char* type; ImVec4 color; bool hit; };
        const outside_t outside[ ] = {
            { "1 - 18", "low", ui::palette::surface_hover, win >= 1 && win <= 18 },
            { "Even", "even", ui::palette::surface_hover, win > 0 && win % 2 == 0 },
            { "Red", "red", ui::palette::red, win > 0 && is_red( win ) },
            { "Black", "black", felt_black, win > 0 && !is_red( win ) },
            { "Odd", "odd", ui::palette::surface_hover, win > 0 && win % 2 == 1 },
            { "19 - 36", "high", ui::palette::surface_hover, win >= 19 }
        };
        const float outside_y = dozen_y + h + g;

        for ( int index = 0; index < 6; ++index ) {
            cell( ImVec2( grid_x + index * 2.0f * ( w + g ), outside_y ), ImVec2( w * 2.0f + g, h ), outside[ index ].label, outside[ index ].color, outside[ index ].type, 0, outside[ index ].hit );
        }

        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( ImVec2( 14.0f * ( w + g ), 5.0f * ( h + g ) ) );
    }

    void app_t::draw_crash( ) {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 origin = ImGui::GetCursorScreenPos( );
        const ImVec2 size( ImGui::GetContentRegionAvail( ).x, 320.0f );
        const ImVec2 max( origin.x + size.x, origin.y + size.y );

        draw->AddRectFilled( origin, max, ui::to_u32( ui::palette::surface ), 8.0f );
        draw->AddRect( origin, max, ui::to_u32( ui::palette::border, 0.6f ), 8.0f );

        for ( int line = 1; line < 4; ++line ) {
            const float y = origin.y + line * size.y / 4.0f;
            draw->AddLine( ImVec2( origin.x + 1.0f, y ), ImVec2( max.x - 1.0f, y ), ui::to_u32( ui::palette::border, 0.35f ) );
        }

        const bool live = !m_active_crash.is_null( );
        const bool has_last = !live && text_of( m_last_round, "game" ) == "crash";
        const bool cashed = has_last && text_of( m_last_round, "status" ) == "cashed_out";
        const bool crashed = has_last && !cashed;
        float seconds = 0.0f;
        float current = 1.0f;

        if ( live ) {
            seconds = m_crash_clock;
            current = crash_multiplier( seconds );
        } else if ( has_last ) {
            current = static_cast<float>( cashed ? number_of( m_last_round, "cashed_at", 1.0 ) : number_of( m_last_round, "crash_point", 1.0 ) );
            seconds = std::log( std::max( 1.0f, current ) ) / 0.12f;
        }

        const float span = std::max( 8.0f, seconds * 1.15f );
        const float top = std::max( 2.0f, current * 1.15f );
        const float pad = 24.0f;

        const auto point = [ & ]( float s ) {
            return ImVec2( origin.x + pad + s / span * ( size.x - pad * 2.0f ), max.y - pad - ( crash_curve( s ) - 1.0f ) / ( top - 1.0f ) * ( size.y - pad * 2.0f ) );
        };

        const ImVec4 line_color = crashed ? ui::palette::red : ( cashed ? ui::palette::green : ui::palette::accent );

        if ( live || has_last ) {
            ImVec2 previous = point( 0.0f );

            for ( int step = 1; step <= 90; ++step ) {
                const ImVec2 next = point( seconds * step / 90.0f );
                draw->AddLine( previous, next, ui::to_u32( line_color ), 2.5f );
                previous = next;
            }

            draw->AddCircleFilled( previous, 5.0f, ui::to_u32( line_color ), 20 );
        }

        char label[ 32 ];
        snprintf( label, sizeof( label ), "%.2fx", current );
        draw->AddText( ui::fonts::bold, 48.0f, ImVec2( origin.x + 30.0f, origin.y + 22.0f ), ui::to_u32( live ? ui::palette::text : line_color ), label );

        const char* caption = live ? "Flying, cash out before it crashes" : ( crashed ? "Crashed" : ( cashed ? "Cashed out" : "Waiting for launch" ) );
        draw->AddText( ImVec2( origin.x + 34.0f, origin.y + 80.0f ), ui::to_u32( ui::palette::muted ), caption );

        ImGui::Dummy( size );
        ImGui::Dummy( ImVec2( 0, 4.0f ) );

        if ( live ) {
            const long long value = static_cast<long long>( std::floor( m_active_crash.value( "bet", 0ll ) * static_cast<double>( current ) ) );
            const std::string text = "Cash out  " + ui::format_coins( value ) + "###cashout";

            if ( ui::primary_button( text.c_str( ), ImVec2( 300.0f, 46.0f ), ui::palette::green ) ) {
                play( L"/v1/casino/crash/cashout", nlohmann::json::object( ) );
            }
        } else {
            draw_bet_controls( );

            ImGui::BeginDisabled( m_animating );
            if ( ui::primary_button( "Launch", ImVec2( 300.0f, 42.0f ), ui::palette::accent ) ) {
                play( L"/v1/casino/crash/start", { { "bet", m_bet }, { "client_seed", m_client_seed } } );
            }
            ImGui::EndDisabled( );

            if ( has_last ) {
                draw_result_line( m_last_round );
            }
        }
    }

    void app_t::update_crash( float dt ) {
        if ( m_active_crash.is_null( ) ) {
            return;
        }

        m_crash_clock += dt;
        m_crash_poll -= dt;

        if ( m_crash_poll > 0.0f || m_animating ) {
            return;
        }

        m_crash_poll = 0.2f;

        const net::response_t response = m_api.post( L"/v1/casino/crash/status", nlohmann::json::object( ) );

        if ( !response.ok( ) ) {
            return;
        }

        const nlohmann::json round = response.body.value( "game", nlohmann::json( ) );

        if ( !round.is_object( ) ) {
            m_active_crash = nlohmann::json( );
            return;
        }

        if ( text_of( round, "status" ) == "playing" ) {
            const float server = static_cast<float>( number_of( round, "elapsed_ms", m_crash_clock * 1000.0 ) / 1000.0 );

            if ( std::fabs( server - m_crash_clock ) > 0.3f ) {
                m_crash_clock = server;
            }

            return;
        }

        const long long balance = response.body.value( "balance", m_balance );
        m_pending_balance = balance;
        m_balance = balance - round.value( "payout", 0ll );
        apply_round( round );
    }

    void app_t::draw_mines( ) {
        const bool active = !m_active_mines.is_null( );
        const nlohmann::json round = active ? m_active_mines : ( text_of( m_last_round, "game" ) == "mines" ? m_last_round : m_active_mines );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const float tile = 70.0f;
        const float gap = 8.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos( );

        for ( int index = 0; index < 25; ++index ) {
            bool revealed = false;
            bool mine = false;

            if ( round.is_object( ) ) {
                for ( const auto& value : round.value( "revealed", nlohmann::json::array( ) ) ) {
                    revealed |= value.get<int>( ) == index;
                }

                if ( round.contains( "mine_positions" ) && round[ "mine_positions" ].is_array( ) ) {
                    for ( const auto& value : round[ "mine_positions" ] ) {
                        mine |= value.get<int>( ) == index;
                    }
                }
            }

            const ImVec2 min( origin.x + ( index % 5 ) * ( tile + gap ), origin.y + ( index / 5 ) * ( tile + gap ) );
            const ImVec2 max( min.x + tile, min.y + tile );

            ImGui::SetCursorScreenPos( min );
            ImGui::PushID( index );
            const bool clickable = active && !revealed && !m_animating;
            const bool pressed = ImGui::InvisibleButton( "tile", ImVec2( tile, tile ) ) && clickable;
            const bool hovered = ImGui::IsItemHovered( ) && clickable;
            ImGui::PopID( );

            ImVec4 fill = hovered ? ui::palette::surface_hover : ui::palette::surface;
            if ( revealed && !mine ) fill = ImVec4( 0.10f, 0.25f, 0.15f, 1.0f );
            if ( mine ) fill = revealed ? ImVec4( 0.35f, 0.10f, 0.10f, 1.0f ) : ImVec4( 0.18f, 0.10f, 0.11f, 1.0f );

            draw->AddRectFilled( min, max, ui::to_u32( fill ), 8.0f );
            draw->AddRect( min, max, ui::to_u32( hovered ? ui::palette::accent : ui::palette::border, hovered ? 0.8f : 0.5f ), 8.0f );

            const ImVec2 center( min.x + tile * 0.5f, min.y + tile * 0.5f );

            if ( revealed && !mine ) {
                draw->AddCircleFilled( center, 10.0f, ui::to_u32( ui::palette::green ), 6 );
            } else if ( mine ) {
                draw->AddCircleFilled( center, 11.0f, ui::to_u32( ui::palette::red, revealed ? 1.0f : 0.55f ), 24 );
            }

            if ( pressed ) {
                play( L"/v1/casino/mines/reveal", { { "tile", index } } );
            }
        }

        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( ImVec2( 5.0f * ( tile + gap ), 5.0f * ( tile + gap ) ) );
        ImGui::SameLine( 0, 36.0f );
        ImGui::BeginGroup( );

        if ( active ) {
            const double multiplier = round.value( "multiplier", 1.0 );
            ui::card_label( "Multiplier" );
            char text[ 32 ];
            snprintf( text, sizeof( text ), "%.2fx", multiplier );
            heading( text, 28.0f );
            ui::card_label( "Cash-out value" );
            heading( ui::format_coins( static_cast<long long>( round.value( "bet", 0 ) * multiplier ) ).c_str( ), 20.0f );
            ImGui::Dummy( ImVec2( 0, 6.0f ) );

            ImGui::BeginDisabled( round.value( "revealed", nlohmann::json::array( ) ).empty( ) || m_animating );
            if ( ui::primary_button( "Cash out", ImVec2( 260.0f, 42.0f ), ui::palette::green ) ) {
                play( L"/v1/casino/mines/cashout", nlohmann::json::object( ) );
            }
            ImGui::EndDisabled( );
        } else {
            draw_bet_controls( );
            ui::card_label( "Mines" );

            for ( int index = 0; index < 3; ++index ) {
                if ( index ) {
                    ImGui::SameLine( 0, 6.0f );
                }

                if ( ui::tab_button( std::to_string( mines_counts[ index ] ).c_str( ), m_mines_count == index, ImVec2( 60.0f, 32.0f ) ) ) {
                    m_mines_count = index;
                }
            }

            ImGui::BeginDisabled( m_animating );
            if ( ui::primary_button( "Start round", ImVec2( 300.0f, 42.0f ), ui::palette::green ) ) {
                play( L"/v1/casino/mines/start", { { "bet", m_bet }, { "mines", mines_counts[ m_mines_count ] }, { "client_seed", m_client_seed } } );
            }
            ImGui::EndDisabled( );

            draw_result_line( round );
        }

        ImGui::EndGroup( );
    }

    void app_t::draw_blackjack( ) {
        const bool active = !m_active_blackjack.is_null( );
        const nlohmann::json round = active ? m_active_blackjack : ( text_of( m_last_round, "game" ) == "blackjack" ? m_last_round : m_active_blackjack );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 card( 84.0f, 120.0f );
        const float deal = m_animating && text_of( m_anim_round, "game" ) == "blackjack" ? ui::ease_out_cubic( ui::clamp01( m_anim_time / 0.35f ) ) : 1.0f;

        const auto draw_cards = [ & ]( const nlohmann::json& cards, ImVec2 origin, bool newest_animated ) {
            const int count = static_cast<int>( cards.size( ) );

            for ( int index = 0; index < count; ++index ) {
                const bool animated = newest_animated && index == count - 1;
                const float offset = animated ? ( 1.0f - deal ) * -40.0f : 0.0f;
                ui::draw_playing_card( draw, ImVec2( origin.x + index * 30.0f, origin.y + offset ), card, cards[ index ].get<std::string>( ), animated ? deal : 1.0f );
            }
        };

        nlohmann::json dealer = round.is_object( ) ? round.value( "dealer_cards", nlohmann::json::array( ) ) : nlohmann::json::array( );
        const bool dealing = m_animating && !active && text_of( m_anim_round, "game" ) == "blackjack" && dealer.size( ) >= 2;

        if ( dealing ) {
            const int extra = static_cast<int>( dealer.size( ) ) - 2;
            const int shown = 2 + std::clamp( static_cast<int>( std::floor( ( m_anim_time - 0.6f ) / 0.7f ) ), 0, extra );

            dealer.erase( dealer.begin( ) + shown, dealer.end( ) );

            if ( m_anim_time < 0.6f ) {
                dealer[ 1 ] = "??";
            }
        }

        ui::card_label( "DEALER" );
        if ( !dealing && round.is_object( ) && round.contains( "dealer_total" ) && round[ "dealer_total" ].is_number( ) ) {
            ImGui::SameLine( );
            ImGui::TextUnformatted( std::to_string( round[ "dealer_total" ].get<int>( ) ).c_str( ) );
        }

        const ImVec2 dealer_origin = ImGui::GetCursorScreenPos( );
        draw_cards( dealer, dealer_origin, false );
        ImGui::Dummy( ImVec2( 10.0f, card.y + 18.0f ) );

        ui::card_label( "YOUR HANDS" );
        const ImVec2 hands_origin = ImGui::GetCursorScreenPos( );
        const nlohmann::json hands = round.is_object( ) ? round.value( "hands", nlohmann::json::array( ) ) : nlohmann::json::array( );
        const int active_hand = round.is_object( ) ? round.value( "active_hand", 0 ) : 0;

        for ( int index = 0; index < static_cast<int>( hands.size( ) ); ++index ) {
            const nlohmann::json& hand = hands[ index ];
            const ImVec2 origin( hands_origin.x + index * 200.0f, hands_origin.y + 4.0f );
            const bool current = active && index == active_hand && hands.size( ) > 1;
            const nlohmann::json cards = hand.value( "cards", nlohmann::json::array( ) );
            const float width = card.x + 30.0f * std::max( 0, static_cast<int>( cards.size( ) ) - 1 );

            if ( current ) {
                draw->AddRect( ImVec2( origin.x - 6.0f, origin.y - 6.0f ), ImVec2( origin.x + width + 6.0f, origin.y + card.y + 6.0f ), ui::to_u32( ui::palette::accent ), 10.0f, 0, 1.5f );
            }

            draw_cards( cards, origin, index == active_hand || !active );

            std::string caption = std::to_string( hand.value( "total", 0 ) ) + "  ·  " + ui::format_coins( hand.value( "bet", 0ll ) );
            if ( hand.value( "doubled", false ) ) caption += "  ·  2x";
            const std::string result = text_of( hand, "result" );
            const bool show_result = !result.empty( ) && ( !m_animating || active );

            draw->AddText( ImVec2( origin.x, origin.y + card.y + 10.0f ), ui::to_u32( ui::palette::muted ), caption.c_str( ) );

            if ( show_result ) {
                draw->AddText( ui::fonts::bold, 14.0f, ImVec2( origin.x, origin.y + card.y + 30.0f ), ui::to_u32( status_color( result ) ), result.c_str( ) );
            }
        }

        ImGui::Dummy( ImVec2( 10.0f, card.y + 58.0f ) );

        const std::string phase = text_of( round, "phase" );

        ImGui::BeginDisabled( m_animating );
        if ( active && phase == "insurance" ) {
            if ( ui::primary_button( "Take insurance", ImVec2( 170.0f, 42.0f ), ui::palette::gold ) ) {
                play( L"/v1/casino/blackjack/action", { { "action", "insurance" } } );
            }

            ImGui::SameLine( 0, 8.0f );

            if ( ImGui::Button( "No insurance", ImVec2( 170.0f, 42.0f ) ) ) {
                play( L"/v1/casino/blackjack/action", { { "action", "decline_insurance" } } );
            }
        } else if ( active ) {
            if ( ui::primary_button( "Hit", ImVec2( 120.0f, 42.0f ), ui::palette::accent ) ) {
                play( L"/v1/casino/blackjack/action", { { "action", "hit" } } );
            }

            ImGui::SameLine( 0, 8.0f );
            if ( ui::primary_button( "Stand", ImVec2( 120.0f, 42.0f ), ui::palette::purple ) ) {
                play( L"/v1/casino/blackjack/action", { { "action", "stand" } } );
            }

            ImGui::SameLine( 0, 8.0f );
            ImGui::BeginDisabled( !round.value( "can_double", false ) );
            if ( ImGui::Button( "Double", ImVec2( 120.0f, 42.0f ) ) ) {
                play( L"/v1/casino/blackjack/action", { { "action", "double" } } );
            }
            ImGui::EndDisabled( );

            ImGui::SameLine( 0, 8.0f );
            ImGui::BeginDisabled( !round.value( "can_split", false ) );
            if ( ImGui::Button( "Split", ImVec2( 120.0f, 42.0f ) ) ) {
                play( L"/v1/casino/blackjack/action", { { "action", "split" } } );
            }
            ImGui::EndDisabled( );
        } else {
            draw_bet_controls( );

            if ( ui::primary_button( "Deal", ImVec2( 300.0f, 42.0f ), ui::palette::purple ) ) {
                play( L"/v1/casino/blackjack/start", { { "bet", m_bet }, { "client_seed", m_client_seed } } );
            }
        }
        ImGui::EndDisabled( );

        if ( active ) {
            ui::card_label( text_of( round, "message" ).c_str( ) );
        } else {
            draw_result_line( round );
        }
    }

    void app_t::draw_chicken( ) {
        const bool active = !m_active_chicken.is_null( );
        const nlohmann::json round = active ? m_active_chicken : ( text_of( m_last_round, "game" ) == "chicken" ? m_last_round : m_active_chicken );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 origin = ImGui::GetCursorScreenPos( );
        const float lane = std::floor( ImGui::GetContentRegionAvail( ).x / 6.0f );
        const int crossed = round.is_object( ) ? round.value( "crossed", 0 ) : 0;
        const bool caught = round.is_object( ) && round.value( "caught", false ) && !m_animating;

        for ( int index = 0; index <= 5; ++index ) {
            const ImVec2 min( origin.x + index * lane, origin.y );
            const ImVec2 max( min.x + lane - 8.0f, origin.y + 210.0f );
            ImVec4 color = ui::palette::surface;

            if ( index > 0 && index <= crossed ) {
                color = caught && index == crossed ? ImVec4( 0.30f, 0.10f, 0.10f, 1.0f ) : ImVec4( 0.10f, 0.22f, 0.14f, 1.0f );
            }

            draw->AddRectFilled( min, max, ui::to_u32( color ), 8.0f );
            draw->AddRect( min, max, ui::to_u32( ui::palette::border, 0.5f ), 8.0f );

            for ( float y = min.y + 14.0f; y < max.y - 30.0f; y += 26.0f ) {
                draw->AddLine( ImVec2( max.x - 1.0f, y ), ImVec2( max.x - 1.0f, y + 12.0f ), ui::to_u32( ui::palette::muted, 0.25f ), 1.0f );
            }

            const std::string label = index == 0 ? "Start" : "Lane " + std::to_string( index );
            draw->AddText( ImVec2( min.x + 12.0f, max.y - 26.0f ), ui::to_u32( ui::palette::muted ), label.c_str( ) );
        }

        m_chicken_x = ui::approach( m_chicken_x, static_cast<float>( crossed ), 7.0f, ImGui::GetIO( ).DeltaTime );
        const float hop = std::fabs( std::sin( ( m_chicken_x - std::floor( m_chicken_x ) ) * pi ) ) * 26.0f;
        const ImVec2 body( origin.x + m_chicken_x * lane + lane * 0.5f - 4.0f, origin.y + 96.0f - hop );

        draw->AddCircleFilled( ImVec2( body.x, origin.y + 124.0f ), 14.0f - hop * 0.2f, IM_COL32( 0, 0, 0, 60 ), 24 );
        draw->AddCircleFilled( body, 20.0f, ui::to_u32( caught ? ui::palette::red : ui::palette::gold ), 32 );
        draw->AddCircleFilled( ImVec2( body.x + 7.0f, body.y - 5.0f ), 2.5f, ui::to_u32( ui::palette::background ), 12 );

        ImGui::Dummy( ImVec2( lane * 6.0f, 222.0f ) );

        ImGui::BeginDisabled( m_animating );
        if ( active ) {
            ui::card_label( "Multiplier" );
            char text[ 32 ];
            snprintf( text, sizeof( text ), "%.2fx", round.value( "multiplier", 1.0 ) );
            heading( text, 26.0f );

            if ( ui::primary_button( "Jump", ImVec2( 160.0f, 42.0f ), ui::palette::gold ) ) {
                play( L"/v1/casino/chicken/jump", nlohmann::json::object( ) );
            }

            ImGui::SameLine( 0, 8.0f );
            ImGui::BeginDisabled( crossed < 1 );
            if ( ui::primary_button( "Cash out", ImVec2( 160.0f, 42.0f ), ui::palette::green ) ) {
                play( L"/v1/casino/chicken/cashout", nlohmann::json::object( ) );
            }
            ImGui::EndDisabled( );
        } else {
            draw_bet_controls( );
            ui::card_label( "Risk" );
            static constexpr const char* risk_labels[ ] = { "Safe 1.15x", "Spicy 1.5x", "Wild 2.75x" };

            for ( int index = 0; index < 3; ++index ) {
                if ( index ) {
                    ImGui::SameLine( 0, 6.0f );
                }

                if ( ui::tab_button( risk_labels[ index ], m_chicken_risk == index, ImVec2( 116.0f, 32.0f ) ) ) {
                    m_chicken_risk = index;
                }
            }

            if ( ui::primary_button( "Start round", ImVec2( 300.0f, 42.0f ), ui::palette::gold ) ) {
                m_chicken_x = 0.0f;
                play( L"/v1/casino/chicken/start", { { "bet", m_bet }, { "risk", chicken_risks[ m_chicken_risk ] }, { "client_seed", m_client_seed } } );
            }

            draw_result_line( round );
        }
        ImGui::EndDisabled( );
    }

    void app_t::draw_fairness( ) {
        ImGui::PushTextWrapPos( 0.0f );
        ui::card_label( "Every result is HMAC-SHA256(server_seed, client_seed:nonce:game:cursor). The server publishes sha256(server_seed) before the round and reveals the seed once it settles." );
        ImGui::PopTextWrapPos( );
        ImGui::Dummy( ImVec2( 0, 8.0f ) );

        ui::card_label( "Next commitment" );
        ImGui::TextUnformatted( m_commitment.c_str( ) );
        ui::card_label( "Next nonce" );
        ImGui::Text( "%lld", m_nonce );

        ui::card_label( "Client seed" );
        ImGui::SetNextItemWidth( 360.0f );
        ImGui::InputText( "##seed", m_client_seed, sizeof( m_client_seed ) );
        ImGui::SameLine( 0, 6.0f );

        if ( ImGui::Button( "Randomize" ) ) {
            strncpy_s( m_client_seed, net::new_client_seed( ).c_str( ), _TRUNCATE );
        }

        ImGui::Dummy( ImVec2( 0, 8.0f ) );
        ImGui::Separator( );
        ImGui::Dummy( ImVec2( 0, 4.0f ) );
        ui::card_label( "Last settled round" );

        if ( m_history.empty( ) ) {
            ImGui::TextUnformatted( "No rounds yet." );
        } else {
            ImGui::TextWrapped( "%s", verification_text( m_history.front( ) ).c_str( ) );
        }
    }

    void app_t::draw_history( ) {
        if ( m_history.empty( ) ) {
            ui::card_label( "No rounds yet." );
            return;
        }

        ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, ImVec2( 10.0f, 8.0f ) );
        if ( ImGui::BeginTable( "history", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY ) ) {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( "Game" );
            ImGui::TableSetupColumn( "Bet" );
            ImGui::TableSetupColumn( "Result" );
            ImGui::TableSetupColumn( "Change" );
            ImGui::TableSetupColumn( "Nonce" );
            ImGui::TableHeadersRow( );

            for ( const auto& round : m_history ) {
                const std::string status = text_of( round, "status" );

                ImGui::TableNextRow( );
                ImGui::TableNextColumn( );
                ImGui::TextUnformatted( text_of( round, "game" ).c_str( ) );
                ImGui::TableNextColumn( );
                ImGui::TextUnformatted( ui::format_coins( round.value( "bet", 0ll ) ).c_str( ) );
                ImGui::TableNextColumn( );
                ImGui::TextColored( status_color( status ), "%s", status.c_str( ) );
                ImGui::TableNextColumn( );
                ImGui::TextColored( status_color( status ), "%s", signed_coins( round.value( "coin_change", 0ll ) ).c_str( ) );
                ImGui::TableNextColumn( );
                ImGui::TextDisabled( "%lld", round.contains( "proof" ) ? round[ "proof" ].value( "nonce", 0ll ) : 0ll );
            }

            ImGui::EndTable( );
        }
        ImGui::PopStyleVar( );
    }

    bool app_t::authenticate( const char* path ) {
        const std::wstring route = std::string( path ) == "register" ? L"/v1/auth/register" : L"/v1/auth/login";
        const net::response_t response = m_api.post( route, { { "username", m_username }, { "password", m_password } } );

        SecureZeroMemory( m_password, sizeof( m_password ) );

        if ( !response.ok( ) ) {
            m_login_error = response.message( );
            return false;
        }

        m_api.set_token( response.body[ "session" ].value( "token", std::string( ) ) );
        m_login_error.clear( );
        m_logged_in = true;
        m_tab = tab_t::lobby;
        refresh_state( );
        m_shown_balance = static_cast<float>( m_balance );

        return true;
    }

    void app_t::refresh_state( ) {
        const net::response_t response = m_api.get( L"/v1/casino/state" );

        if ( !response.ok( ) ) {
            notify( response.message( ), true );

            if ( response.status == 401 ) {
                m_logged_in = false;
            }

            return;
        }

        const nlohmann::json& body = response.body;

        m_name = body.value( "username", std::string( ) );
        m_balance = body.value( "balance", 0ll );
        m_pending_balance = m_balance;
        m_commitment = body[ "fairness" ].value( "commitment", std::string( ) );
        m_nonce = body[ "fairness" ].value( "nonce", 0ll );

        const nlohmann::json active = body.value( "active", nlohmann::json::object( ) );
        m_active_mines = active.contains( "mines" ) ? active[ "mines" ] : nlohmann::json( );
        m_active_blackjack = active.contains( "blackjack" ) ? active[ "blackjack" ] : nlohmann::json( );
        m_active_chicken = active.contains( "chicken" ) ? active[ "chicken" ] : nlohmann::json( );
        m_active_crash = active.contains( "crash" ) ? active[ "crash" ] : nlohmann::json( );
        m_crash_clock = static_cast<float>( number_of( m_active_crash, "elapsed_ms", 0.0 ) / 1000.0 );
        m_chicken_x = m_active_chicken.is_object( ) ? static_cast<float>( m_active_chicken.value( "crossed", 0 ) ) : 0.0f;

        m_history.clear( );

        for ( const auto& round : body.value( "history", nlohmann::json::array( ) ) ) {
            m_history.push_front( round );
        }
    }

    bool app_t::play( const wchar_t* path, nlohmann::json body ) {
        if ( m_animating ) {
            return false;
        }

        body[ "request_id" ] = net::new_request_id( );

        const net::response_t response = m_api.post( path, body );

        if ( !response.ok( ) ) {
            notify( response.message( ), true );
            return false;
        }

        const nlohmann::json& round = response.body[ "game" ];
        const long long balance = response.body.value( "balance", m_balance );
        const bool settled = text_of( round, "status" ) != "playing";

        m_pending_balance = balance;
        m_balance = settled ? balance - round.value( "payout", 0ll ) : balance;
        apply_round( round );

        return true;
    }

    void app_t::apply_round( const nlohmann::json& round ) {
        const std::string game = text_of( round, "game" );
        const bool playing = text_of( round, "status" ) == "playing";
        const nlohmann::json& proof = round[ "proof" ];

        m_commitment = proof.value( "next_commitment", m_commitment );
        m_nonce = proof.value( "nonce", m_nonce ) + 1;
        m_last_round = round;

        if ( game == "mines" ) m_active_mines = playing ? round : nlohmann::json( );
        if ( game == "blackjack" ) m_active_blackjack = playing ? round : nlohmann::json( );
        if ( game == "chicken" ) m_active_chicken = playing ? round : nlohmann::json( );

        if ( game == "crash" ) {
            m_active_crash = playing ? round : nlohmann::json( );

            if ( playing ) {
                m_crash_clock = static_cast<float>( number_of( round, "elapsed_ms", 0.0 ) / 1000.0 );
                m_crash_poll = 0.2f;
            }
        }

        m_anim_round = round;
        m_anim_time = 0.0f;

        if ( game == "roulette" ) {
            m_anim_duration = 5.2f;
        } else if ( game == "crash" ) {
            m_anim_duration = playing ? 0.01f : 0.6f;
        } else if ( game == "blackjack" ) {
            const int dealer_cards = round.contains( "dealer_cards" ) && round[ "dealer_cards" ].is_array( ) ? static_cast<int>( round[ "dealer_cards" ].size( ) ) : 2;
            m_anim_duration = playing ? 0.35f : 1.3f + 0.7f * std::max( 0, dealer_cards - 2 );
        } else {
            m_anim_duration = playing ? 0.25f : 0.6f;
        }

        m_animating = true;
    }

    void app_t::finish_animation( ) {
        m_balance = m_pending_balance;

        if ( !m_anim_round.is_object( ) || text_of( m_anim_round, "status" ) == "playing" ) {
            return;
        }

        m_history.push_front( m_anim_round );

        while ( m_history.size( ) > 50 ) {
            m_history.pop_back( );
        }

        const long long change = m_anim_round.value( "coin_change", 0ll );
        const long long payout = m_anim_round.value( "payout", 0ll );

        if ( payout > 0 ) {
            notify( "You received " + ui::format_coins( payout ) + "  (" + signed_coins( change ) + ")", false );
        }
    }

    void app_t::notify( const std::string& message, bool error ) {
        m_toast = message;
        m_toast_error = error;
        m_toast_time = 3.0f;
    }

    std::string app_t::verification_text( const nlohmann::json& round ) const {
        const nlohmann::json& proof = round[ "proof" ];
        const std::string server_seed = text_of( proof, "server_seed" );
        const std::string commitment = text_of( proof, "commitment" );
        const std::string client_seed = text_of( proof, "client_seed" );
        const long long nonce = proof.value( "nonce", 0ll );

        std::string text = "Game: " + text_of( round, "game" ) + "\nServer seed: " + server_seed + "\nCommitment: " + commitment + "\nClient seed: " + client_seed + "\nNonce: " + std::to_string( nonce );

        const bool committed = net::verify_commitment( server_seed, commitment );
        text += committed ? "\n\nsha256(server_seed) matches the commitment." : "\n\nCommitment mismatch.";

        if ( committed && text_of( round, "game" ) == "roulette" ) {
            const std::uint32_t number = net::fair_int( server_seed, client_seed, nonce, "roulette", 37 );
            const bool same = static_cast<int>( number ) == round.value( "number", -1 );
            text += "\nRecomputed pocket: " + std::to_string( number ) + ( same ? " (matches)" : " (mismatch)" );
        }

        return text;
    }

}
