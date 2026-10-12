#pragma once

#include <string>

#include <imgui.h>

namespace ui {

    bool tab_button( const char* label, bool selected, const ImVec2& size );
    bool primary_button( const char* label, const ImVec2& size, const ImVec4& color );
    void card_label( const char* label );
    void draw_playing_card( ImDrawList* draw, const ImVec2& position, const ImVec2& size, const std::string& card, float reveal );
    std::string format_coins( long long value );

}
