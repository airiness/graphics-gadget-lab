#pragma once
#include "Application/Lab/LabCatalog.h"
#include "Application/Lab/LabCommandQueue.h"
#include "Application/Lab/LabInterfaces.h"
#include "Application/Lab/LabSessionBase.h"
#include "Core/CoreMacros.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"

namespace gglab
{
	class LabRuntime final : public ILabControl, public LabSnapshotSourceBase
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

		void RequestSwitchLab(const LabId& id) noexcept override
		{
			m_CommandQueue.RequestSwitch(id);
		}
		void RequestSetParameter(const LabParameterId& id, const LabValue& value) noexcept override
		{
			m_CommandQueue.RequestSetParameter(id, value);
		}
		void RequestResetParameters() noexcept override { m_CommandQueue.RequestResetParameters(); }
		void RequestRebuildScene() noexcept override { m_CommandQueue.RequestRebuildScene(); }
		void RequestRestartSession() noexcept override { m_CommandQueue.RequestRestart(); }
		void RequestRunConfig(const LabRunConfig& config) noexcept override
		{
			m_CommandQueue.RequestRunConfig(config);
		}
		void ProcessPendingCommands() noexcept;
		void TickTransitions() noexcept;
		LabSnapshot GetLabSnapshot() const noexcept override;
		[[nodiscard]] std::optional<LoadingProgress> GetLoadingProgress() const noexcept;

		LabRunState GetState() const noexcept { return m_State; }
		bool IsInitialized() const noexcept
		{
			return m_ActiveSession && m_State != LabRunState::Uninitialized &&
				m_State != LabRunState::Failed;
		}
		bool IsReady() const noexcept { return m_State == LabRunState::Ready && m_ActiveSession; }
		bool HasPendingSession() const noexcept { return m_PendingSession != nullptr; }
		uint32_t GetRetiringSessionCount() const noexcept
		{
			return static_cast<uint32_t>(m_RetiringSessions.size());
		}
		std::string_view GetLastError() const noexcept { return m_LastError; }
		const LabCatalog& GetCatalog() const noexcept { return m_Catalog; }
		const LabSessionBase* GetActiveSession() const noexcept { return m_ActiveSession.get(); }

		World& GetWorld() noexcept;
		Camera& GetCamera() noexcept;
		CameraController& GetCameraController() noexcept;
		CameraRig& GetCameraRig() noexcept;
		const ViewRenderProfile& GetViewRenderProfile() const noexcept;
		RenderPipelineBase& GetRenderPipeline() noexcept;

	private:
		bool BeginSessionTransition(
			const LabId& id, std::span<const LabParameterValue> values = {}) noexcept;
		bool CommitPendingSession() noexcept;
		bool RestartActiveSessionWithValues(std::span<const LabParameterValue> values) noexcept;
		void PollRetiringSessions() noexcept;
		void SetError(std::string message) noexcept;
		static LabChangeImpact MaxImpact(LabChangeImpact lhs, LabChangeImpact rhs) noexcept;

		LabSessionCreateInfo m_CreateInfo{};
		LabCatalog m_Catalog;
		LabCommandQueue m_CommandQueue;
		std::unique_ptr<LabSessionBase> m_ActiveSession;
		std::unique_ptr<LabSessionBase> m_PendingSession;
		struct RetiringSession
		{
			std::unique_ptr<LabSessionBase> m_Instance;
			RHIFencePoint m_RetireFence{};
		};
		std::vector<RetiringSession> m_RetiringSessions;
		std::string m_LastError;
		LabRunState m_State = LabRunState::Uninitialized;
		LabRunState m_StateBeforeTransition = LabRunState::Uninitialized;
		uint64_t m_FrameInSession = 0;
		uint32_t m_WarmupFramesRemaining = 0;
		float m_EffectiveDeltaTime = 0.0f;
		DemoFrameFeedback m_LastFrameFeedback{};
		bool m_HasFrameFeedback = false;
		bool m_IsEntered = false;
	};
}
