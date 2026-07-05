#pragma once
#include "Application/Lab/LabCatalog.h"
#include "Application/Lab/LabCommandQueue.h"
#include "Application/Lab/LabSession.h"

namespace gglab
{
	enum class LabRuntimeState : uint8_t
	{
		Uninitialized,
		Ready,
		Failed,
	};

	class LabRuntime
	{
	public:
		explicit LabRuntime(const LabSessionCreateInfo& createInfo) noexcept;
		~LabRuntime();
		GGLAB_DELETE_COPYABLE_MOVABLE(LabRuntime);

		bool RegisterLab(LabDescriptor descriptor, LabSessionFactory factory) noexcept;
		bool Initialize(const LabId& startupLab) noexcept;
		void Shutdown() noexcept;

		void OnEnter() noexcept;
		void OnExit() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;
		void Update() noexcept;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept;

		void RequestSwitch(const LabId& id) noexcept { m_CommandQueue.RequestSwitch(id); }
		void RequestRestart() noexcept { m_CommandQueue.RequestRestart(); }
		void ProcessPendingCommands() noexcept;

		LabRuntimeState GetState() const noexcept { return m_State; }
		bool IsReady() const noexcept { return m_State == LabRuntimeState::Ready && m_ActiveSession; }
		std::string_view GetLastError() const noexcept { return m_LastError; }
		const LabCatalog& GetCatalog() const noexcept { return m_Catalog; }
		const LabSession* GetActiveSession() const noexcept { return m_ActiveSession.get(); }

		World& GetWorld() noexcept;
		Camera& GetCamera() noexcept;
		CameraController& GetCameraController() noexcept;
		RenderPipelineBase& GetRenderPipeline() noexcept;

	private:
		bool ReplaceActiveSession(const LabId& id, bool waitForGpu) noexcept;
		void SetError(std::string message) noexcept;

		LabSessionCreateInfo m_CreateInfo{};
		LabCatalog m_Catalog;
		LabCommandQueue m_CommandQueue;
		std::unique_ptr<LabSession> m_ActiveSession;
		std::string m_LastError;
		LabRuntimeState m_State = LabRuntimeState::Uninitialized;
		bool m_IsEntered = false;
	};
}
