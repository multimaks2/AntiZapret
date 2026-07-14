#include "window/window_manager.h"

#include "image/resource.h"
#include "imgui_impl_win32.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	constexpr int kTitleBarHeight = 32;
	constexpr int kBorder = 5;
	constexpr int kButtonAreaWidth = 100;
	constexpr int kSnapPixels = 8;
	constexpr int kAnimationMs = 220;
	constexpr int kDragRestorePixels = 6;
	constexpr wchar_t kWindowClassName[] = L"AntiZapretWindowClass";
}

void WindowManager::WindowAnimation::Begin(HWND hwnd, const RECT& target, bool toMaximized, bool& maximizedFlag)
{
	RECT current = {};
	GetWindowRect(hwnd, &current);
	m_fromLeft = float(current.left);
	m_fromTop = float(current.top);
	m_fromRight = float(current.right);
	m_fromBottom = float(current.bottom);
	m_toLeft = float(target.left);
	m_toTop = float(target.top);
	m_toRight = float(target.right);
	m_toBottom = float(target.bottom);

	LARGE_INTEGER qpc = {};
	QueryPerformanceCounter(&qpc);
	m_startQpc = qpc.QuadPart;
	m_active = true;
	maximizedFlag = toMaximized;

	const DWM_WINDOW_CORNER_PREFERENCE pref = toMaximized ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

void WindowManager::WindowAnimation::Update(HWND hwnd, float progress, bool& active)
{
	(void)hwnd;
	(void)progress;
	active = m_active;
}

RECT WindowManager::WindowAnimation::Lerp(float t) const
{
	return {
		(int)roundf(m_fromLeft + (m_toLeft - m_fromLeft) * t),
		(int)roundf(m_fromTop + (m_toTop - m_fromTop) * t),
		(int)roundf(m_fromRight + (m_toRight - m_fromRight) * t),
		(int)roundf(m_fromBottom + (m_toBottom - m_fromBottom) * t),
	};
}

void WindowManager::DragState::Clear()
{
	drag = false;
	pending = false;
	snapAnim = false;
}

void WindowManager::DragState::Release(HWND hwnd)
{
	Clear();
	ReleaseCapture();
	(void)hwnd;
}

bool WindowManager::DragState::HandleTitleMouse(HWND hwnd, UINT msg, LPARAM lParam, WindowManager& window)
{
	if (msg == WM_LBUTTONDBLCLK)
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		if (!window.IsTitleBar(pt))
			return false;
		Release(hwnd);
		window.ToggleMaximize();
		return true;
	}

	if (msg == WM_LBUTTONDOWN && !drag && !pending)
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		if (!window.IsTitleBar(pt))
			return false;
		GetCursorPos(&cursor);
		clickY = pt.y;
		if (window.IsWindowMaximized())
		{
			pending = true;
			restoreClickY = pt.y;
			pendingCursor = cursor;
		}
		else
		{
			RECT wr = {};
			GetWindowRect(hwnd, &wr);
			windowOrigin = { wr.left, wr.top };
			drag = true;
		}
		SetCapture(hwnd);
		return true;
	}

	if (msg != WM_MOUSEMOVE || !(drag || pending || snapAnim))
		return false;

	POINT currentCursor = {};
	GetCursorPos(&currentCursor);
	if (snapAnim || restoreAnim)
		return true;

	if (pending)
	{
		const int dx = currentCursor.x - pendingCursor.x;
		const int dy = currentCursor.y - pendingCursor.y;
		if (dx * dx + dy * dy >= kDragRestorePixels * kDragRestorePixels)
		{
			window.RestoreFromMaxDrag(currentCursor, restoreClickY);
			pending = false;
			restoreAnim = drag = true;
			cursor = currentCursor;
		}
		return true;
	}

	if (window.m_animation.IsActive())
		return true;

	if (!window.IsWindowMaximized())
	{
		const RECT work = window.WorkArea();
		if (currentCursor.y <= work.top + kSnapPixels)
		{
			window.MaximizeWindow();
			drag = false;
			snapAnim = true;
			window.KeepDragCapture();
			return true;
		}
	}

	windowOrigin.x += currentCursor.x - cursor.x;
	windowOrigin.y += currentCursor.y - cursor.y;
	cursor = currentCursor;
	SetWindowPos(hwnd, nullptr, windowOrigin.x, windowOrigin.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	return true;
}

WindowManager* WindowManager::FromHandle(HWND hwnd)
{
	return reinterpret_cast<WindowManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

bool WindowManager::Create(float dpiScale, int minWidth, int minHeight)
{
	m_minWidth = minWidth;
	m_minHeight = minHeight;
	m_monitorHz = QueryMonitorHz({ 0, 0 });
	QueryPerformanceFrequency(&m_qpcFrequency);

	const HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_CLASSDC | CS_DBLCLKS;
	windowClass.lpfnWndProc = WndProcStatic;
	windowClass.hInstance = instance;
	windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
	windowClass.lpszClassName = kWindowClassName;
	windowClass.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	RegisterClassExW(&windowClass);

	m_hwnd = CreateWindowExW(
		WS_EX_APPWINDOW,
		kWindowClassName,
		L"AntiZapret",
		WS_POPUP,
		100, 100,
		int(800 * dpiScale),
		int(600 * dpiScale),
		nullptr, nullptr, instance, this);
	if (!m_hwnd)
		return false;

	SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	SetIcons();
	SetRounded(true);

	const BOOL dark = TRUE;
	DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
	return true;
}

void WindowManager::Destroy()
{
	if (m_hwnd)
	{
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}
	UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
}

void WindowManager::SetRenderCallback(RenderCallback callback)
{
	m_renderCallback = std::move(callback);
}

void WindowManager::SetResizeCallback(ResizeCallback callback)
{
	m_resizeCallback = std::move(callback);
}

void WindowManager::SetInputHandler(const std::function<bool(UINT, WPARAM, LPARAM)>& handler)
{
	m_inputHandler = handler;
}

void WindowManager::ToggleMaximize()
{
	if (m_maximized)
		RestoreWindow(m_restoreRect.left, m_restoreRect.top);
	else
		MaximizeWindow();
}

void WindowManager::UpdateAnimations()
{
	if (!m_animation.IsActive())
		return;

	if (m_drag.snapAnim || m_drag.restoreAnim)
		KeepDragCapture();

	const float t = AnimationProgress();
	const float eased = EaseOut(t >= 1.f ? 1.f : t);

	if (m_drag.restoreAnim)
	{
		int width = 0;
		int height = 0;
		GetRestoreSize(width, height);
		POINT cursor = {};
		GetCursorPos(&cursor);
		const float x = float(ClampRestoreX(cursor.x, width, MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST)));
		const float y = float(cursor.y - m_drag.restoreClickY);
		const float fromWidth = m_animation.FromWidth();
		const float fromHeight = m_animation.FromHeight();
		ApplyRect(x, y, fromWidth + (float(width) - fromWidth) * eased, fromHeight + (float(height) - fromHeight) * eased);
		if (t >= 1.f)
		{
			ApplyRect(x, y, float(width), float(height));
			m_animation.SetActive(false);
			m_drag.restoreAnim = false;
			SetRounded(true);
			m_drag.windowOrigin = { int(x), int(y) };
			m_drag.cursor = cursor;
		}
		return;
	}

	ApplyRect(m_animation.Lerp(eased));
	if (t >= 1.f)
	{
		m_animation.SetActive(false);
		SetRounded(!m_maximized);
		if (m_drag.snapAnim)
			FinishSnapAnim();
	}
}

void WindowManager::EnsureMinimumSize()
{
	if (!m_hwnd)
		return;

	RECT rect = {};
	GetWindowRect(m_hwnd, &rect);
	const int width = rect.right - rect.left;
	const int height = rect.bottom - rect.top;
	if (width >= m_minWidth && height >= m_minHeight)
		return;

	const int newWidth = width >= m_minWidth ? width : m_minWidth;
	const int newHeight = height >= m_minHeight ? height : m_minHeight;
	SetWindowPos(m_hwnd, nullptr, rect.left, rect.top, newWidth, newHeight, SWP_NOZORDER);
}

LRESULT WindowManager::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (m_drag.HandleTitleMouse(hwnd, msg, lParam, *this))
		return 0;

	if (msg == WM_CAPTURECHANGED && (m_drag.snapAnim || m_drag.restoreAnim || m_drag.pending))
	{
		KeepDragCapture();
		return 0;
	}

	if ((msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) &&
		(m_drag.drag || m_drag.pending || m_drag.restoreAnim || m_drag.snapAnim))
	{
		m_drag.Release(hwnd);
		if (msg == WM_LBUTTONUP)
			return 0;
	}

	if (m_inputHandler && m_inputHandler(msg, wParam, lParam))
		return 0;

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_ACTIVATE:
		m_active = LOWORD(wParam) != WA_INACTIVE;
		return 0;
	case WM_GETMINMAXINFO:
	{
		auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
		info->ptMinTrackSize = { m_minWidth, m_minHeight };
		MONITORINFO monitorInfo = { sizeof(monitorInfo) };
		GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
		info->ptMaxPosition = { monitorInfo.rcWork.left, monitorInfo.rcWork.top };
		info->ptMaxSize = {
			monitorInfo.rcWork.right - monitorInfo.rcWork.left,
			monitorInfo.rcWork.bottom - monitorInfo.rcWork.top,
		};
		return 0;
	}
	case WM_ENTERSIZEMOVE:
		m_inSizeMove = true;
		return 0;
	case WM_EXITSIZEMOVE:
		m_inSizeMove = false;
		return 0;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			m_minimized = true;
		else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
			m_minimized = false;

		if (wParam != SIZE_MINIMIZED)
		{
			if (m_resizeCallback)
				m_resizeCallback(LOWORD(lParam), HIWORD(lParam));
			if (m_ready && m_inSizeMove && m_renderCallback)
				m_renderCallback();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_NCHITTEST:
		return HitTest(hwnd, lParam);
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WindowManager::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NCCREATE)
	{
		const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
	}
	if (WindowManager* window = FromHandle(hwnd))
		return window->HandleMessage(hwnd, msg, wParam, lParam);
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void WindowManager::SetRounded(bool rounded) const
{
	const DWM_WINDOW_CORNER_PREFERENCE pref = rounded ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
	DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

void WindowManager::ApplyRect(const RECT& rect) const
{
	SetWindowPos(
		m_hwnd, nullptr,
		rect.left, rect.top,
		rect.right - rect.left, rect.bottom - rect.top,
		SWP_NOZORDER | SWP_FRAMECHANGED);
}

void WindowManager::ApplyRect(float x, float y, float w, float h) const
{
	SetWindowPos(m_hwnd, nullptr, (int)roundf(x), (int)roundf(y), (int)roundf(w), (int)roundf(h), SWP_NOZORDER | SWP_FRAMECHANGED);
}

RECT WindowManager::WorkArea() const
{
	MONITORINFO info = { sizeof(info) };
	GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &info);
	return info.rcWork;
}

RECT WindowManager::MakeRect(int x, int y, int w, int h) const
{
	return { x, y, x + w, y + h };
}

bool WindowManager::IsTitleBar(POINT clientPoint) const
{
	RECT clientRect = {};
	GetClientRect(m_hwnd, &clientRect);
	return clientPoint.y < kTitleBarHeight && clientPoint.x < clientRect.right - kButtonAreaWidth;
}

void WindowManager::GetRestoreSize(int& width, int& height) const
{
	width = m_restoreRect.right - m_restoreRect.left;
	height = m_restoreRect.bottom - m_restoreRect.top;
	if (width <= 0 || height <= 0)
	{
		width = 800;
		height = 600;
	}
}

int WindowManager::ClampRestoreX(int cursorX, int width, HMONITOR monitor) const
{
	MONITORINFO info = { sizeof(info) };
	GetMonitorInfoW(monitor, &info);
	int x = cursorX - width / 2;
	if (x < info.rcWork.left)
		x = info.rcWork.left;
	if (x + width > info.rcWork.right)
		x = info.rcWork.right - width;
	return x;
}

void WindowManager::RestoreWindow(int x, int y)
{
	if (!m_maximized && !m_animation.IsActive())
		return;
	int width = 0;
	int height = 0;
	GetRestoreSize(width, height);
	m_animation.Begin(m_hwnd, MakeRect(x, y, width, height), false, m_maximized);
}

void WindowManager::MaximizeWindow()
{
	if (m_maximized && !m_animation.IsActive())
		return;
	if (!m_maximized && !m_animation.IsActive())
		GetWindowRect(m_hwnd, &m_restoreRect);
	m_animation.Begin(m_hwnd, WorkArea(), true, m_maximized);
}

void WindowManager::RestoreFromMaxDrag(POINT cursor, int clickY)
{
	int width = 0;
	int height = 0;
	GetRestoreSize(width, height);
	const int x = ClampRestoreX(cursor.x, width, MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST));
	m_animation.Begin(m_hwnd, MakeRect(x, cursor.y - clickY, width, height), false, m_maximized);
}

void WindowManager::FinishSnapAnim()
{
	if (IsMouseHeld())
	{
		m_drag.pending = true;
		m_drag.restoreClickY = m_drag.clickY;
		GetCursorPos(&m_drag.pendingCursor);
		KeepDragCapture();
	}
	else
	{
		ReleaseCapture();
	}
	m_drag.snapAnim = false;
}

float WindowManager::AnimationProgress() const
{
	LARGE_INTEGER now = {};
	QueryPerformanceCounter(&now);
	return float(now.QuadPart - m_animation.StartQpc()) / float(m_qpcFrequency.QuadPart) / (kAnimationMs / 1000.f);
}

float WindowManager::EaseOut(float t)
{
	const float u = 1.f - t;
	return 1.f - u * u * u;
}

bool WindowManager::IsMouseHeld()
{
	return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

void WindowManager::KeepDragCapture() const
{
	if (IsMouseHeld() && GetCapture() != m_hwnd)
		SetCapture(m_hwnd);
}

LRESULT WindowManager::HitTest(HWND hwnd, LPARAM lParam) const
{
	if (m_animation.IsActive())
		return HTCLIENT;

	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	ScreenToClient(hwnd, &pt);
	RECT clientRect = {};
	GetClientRect(hwnd, &clientRect);
	const int x = pt.x;
	const int y = pt.y;
	const int right = clientRect.right;
	const int bottom = clientRect.bottom;

	if (y < kBorder)
	{
		if (m_maximized)
			return HTCLIENT;
		if (x < kBorder)
			return HTTOPLEFT;
		if (x > right - kBorder)
			return HTTOPRIGHT;
		return HTTOP;
	}
	if (y > bottom - kBorder)
		return x < kBorder ? HTBOTTOMLEFT : (x > right - kBorder ? HTBOTTOMRIGHT : HTBOTTOM);
	if (x < kBorder)
		return HTLEFT;
	if (x > right - kBorder)
		return HTRIGHT;
	return HTCLIENT;
}

void WindowManager::SetIcons() const
{
	const HINSTANCE instance = GetModuleHandleW(nullptr);
	SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
	SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
}

UINT WindowManager::QueryMonitorHz(POINT pt)
{
	HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFOEXW info = { sizeof(info) };
	if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info)))
		return 60;
	DEVMODEW mode = { sizeof(mode) };
	if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
		return mode.dmDisplayFrequency;
	return 60;
}

UINT WindowManager::QueryMonitorHzForWindow(HWND hwnd)
{
	if (!hwnd)
		return QueryMonitorHz({ 0, 0 });

	RECT rect = {};
	if (!GetWindowRect(hwnd, &rect))
		return QueryMonitorHz({ 0, 0 });

	const POINT center = {
		(rect.left + rect.right) / 2,
		(rect.top + rect.bottom) / 2,
	};
	return QueryMonitorHz(center);
}
