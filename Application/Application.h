#pragma once
namespace graphicsGadgetLab
{
	class Application
	{
	public:
		void Initialize();
		void Update();
		void Finalize();
	private:
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


	private:
		void InitializeWindow();
	};
}


