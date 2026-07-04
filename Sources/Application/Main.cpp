#include "Core/Precompiled.h"
#include "Application/Application.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"

int main(int argc, char* argv[])
{
	HINSTANCE hInstance = GetModuleHandle(nullptr);

	gglab::Application::CreateInfo createInfo{};
	createInfo.m_WindowName = L"GraphicsGadgetLab";
	createInfo.m_WindowWidth = 1920;
	createInfo.m_WindowHeight = 1080;
	createInfo.m_PlatformHost = std::make_unique<gglab::Win32PlatformHost>(hInstance);

	gglab::Application::CreateApplicationInstance(std::move(createInfo));
	gglab::Application::GetInstance()->Run();
	gglab::Application::DestroyApplicationInstance();

	return 0;
}
