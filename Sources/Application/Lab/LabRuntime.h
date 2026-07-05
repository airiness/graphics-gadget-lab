#pragma once
#include "Application/Lab/LabCatalog.h"
#include "Application/Lab/LabCommandQueue.h"
#include "Application/Lab/LabInterfaces.h"
#include "Application/Lab/LabSession.h"

namespace gglab
{
	class LabRuntime final : public ILabControl, public ILabSnapshotSource
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

		void RequestSwitchLab(const LabId& id) noexcept override { m_CommandQueue.RequestSwitch(id); }
		void RequestSetParameter(
			const LabParameterId& id,
			const LabValue& value) noexcept override
		{
			m_CommandQueue.RequestSetParameter(id, value);
		}
		void RequestResetParameters() noexcept override { m_CommandQueue.RequestResetParameters(); }
		void RequestRebuildScene() noexcept override { m_CommandQueue.RequestRebuildScene(); }
		void RequestRestartSession() noexcept override { m_CommandQueue.RequestRestart(); }
		void ProcessPendingCommands() noexcept;
		LabSnapshot GetLabSnapshot() const noexcept override;

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
		bool RestartActiveSessionWithValues(
			std::span<const LabParameterValue> values) noexcept;
		void WaitForGpuIdle() noexcept;
		void SetError(std::string message) noexcept;
		static LabChangeImpact MaxImpact(LabChangeImpact lhs, LabChangeImpact rhs) noexcept;

		LabSessionCreateInfo m_CreateInfo{};
		LabCatalog m_Catalog;
		LabCommandQueue m_CommandQueue;
		std::unique_ptr<LabSession> m_ActiveSession;
		std::string m_LastError;
		LabRuntimeState m_State = LabRuntimeState::Uninitialized;
		uint64_t m_FrameInSession = 0;
		bool m_IsEntered = false;
	};
}
