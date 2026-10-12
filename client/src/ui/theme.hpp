#pragma once

#include <cmath>

#include <imgui.h>

namespace ui {

    namespace palette {
        inline const ImVec4 background = ImVec4( 0.110f, 0.110f, 0.118f, 1.0f );
        inline const ImVec4 sidebar = ImVec4( 0.141f, 0.141f, 0.149f, 1.0f );
        inline const ImVec4 surface = ImVec4( 0.173f, 0.173f, 0.180f, 1.0f );
        inline const ImVec4 surface_hover = ImVec4( 0.227f, 0.227f, 0.235f, 1.0f );
        inline const ImVec4 border = ImVec4( 0.220f, 0.220f, 0.227f, 1.0f );
        inline const ImVec4 text = ImVec4( 0.961f, 0.961f, 0.969f, 1.0f );
        inline const ImVec4 muted = ImVec4( 0.557f, 0.557f, 0.576f, 1.0f );
        inline const ImVec4 accent = ImVec4( 0.039f, 0.518f, 1.0f, 1.0f );
        inline const ImVec4 purple = ImVec4( 0.749f, 0.353f, 0.949f, 1.0f );
        inline const ImVec4 green = ImVec4( 0.188f, 0.820f, 0.345f, 1.0f );
        inline const ImVec4 red = ImVec4( 1.0f, 0.271f, 0.227f, 1.0f );
        inline const ImVec4 gold = ImVec4( 1.0f, 0.839f, 0.039f, 1.0f );
    }

    namespace fonts {
        inline ImFont* regular = nullptr;
        inline ImFont* bold = nullptr;
    }

    void load_fonts( );

    inline float ease_out_cubic( float t ) {
        const float inverse = 1.0f - t;
        return 1.0f - inverse * inverse * inverse;
    }

    inline float ease_in_out_quad( float t ) {
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow( -2.0f * t + 2.0f, 2.0f ) / 2.0f;
    }

    inline float clamp01( float value ) {
        return value < 0.0f ? 0.0f : ( value > 1.0f ? 1.0f : value );
    }

    inline float approach( float current, float target, float speed, float dt ) {
        return current + ( target - current ) * clamp01( speed * dt );
    }

    inline ImU32 to_u32( const ImVec4& color, float alpha = 1.0f ) {
        return ImGui::ColorConvertFloat4ToU32( ImVec4( color.x, color.y, color.z, color.w * alpha ) );
    }

    void apply_theme( );

}
