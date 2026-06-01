#include "Core/Precompiled.h"
#include "Core/Application.h"

int main(int argc, char* argv[])
{
	HINSTANCE hInstance = GetModuleHandle(nullptr);

	gglab::Application::CreateInfo createInfo{};
	createInfo.m_WindowName = L"GraphicsGadgetLab";
	createInfo.m_WindowWidth = 1920;
	createInfo.m_WindowHeight = 1080;
	createInfo.m_HInstance = hInstance;

	gglab::Application::CreateApplicationInstance(createInfo);
	gglab::Application::GetInstance()->Run();
	gglab::Application::DestroyApplicationInstance();

	return 0;
}