#pragma once

class Application
{
public:
	int Run();

private:
	bool Initialize();
	void Shutdown();
	void UpdateFrame();
	void RenderFrame();

	class Components;
	Components* m_components = nullptr;
};

int RunApplication();
