#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include "ui/theme.hpp"

namespace ui {

    bool tab_button( const char* label, bool selected, const ImVec2& size ) {
        ImGuiWindow* window = ImGui::GetCurrentWindow( );
        const ImGuiID id = window->GetID( label );
        const ImVec2 position = window->DC.CursorPos;
        const ImRect bounds( position, ImVec2( position.x + size.x, position.y + size.y ) );

        ImGui::ItemSize( size );
        if ( !ImGui::ItemAdd( bounds, id ) ) {
            return false;
        }

        bool hovered = false;
        bool held = false;
        const bool pressed = ImGui::ButtonBehavior( bounds, id, &hovered, &held );

        float& blend = *window->StateStorage.GetFloatRef( id, 0.0f );
        blend = approach( blend, selected ? 1.0f : ( hovered ? 0.5f : 0.0f ), 14.0f, ImGui::GetIO( ).DeltaTime );

        ImDrawList* draw = window->DrawList;

        if ( blend > 0.01f ) {
            const ImU32 fill = selected ? to_u32( palette::accent, 0.9f * blend ) : to_u32( palette::text, 0.07f * blend );
            draw->AddRectFilled( bounds.Min, bounds.Max, fill, 7.0f );
        }

        const ImVec2 text_size = ImGui::CalcTextSize( label );
        const ImU32 color = selected ? IM_COL32_WHITE : to_u32( palette::text, 0.82f );

        draw->AddText(
            ImVec2( bounds.Min.x + 12.0f, bounds.Min.y + ( size.y - text_size.y ) * 0.5f ),
            color, label );

        return pressed;
    }

    bool primary_button( const char* label, const ImVec2& size, const ImVec4& color ) {
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( color.x, color.y, color.z, 0.85f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, color );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );

        const bool pressed = ImGui::Button( label, size );

        ImGui::PopStyleColor( 4 );
        return pressed;
    }

    void card_label( const char* label ) {
        ImGui::PushStyleColor( ImGuiCol_Text, palette::muted );
        ImGui::TextUnformatted( label );
        ImGui::PopStyleColor( );
    }

    void draw_playing_card( ImDrawList* draw, const ImVec2& position, const ImVec2& size, const std::string& card, float reveal ) {
        const float width = size.x * std::max( 0.05f, std::fabs( std::cos( ( 1.0f - clamp01( reveal ) ) * 1.5707963f ) ) );
        const float left = position.x + ( size.x - width ) * 0.5f;
        const ImVec2 min( left, position.y );
        const ImVec2 max( left + width, position.y + size.y );

        const bool hidden = card == "??" || reveal < 0.5f;

        draw->AddRectFilled( min, max, hidden ? to_u32( palette::purple, 0.9f ) : IM_COL32( 245, 247, 250, 255 ), 8.0f );
        draw->AddRect( min, max, to_u32( palette::border ), 8.0f, 0, 1.5f );

        if ( hidden || card.size( ) < 2 || width < size.x * 0.6f ) {
            return;
        }

        const std::string rank = card.substr( 0, card.size( ) - 1 );
        const char suit = card.back( );
        const bool red_suit = suit == 'H' || suit == 'D';
        const char* suit_name = suit == 'S' ? "\xE2\x99\xA0" : ( suit == 'H' ? "\xE2\x99\xA5" : ( suit == 'D' ? "\xE2\x99\xA6" : "\xE2\x99\xA3" ) );
        const ImU32 color = red_suit ? IM_COL32( 205, 50, 45, 255 ) : IM_COL32( 25, 30, 38, 255 );

        draw->AddText( ImVec2( min.x + 8.0f, min.y + 6.0f ), color, rank.c_str( ) );
        draw->AddText( ImVec2( min.x + 8.0f, min.y + 24.0f ), color, suit_name );

        const ImVec2 rank_size = ImGui::CalcTextSize( rank.c_str( ) );
        draw->AddText( ImVec2( max.x - rank_size.x - 8.0f, max.y - 38.0f ), color, rank.c_str( ) );
        draw->AddText( ImVec2( max.x - 18.0f, max.y - 22.0f ), color, suit_name );
    }

    std::string format_coins( long long value ) {
        std::string digits = std::to_string( value < 0 ? -value : value );
        std::string text;

        for ( size_t index = 0; index < digits.size( ); ++index ) {
            if ( index > 0 && ( digits.size( ) - index ) % 3 == 0 ) {
                text.push_back( ',' );
            }

            text.push_back( digits[ index ] );
        }

        return value < 0 ? "-" + text : text;
    }

}
