#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <functional>

class D3D11Renderer
{
public:
	using DrawCallback = std::function<void()>;

	bool Initialize(HWND hwnd, UINT monitorHz);
	void Shutdown();
	void Resize(UINT width, UINT height);
	void Render(const DrawCallback& drawCallback, const float clearColor[4], bool vsync = true);
	bool IsOccluded() const { return m_occluded; }
	bool TestOccluded();

	ID3D11Device* Device() const { return m_device; }
	ID3D11DeviceContext* Context() const { return m_context; }

private:
	template<typename T>
	static void Release(T*& resource)
	{
		if (resource)
		{
			resource->Release();
			resource = nullptr;
		}
	}

	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	IDXGISwapChain* m_swapChain = nullptr;
	ID3D11RenderTargetView* m_renderTarget = nullptr;
	bool m_occluded = false;
};
