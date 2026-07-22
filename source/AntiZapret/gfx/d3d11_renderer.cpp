#include "gfx/d3d11_renderer.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui.h"

bool D3D11Renderer::Initialize(HWND hwnd, UINT monitorHz)
{
	DXGI_SWAP_CHAIN_DESC desc = {};
	desc.BufferCount = 2;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferDesc.RefreshRate = { monitorHz, 1 };
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = hwnd;
	desc.SampleDesc.Count = 1;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	auto tryCreate = [&](D3D_DRIVER_TYPE type) {
		return D3D11CreateDeviceAndSwapChain(
			nullptr, type, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
			&desc, &m_swapChain, &m_device, &level, &m_context);
	};

	HRESULT hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE);
	if (hr == DXGI_ERROR_UNSUPPORTED)
		hr = tryCreate(D3D_DRIVER_TYPE_WARP);
	if (FAILED(hr))
		return false;

	ID3D11Texture2D* backBuffer = nullptr;
	if (SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
	{
		m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
		backBuffer->Release();
	}
	return m_renderTarget != nullptr;
}

void D3D11Renderer::Shutdown()
{
	Release(m_renderTarget);
	Release(m_swapChain);
	Release(m_context);
	Release(m_device);
	m_occluded = false;
}

void D3D11Renderer::Resize(UINT width, UINT height)
{
	if (!m_swapChain || !width || !height)
		return;

	Release(m_renderTarget);
	m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

	ID3D11Texture2D* backBuffer = nullptr;
	if (SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
	{
		m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
		backBuffer->Release();
	}
}

void D3D11Renderer::Render(const DrawCallback& drawCallback, const float clearColor[4], bool vsync)
{
	if (!m_renderTarget || !drawCallback)
		return;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	drawCallback();
	ImGui::Render();

	m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
	m_context->ClearRenderTargetView(m_renderTarget, clearColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	m_occluded = m_swapChain->Present(vsync ? 1 : 0, 0) == DXGI_STATUS_OCCLUDED;
}

bool D3D11Renderer::TestOccluded()
{
	if (!m_swapChain)
		return false;
	m_occluded = m_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED;
	return m_occluded;
}
