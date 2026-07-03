#pragma once

namespace gglab
{
	class Renderer;
	class AssetManager;
	class TextureRegistry;
	class InputManager;
	class ShaderManager;
	class DemoManager;
	class RenderFrameBuilder;
	class DevToolsRuntime;
	class Time;
	class Keyboard;
	class Mouse;
	class World;
	class Application
	{
	public:
		struct CreateInfo
		{
			std::wstring_view m_WindowName;
			uint32_t m_WindowWidth = 0;
			uint32_t m_WindowHeight = 0;
			HINSTANCE m_HInstance = nullptr;
		};

	public:
		explicit Application(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Application);
		~Application() = default;

		void Run() noexcept;

		uint32_t GetWindowWidth() const noexcept { return m_WindowWidth; }
		uint32_t GetWindowHeight() const noexcept { return m_WindowHeight; }

		HWND GetHwnd() const noexcept { return m_Hwnd; };

		Renderer* GetRenderer() const noexcept { return m_Renderer.get(); }
		AssetManager* GetAssetManager() const noexcept { return m_AssetManager.get(); }
		InputManager* GetInputManager() const noexcept { return m_InputManager.get(); }
		ShaderManager* GetShaderManager() const noexcept { return m_ShaderManager.get(); }

		Keyboard* GetKeyboard() const noexcept;
		Mouse* GetMouse() const noexcept;
		Time* GetTime() const noexcept { return m_Time.get(); }

		static void CreateApplicationInstance(const CreateInfo& createInfo) noexcept;
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
		void InitializeDevToolsPanels() noexcept;

		// Functions called by the WindowProc
		void OnActive() noexcept;
		void OnInactive() noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;

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
		std::unique_ptr<DemoManager> m_DemoManager;
		std::unique_ptr<RenderFrameBuilder> m_RenderFrameBuilder;
		std::unique_ptr<DevToolsRuntime> m_DevToolsRuntime;

		bool m_IsInitialized = false;
		bool m_IsMinimized = false;
		bool m_InSizeMove = false;
		bool m_IsSuspended = false;
	};
}
