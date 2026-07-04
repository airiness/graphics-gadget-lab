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
	class DevelopGuiSystem;
	class PlatformHost;
	class Time;
	class Keyboard;
	class Mouse;
	class World;
	struct PlatformEvent;
	class Application
	{
	public:
		struct CreateInfo
		{
			std::wstring_view m_WindowName;
			uint32_t m_WindowWidth = 0;
			uint32_t m_WindowHeight = 0;
			std::unique_ptr<PlatformHost> m_PlatformHost;
		};

	public:
		explicit Application(CreateInfo createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Application);
		~Application();

		void Run() noexcept;

		uint32_t GetWindowWidth() const noexcept { return m_WindowWidth; }
		uint32_t GetWindowHeight() const noexcept { return m_WindowHeight; }

		Renderer* GetRenderer() const noexcept { return m_Renderer.get(); }
		AssetManager* GetAssetManager() const noexcept { return m_AssetManager.get(); }
		InputManager* GetInputManager() const noexcept { return m_InputManager.get(); }
		ShaderManager* GetShaderManager() const noexcept { return m_ShaderManager.get(); }

		Keyboard* GetKeyboard() const noexcept;
		Mouse* GetMouse() const noexcept;
		Time* GetTime() const noexcept { return m_Time.get(); }

		static void CreateApplicationInstance(CreateInfo createInfo) noexcept;
		static Application* GetInstance() noexcept;
		static void DestroyApplicationInstance() noexcept;

	private:
		void Initialize() noexcept;
		bool Tick() noexcept;
		void Finalize() noexcept;

		void InitializeAssets() noexcept;
		void HandlePlatformEvent(const PlatformEvent& event) noexcept;

		// Platform lifecycle handlers
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
		std::unique_ptr<PlatformHost> m_PlatformHost;
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<Time> m_Time;
		std::unique_ptr<AssetManager> m_AssetManager;
		std::unique_ptr<InputManager> m_InputManager;
		std::unique_ptr<ShaderManager> m_ShaderManager;
		std::unique_ptr<DemoManager> m_DemoManager;
		std::unique_ptr<RenderFrameBuilder> m_RenderFrameBuilder;
		std::unique_ptr<DevelopGuiSystem> m_DevelopGuiSystem;

		bool m_IsInitialized = false;
		bool m_IsSuspended = false;
	};
}
