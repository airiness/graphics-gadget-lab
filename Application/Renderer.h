#pragma once
namespace graphicsGadgetLab
{
	class Renderer
	{
	public:
		explicit Renderer() noexcept;
		~Renderer() noexcept;

		void Initialize(HWND hWnd) noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

	private:


	};
}
