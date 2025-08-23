#include "Precompiled.h"
#include "Application.h"

int main(int argc, char* argv[])
{
	HINSTANCE hInstance = GetModuleHandle(nullptr);
	gglab::Application::CreateApplicationInstance(L"GraphicsGadgetLab", 1920, 1080, hInstance);
	gglab::Application::GetInstance()->Run();
	gglab::Application::DestroyApplicationInstance();

	return 0;
}
