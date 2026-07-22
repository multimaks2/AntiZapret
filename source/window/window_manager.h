#pragma once

#include <Windows.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class ThemeManager;

struct TrayMenuState
{
	bool azRunning = false;
	bool azBusy = false;
	bool azProbeRunning = false;
	int azActiveStrategy = -1;
	std::vector<std::pair<int, std::wstring>> strategies; // strategyIndex, label

	bool tgRunning = false;
	bool tgCanAction = false;

	bool vpnActive = false;
	bool vpnCanStart = false;
	int vpnWorkMode = 1; // 1..4
	int vpnTransportMode = 0; // 0 = Proxy, 1 = Tunnel
	int vpnActiveServer = -1;
	std::vector<std::pair<int, std::wstring>> vpnServers; // nodeIndex, label
};

enum class TrayCommand
{
	Open,
	Exit,
	AzStart,
	AzStop,
	AzProbe,
	AzStopProbe,
	AzStartStrategy, // param = strategyIndex
	TgToggle,
	TgCopyLink,
	TgOpenTelegram,
	VpnToggle,
	VpnSetWorkMode, // param = workMode 1..4
	VpnSetTransportMode, // param = 0/1
	VpnSelectServer, // param = nodeIndex
};

class WindowManager
{
public:
	using RenderCallback = std::function<void()>;
	using ResizeCallback = std::function<void(UINT, UINT)>;
	using TrayMenuStateCallback = std::function<TrayMenuState()>;
	using TrayCommandCallback = std::function<void(TrayCommand, int)>;

	bool Create(float dpiScale, int minWidth, int minHeight);
	void Destroy();
	void SetRenderCallback(RenderCallback callback);
	void SetResizeCallback(ResizeCallback callback);
	void SetInputHandler(const std::function<bool(UINT, WPARAM, LPARAM)>& handler);
	void SetTrayCallbacks(TrayMenuStateCallback stateCallback, TrayCommandCallback commandCallback);

	HWND Handle() const { return m_hwnd; }
	int MinWidth() const { return m_minWidth; }
	int MinHeight() const { return m_minHeight; }
	bool IsWindowMaximized() const { return m_maximized; }
	bool IsActive() const { return m_active; }
	bool IsMinimized() const { return m_minimized; }
	// True while the main window is hidden via close-to-tray (render loop may pause).
	bool IsInTray() const { return m_hiddenToTray; }
	bool IsReady() const { return m_ready; }
	void SetReady(bool ready) { m_ready = ready; }

	void HideToTray();
	void RestoreFromTray();
	void RequestQuit();

	void ToggleMaximize();
	void UpdateAnimations();
	void EnsureMinimumSize();

	static UINT QueryMonitorHz(POINT pt);
	static UINT QueryMonitorHzForWindow(HWND hwnd);

	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static constexpr UINT kTrayCallbackMessage = WM_APP + 1;
	static constexpr UINT kRestoreFromTrayMessage = WM_APP + 2;

private:
	class WindowAnimation
	{
	public:
		void Begin(HWND hwnd, const RECT& target, bool toMaximized, bool& maximizedFlag);
		void Update(HWND hwnd, float progress, bool& active);
		RECT Lerp(float t) const;
		bool IsActive() const { return m_active; }
		void SetActive(bool active) { m_active = active; }
		float FromWidth() const { return m_fromRight - m_fromLeft; }
		float FromHeight() const { return m_fromBottom - m_fromTop; }
		LONGLONG StartQpc() const { return m_startQpc; }

	private:
		bool m_active = false;
		float m_fromLeft = 0.f;
		float m_fromTop = 0.f;
		float m_fromRight = 0.f;
		float m_fromBottom = 0.f;
		float m_toLeft = 0.f;
		float m_toTop = 0.f;
		float m_toRight = 0.f;
		float m_toBottom = 0.f;
		LONGLONG m_startQpc = 0;
	};

	class DragState
	{
	public:
		void Clear();
		void Release(HWND hwnd);
		bool HandleTitleMouse(HWND hwnd, UINT msg, LPARAM lParam, WindowManager& window);

		bool drag = false;
		bool pending = false;
		bool restoreAnim = false;
		bool snapAnim = false;
		int clickY = 0;
		int restoreClickY = 0;
		POINT cursor = {};
		POINT windowOrigin = {};
		POINT pendingCursor = {};
	};

	friend class DragState;

	void SetRounded(bool rounded) const;
	void ApplyRect(const RECT& rect) const;
	void ApplyRect(float x, float y, float w, float h) const;
	RECT WorkArea() const;
	RECT MakeRect(int x, int y, int w, int h) const;
	bool IsTitleBar(POINT clientPoint) const;
	void GetRestoreSize(int& width, int& height) const;
	int ClampRestoreX(int cursorX, int width, HMONITOR monitor) const;
	void RestoreWindow(int x, int y);
	void MaximizeWindow();
	void RestoreFromMaxDrag(POINT cursor, int clickY);
	void FinishSnapAnim();
	float AnimationProgress() const;
	static float EaseOut(float t);
	static bool IsMouseHeld();
	void KeepDragCapture() const;
	LRESULT HitTest(HWND hwnd, LPARAM lParam) const;
	void SetIcons() const;
	void AddTrayIcon();
	void RemoveTrayIcon();
	void ShowTrayMenu();
	void ShowMainWindow();
	bool ResolveTrayCommand(UINT cmd, TrayCommand& outCommand, int& outParam) const;

	HWND m_hwnd = nullptr;
	RenderCallback m_renderCallback;
	ResizeCallback m_resizeCallback;
	std::function<bool(UINT, WPARAM, LPARAM)> m_inputHandler;
	TrayMenuStateCallback m_trayMenuStateCallback;
	TrayCommandCallback m_trayCommandCallback;
	TrayMenuState m_lastTrayMenuState {};

	int m_minWidth = 640;
	int m_minHeight = 480;
	UINT m_monitorHz = 60;
	bool m_ready = false;
	bool m_active = true;
	bool m_minimized = false;
	bool m_trayIconVisible = false;
	bool m_hiddenToTray = false;
	bool m_forceQuit = false;
	bool m_occluded = false;
	bool m_inSizeMove = false;
	bool m_maximized = false;
	RECT m_restoreRect = {};
	LARGE_INTEGER m_qpcFrequency = {};
	WindowAnimation m_animation;
	DragState m_drag;

	static WindowManager* FromHandle(HWND hwnd);
};
