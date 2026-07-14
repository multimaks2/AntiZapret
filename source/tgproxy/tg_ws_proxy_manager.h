#pragma once



#include <Windows.h>



#include <atomic>

#include <mutex>

#include <string>

#include <vector>



class AppSettings;



enum class TgProxyEnvState

{

	Checking,

	Ready,

	MissingFiles,

	MissingPython,

	MissingDependencies,

	SettingUp

};



class TgWsProxyManager

{

public:

	static constexpr const char* kPythonManagerStoreUrl = "ms-windows-store://pdp/?ProductId=9NQ7512CXL7T";
	static constexpr const char* kPythonManagerUrl = "https://www.python.org/ftp/python/pymanager/python-manager-26.1.msix";



	TgWsProxyManager();

	~TgWsProxyManager();



	void SetSettings(AppSettings* settings);



	void Update(float deltaTime);



	bool Start(bool openTelegram);

	void RequestStart(bool openTelegram);

	void Stop();

	void RequestStop();

	bool IsOperationInFlight() const { return m_asyncOpRunning.load(); }

	bool ProbeTelegramConnectivity() const;



	bool IsRunning() const;

	bool IsStartedByUs() const { return m_startedByUs; }

	TgProxyEnvState GetEnvState() const { return m_envState.load(); }

	const std::string& GetStatusMessage() const;

	const std::string& GetErrorMessage() const { return m_lastError; }



	const std::string& GetTelegramLinkCached() const;

	bool CopyTelegramLinkToClipboard() const;

	bool OpenTelegramLink() const;



	const char* GetPrimaryActionLabel() const;

	bool CanPrimaryAction() const;

	void HandlePrimaryAction(bool openTelegramAfterStart);



private:

	void RefreshEnvironmentStateQuick();

	void RefreshEnvironmentState();

	void BeginEnvironmentProbe();

	bool LaunchIfReady(bool openTelegram);

	bool LaunchProxyProcess(const std::string& secret);

	bool EnsureDependencies(std::string& outError);

	bool DetectPythonLauncher(std::string& outLauncher);

	bool CommandSucceeded(const std::string& commandLine, const std::wstring& workDir, DWORD timeoutMs);

	void KillProcessTree(DWORD rootPid);

	void SetStatusMessage(const std::string& message);

	bool CanRunProxyModule(const std::string& launcher) const;

	bool OpenPythonInstaller(std::string& outMessage);

	void RunSetupAndStart(bool openTelegram);

	void RefreshRunningState();

	void TryFlushPendingStart();

	bool WaitForProxyRunning(int timeoutMs);

	DWORD FindListenerPidForProxy() const;

	void ClearProcessHandleLocked();

	void RefreshTelegramLinkCache() const;



	HANDLE m_process = nullptr;

	mutable std::mutex m_processMutex;

	mutable std::mutex m_messageMutex;

	std::mutex m_opMutex;



	AppSettings* m_settings = nullptr;

	std::atomic<TgProxyEnvState> m_envState { TgProxyEnvState::Checking };

	std::atomic<bool> m_setupRunning { false };

	std::atomic<bool> m_envProbeRunning { false };

	std::atomic<bool> m_asyncOpRunning { false };

	std::atomic<bool> m_cachedRunning { false };

	std::atomic<bool> m_pendingStart { false };

	std::atomic<bool> m_pendingOpenTelegram { false };

	std::string m_statusMessage;

	std::string m_lastError;

	std::string m_pythonLauncher;

	float m_envRefreshTimer = 0.f;

	float m_statusPollTimer = 0.f;

	bool m_startedByUs = false;

	bool m_envProbeStarted = false;



	mutable std::vector<BYTE> m_tcpTableBuffer;

	mutable std::string m_cachedTelegramLink;

	mutable std::string m_cachedLinkHost;

	mutable int m_cachedLinkPort = -1;

	mutable std::string m_cachedLinkSecret;

};


