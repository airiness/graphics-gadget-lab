#pragma once

namespace gglab
{
	class Renderer;
	class AssetManager;
	class InputManager;
	class ShaderManager;
	class TransferManager;
	class DemoManager;
	class RenderViewBuilder;
	class RenderSceneBuilder;
	class Time;
	class Keyboard;
	class Mouse;
	class World;
	class Application
	{
	public:
		explicit Application(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Application);
		~Application() noexcept = default;
		void Run() noexcept;

		uint32_t GetWindowWidth() const noexcept { return m_WindowWidth; }
		uint32_t GetWindowHeight() const noexcept { return m_WindowHeight; }

		HWND GetHwnd() const noexcept { return m_Hwnd; };

		Renderer* GetRenderer() const noexcept { return m_Renderer.get(); }
		AssetManager* GetAssetManager() const noexcept { return m_AssetManager.get(); }
		InputManager* GetInputManager() const noexcept { return m_InputManager.get(); }
		ShaderManager* GetShaderManager() const noexcept { return m_ShaderManager.get(); }
		TransferManager* GetTransferManager() const noexcept { return m_TransferManager.get(); }
		Keyboard* GetKeyboard() const noexcept;
		Mouse* GetMouse() const noexcept;
		Time* GetTime() const noexcept { return m_Time.get(); }

		static void CreateApplicationInstance(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, HINSTANCE hInstance) noexcept;
		static Application* GetInstance() noexcept;
		static void DestroyApplicationInstance() noexcept;

	private:
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	private:
		void Initialize() noexcept;
		bool Tick() noexcept;
		void Finalize() noexcept;

		void InitializeWindow() noexcept;
		void InitializeAssets() noexcept;

		// Functions called by the WindowProc
		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;
		void OnResize(int32_t width, int32_t height) noexcept;

	private:
		static std::unique_ptr<Application> s_Application;

	private:
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;

		std::wstring m_WindowName;

		HWND m_Hwnd = nullptr;
		HINSTANCE m_HInstance = nullptr;

		// Renderer
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<Time> m_Time;
		std::unique_ptr<AssetManager> m_AssetManager;
		std::unique_ptr<InputManager> m_InputManager;
		std::unique_ptr<ShaderManager> m_ShaderManager;
		std::unique_ptr<TransferManager> m_TransferManager;
		std::unique_ptr<DemoManager> m_DemoManager;
		std::unique_ptr<RenderViewBuilder> m_RenderViewBuilder;
		std::unique_ptr<RenderSceneBuilder> m_RenderSceneBuilder;

		bool m_IsInitialized = false;
	};
}


