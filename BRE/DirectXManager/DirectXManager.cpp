#include "DirectXManager.h"

#include <ApplicationSettings\ApplicationSettings.h>

namespace BRE {
namespace {

IDXGIAdapter1*
GetSupportedAdapter(Microsoft::WRL::ComPtr<IDXGIFactory4>& dxgiFactory,
                    const D3D_FEATURE_LEVEL featureLevel)
{
    IDXGIAdapter1* adapter = nullptr;
    for (std::uint32_t adapterIndex = 0U; ; ++adapterIndex) {
        IDXGIAdapter1* currentAdapter = nullptr;
        if (DXGI_ERROR_NOT_FOUND == dxgiFactory->EnumAdapters1(adapterIndex, &currentAdapter)) {
            // No more adapters to enumerate.
            break;
        }

        // Check to see if the adapter supports Direct3D 12, but don't create the
        // actual device yet.
        const HRESULT hres = D3D12CreateDevice(currentAdapter,
                                               featureLevel,
                                               _uuidof(ID3D12Device),
                                               nullptr);
        if (SUCCEEDED(hres)) {
            adapter = currentAdapter;
            break;
        }

        currentAdapter->Release();
    }

    return adapter;
}

void
InitMainWindow(HWND& windowHandle,
               const HINSTANCE moduleInstanceHandle) noexcept
{
    WNDCLASS windowClass = {};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = DefWindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = moduleInstanceHandle;
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = L"MainWnd";

#ifdef _DEBUG
    BRE_ASSERT(RegisterClass(&windowClass));
#else
    RegisterClass(&windowClass);
#endif

    // Compute window rectangle dimensions based on requested client area dimensions.
    RECT rect = { 0, 0, static_cast<long>(ApplicationSettings::sWindowWidth), static_cast<long>(ApplicationSettings::sWindowHeight) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
    const int32_t width{ rect.right - rect.left };
    const int32_t height{ rect.bottom - rect.top };

    const std::uint32_t windowStyle =
        ApplicationSettings::sIsFullscreenWindow ? WS_POPUP
        : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    windowHandle = CreateWindowEx(WS_EX_APPWINDOW,
                                  L"MainWnd",
                                  L"App",
                                  windowStyle,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  width,
                                  height,
                                  nullptr,
                                  nullptr,
                                  moduleInstanceHandle,
                                  nullptr);

    BRE_CHECK_MSG(windowHandle, L"Window creation failed");

    ShowWindow(windowHandle, SW_SHOW);
    UpdateWindow(windowHandle);
}
}

HWND DirectXManager::mWindowHandle;
Microsoft::WRL::ComPtr<IDXGIFactory4> DirectXManager::mDxgiFactory{ nullptr };
Microsoft::WRL::ComPtr<ID3D12Device> DirectXManager::mDevice{ nullptr };

void
DirectXManager::InitWindowAndDevice(const HINSTANCE moduleInstanceHandle) noexcept
{
    InitMainWindow(mWindowHandle, moduleInstanceHandle);

#if defined(DEBUG) || defined(_DEBUG) 
    // Enable the D3D12 debug layer.
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        BRE_CHECK_HR(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())));
        debugController->EnableDebugLayer();
    }
#endif

    BRE_CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(mDxgiFactory.GetAddressOf())));

    // Get the first adapter that supports ID3D12Device and a feature level of 
    // the following list.
    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    IDXGIAdapter1* adapter = nullptr;
    for (std::uint32_t i = 0U; i < _countof(featureLevels); ++i) {
        adapter = GetSupportedAdapter(mDxgiFactory, featureLevels[i]);
        if (adapter != nullptr) {
            break;
        }
    }

    BRE_CHECK_MSG(adapter != nullptr, L"No adapter supports ID3D12Device or a feature level");

    BRE_CHECK_HR(D3D12CreateDevice(adapter,
                                   D3D_FEATURE_LEVEL_12_1,
                                   IID_PPV_ARGS(mDevice.GetAddressOf())));
}
}

