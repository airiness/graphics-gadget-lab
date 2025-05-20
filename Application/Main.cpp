#include "Precompiled.h"
#include "Application.h"

int main(int argc, char* argv[])
{
	HINSTANCE hInstance = GetModuleHandle(nullptr);
	graphicsGadgetLab::Application::CreateApplicationInstance(L"GraphicsGadgetLab", 1280, 720, hInstance);
	auto* app = graphicsGadgetLab::Application::Get();

	app->Initialize();
	app->Update();
	app->Finalize();

	return 0;
}
