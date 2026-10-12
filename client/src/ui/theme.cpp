#include "ui/theme.hpp"

#include <cstdio>

namespace ui {

    static bool file_exists( const char* path ) {
        FILE* file = nullptr;

        if ( fopen_s( &file, path, "rb" ) != 0 || !file ) {
            return false;
        }

        fclose( file );
        return true;
    }

    void load_fonts( ) {
        ImGuiIO& io = ImGui::GetIO( );

        if ( file_exists( "C:/Windows/Fonts/segoeui.ttf" ) ) {
            fonts::regular = io.Fonts->AddFontFromFileTTF( "C:/Windows/Fonts/segoeui.ttf", 16.0f );

            if ( file_exists( "C:/Windows/Fonts/seguisym.ttf" ) ) {
                ImFontConfig merge;
                merge.MergeMode = true;
                io.Fonts->AddFontFromFileTTF( "C:/Windows/Fonts/seguisym.ttf", 16.0f, &merge );
            }
        }

        if ( !fonts::regular ) {
            fonts::regular = io.Fonts->AddFontDefault( );
        }

        fonts::bold = file_exists( "C:/Windows/Fonts/seguisb.ttf" )
            ? io.Fonts->AddFontFromFileTTF( "C:/Windows/Fonts/seguisb.ttf", 16.0f )
            : fonts::regular;

        io.FontDefault = fonts::regular;
    }

    void apply_theme( ) {
        ImGuiStyle& style = ImGui::GetStyle( );

        style.WindowRounding = 0.0f;
        style.ChildRounding = 12.0f;
        style.FrameRounding = 8.0f;
        style.PopupRounding = 8.0f;
        style.GrabRounding = 6.0f;
        style.WindowPadding = ImVec2( 0.0f, 0.0f );
        style.FramePadding = ImVec2( 12.0f, 8.0f );
        style.ItemSpacing = ImVec2( 10.0f, 10.0f );
        style.ScrollbarSize = 0.0f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;

        ImVec4* colors = style.Colors;

        colors[ ImGuiCol_WindowBg ] = palette::background;
        colors[ ImGuiCol_ChildBg ] = palette::surface;
        colors[ ImGuiCol_PopupBg ] = palette::surface;
        colors[ ImGuiCol_Border ] = palette::border;
        colors[ ImGuiCol_Text ] = palette::text;
        colors[ ImGuiCol_TextDisabled ] = palette::muted;
        colors[ ImGuiCol_FrameBg ] = palette::background;
        colors[ ImGuiCol_FrameBgHovered ] = palette::surface_hover;
        colors[ ImGuiCol_FrameBgActive ] = palette::surface_hover;
        colors[ ImGuiCol_Button ] = palette::surface_hover;
        colors[ ImGuiCol_ButtonHovered ] = ImVec4( 0.290f, 0.290f, 0.298f, 1.0f );
        colors[ ImGuiCol_ButtonActive ] = ImVec4( 0.333f, 0.333f, 0.345f, 1.0f );
        colors[ ImGuiCol_Header ] = palette::surface_hover;
        colors[ ImGuiCol_HeaderHovered ] = ImVec4( 0.290f, 0.290f, 0.298f, 1.0f );
        colors[ ImGuiCol_HeaderActive ] = ImVec4( 0.333f, 0.333f, 0.345f, 1.0f );
        colors[ ImGuiCol_TableRowBgAlt ] = ImVec4( 1.0f, 1.0f, 1.0f, 0.025f );
        colors[ ImGuiCol_TableHeaderBg ] = palette::surface;
        colors[ ImGuiCol_ScrollbarBg ] = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
        colors[ ImGuiCol_ScrollbarGrab ] = palette::border;
        colors[ ImGuiCol_ScrollbarGrabHovered ] = palette::muted;
        colors[ ImGuiCol_ScrollbarGrabActive ] = palette::accent;
        colors[ ImGuiCol_SliderGrab ] = palette::accent;
        colors[ ImGuiCol_SliderGrabActive ] = palette::purple;
        colors[ ImGuiCol_CheckMark ] = palette::accent;
        colors[ ImGuiCol_Separator ] = palette::border;
    }

}
