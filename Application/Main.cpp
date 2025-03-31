#include "Precompiled.h"
#include "Application.h"

int main(int argc, char* argv[])
{
	HINSTANCE hInstance = GetModuleHandle(nullptr);
	auto app = std::make_unique<graphicsGadgetLab::Application>(L"GraphicsGadgetLab", 1280, 720, hInstance);

	app->Initialize();
	app->Update();
	app->Finalize();

	return 0;
}
