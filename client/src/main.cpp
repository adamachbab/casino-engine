#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dwmapi.h>

#include <exception>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "app.hpp"
#include "ui/theme.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM w_param, LPARAM l_param );

namespace {

    constexpr int window_width = 1180;
    constexpr int window_height = 760;
    constexpr int title_bar_height = 40;
    constexpr int close_button_width = 46;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;

    void create_render_target( ) {
        ID3D11Texture2D* back_buffer = nullptr;
        swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) );
        device->CreateRenderTargetView( back_buffer, nullptr, &render_target );
        back_buffer->Release( );
    }

    void release_render_target( ) {
        if ( render_target ) {
            render_target->Release( );
            render_target = nullptr;
        }
    }

    bool create_device( HWND window ) {
        DXGI_SWAP_CHAIN_DESC desc = { };
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL levels[ ] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL level;

        if ( D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &swap_chain, &device, &level, &context ) != S_OK ) {
            return false;
        }

        create_render_target( );
        return true;
    }

    void destroy_device( ) {
        release_render_target( );

        if ( swap_chain ) { swap_chain->Release( ); swap_chain = nullptr; }
        if ( context ) { context->Release( ); context = nullptr; }
        if ( device ) { device->Release( ); device = nullptr; }
    }

    LRESULT CALLBACK window_proc( HWND window, UINT message, WPARAM w_param, LPARAM l_param ) {
        if ( ImGui_ImplWin32_WndProcHandler( window, message, w_param, l_param ) ) {
            return true;
        }

        switch ( message ) {
        case WM_NCCALCSIZE:
            if ( w_param ) {
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT point = { GET_X_LPARAM( l_param ), GET_Y_LPARAM( l_param ) };
            ScreenToClient( window, &point );

            RECT client;
            GetClientRect( window, &client );

            if ( point.y >= 0 && point.y < title_bar_height && point.x < client.right - close_button_width * 2 ) {
                return HTCAPTION;
            }

            return HTCLIENT;
        }
        case WM_SIZE:
            if ( device && w_param != SIZE_MINIMIZED ) {
                release_render_target( );
                swap_chain->ResizeBuffers( 0, LOWORD( l_param ), HIWORD( l_param ), DXGI_FORMAT_UNKNOWN, 0 );
                create_render_target( );
            }
            return 0;
        case WM_SYSCOMMAND:
            if ( ( w_param & 0xfff0 ) == SC_KEYMENU ) {
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage( 0 );
            return 0;
        }

        return DefWindowProcW( window, message, w_param, l_param );
    }

}

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE, PWSTR, int ) {
    WNDCLASSEXW window_class = { sizeof( window_class ) };
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW( nullptr, IDC_ARROW );
    window_class.lpszClassName = L"casino_engine_window";
    RegisterClassExW( &window_class );

    const int x = ( GetSystemMetrics( SM_CXSCREEN ) - window_width ) / 2;
    const int y = ( GetSystemMetrics( SM_CYSCREEN ) - window_height ) / 2;

    HWND window = CreateWindowExW(
        0, window_class.lpszClassName, L"casino-engine",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, window_width, window_height, nullptr, nullptr, instance, nullptr );

    if ( !window || !create_device( window ) ) {
        MessageBoxW( nullptr, L"Unable to initialize Direct3D 11.", L"casino-engine", MB_ICONERROR );
        return 1;
    }

    const BOOL dark = TRUE;
    DwmSetWindowAttribute( window, 20, &dark, sizeof( dark ) );

    ShowWindow( window, SW_SHOWDEFAULT );
    UpdateWindow( window );

    ImGui::CreateContext( );
    ImGui::GetIO( ).IniFilename = nullptr;
    ui::apply_theme( );
    ui::load_fonts( );

    ImGui_ImplWin32_Init( window );
    ImGui_ImplDX11_Init( device, context );

    app::app_t application( L"127.0.0.1", 8420 );

    bool running = true;

    while ( running ) {
        MSG message;

        while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) ) {
            TranslateMessage( &message );
            DispatchMessageW( &message );

            if ( message.message == WM_QUIT ) {
                running = false;
            }
        }

        if ( !running ) {
            break;
        }

        ImGui_ImplDX11_NewFrame( );
        ImGui_ImplWin32_NewFrame( );
        ImGui::NewFrame( );

        RECT client;
        GetClientRect( window, &client );

        app::frame_result_t frame = app::frame_result_t::none;
        ImGuiErrorRecoveryState recovery;
        ImGui::ErrorRecoveryStoreState( &recovery );

        try {
            frame = application.render( static_cast<float>( client.right ), static_cast<float>( client.bottom ), title_bar_height, close_button_width );
        } catch ( const std::exception& ) {
            ImGui::ErrorRecoveryTryToRecoverState( &recovery );
        }

        if ( frame == app::frame_result_t::close ) {
            PostMessageW( window, WM_CLOSE, 0, 0 );
        } else if ( frame == app::frame_result_t::minimize ) {
            ShowWindow( window, SW_MINIMIZE );
        }

        ImGui::Render( );

        const float clear[ 4 ] = { 0.110f, 0.110f, 0.118f, 1.0f };
        context->OMSetRenderTargets( 1, &render_target, nullptr );
        context->ClearRenderTargetView( render_target, clear );
        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );

        swap_chain->Present( 1, 0 );
    }

    ImGui_ImplDX11_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    ImGui::DestroyContext( );

    destroy_device( );
    DestroyWindow( window );
    UnregisterClassW( window_class.lpszClassName, instance );

    return 0;
}
