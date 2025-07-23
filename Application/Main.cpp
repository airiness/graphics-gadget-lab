#include "Precompiled.h"
#include "Application.h"

#include <spdlog/spdlog.h>

int main(int argc, char* argv[])
{
	spdlog::error("Hello error: {}-{}", "oho", 1);


	HINSTANCE hInstance = GetModuleHandle(nullptr);
	graphicsGadgetLab::Application::CreateApplicationInstance(L"GraphicsGadgetLab", 1920, 1080, hInstance);
	graphicsGadgetLab::Application::GetInstance()->Run();
	graphicsGadgetLab::Application::DestroyApplicationInstance();

	return 0;
}
