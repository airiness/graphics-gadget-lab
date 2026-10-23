#pragma once
#include "Lab/LabCatalog.h"
#include "Lab/LabCommandQueue.h"
#include "Lab/LabInterfaces.h"
#include "Lab/LabSessionBase.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	class LabRuntime final : public ILabControl, public LabSnapshotSourceBase
	{
	public:
		explicit LabRuntime(const LabSessionCreateInfo& createInfo) noexcept;
		~LabRuntime();
		GGLAB_DELETE_COPYABLE_MOVABLE(LabRuntime);

		bool RegisterLab(LabDescriptor descriptor, LabSessionFactory factory) noexcept;
		// Startup assignments are applied before the first preparation. Any rejected
		// assignment fails initialization instead of being ignored or clamped.
		bool Initialize(const LabId& startupLab,
			std::span<const LabParameterAssignment> startupParameters = {}) noexcept;
		void Shutdown() noexcept;

		void OnEnter() noexcept;
		void OnExit() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;
		void Update() noexcept;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept;
		void OnCameraPathFrameApplied(const CameraPath& path, uint32_t frame) noexcept;

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
		// Session coordination owns the receipt; this source owns its queued mutation.
		[[nodiscard]] bool QueueParameterBatch(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error) noexcept;
		class PreparedParameterBatch
		{
		public:
			PreparedParameterBatch(PreparedParameterBatch&&) noexcept = default;
			PreparedParameterBatch& operator=(PreparedParameterBatch&&) noexcept = default;
			PreparedParameterBatch(const PreparedParameterBatch&) = delete;
			PreparedParameterBatch& operator=(const PreparedParameterBatch&) = delete;
			[[nodiscard]] bool HasChanges() const noexcept { return m_HasChanges; }
			[[nodiscard]] uint64_t GetRebuildGeneration() const noexcept { return m_RebuildGeneration; }
			[[nodiscard]] bool HasWorldLightingChanges() const noexcept
			{
				return m_WorldLightingMutation && m_WorldLightingMutation->HasChanges();
			}

		private:
			friend class LabRuntime;
			PreparedParameterBatch() = default;
			const LabRuntime* m_Owner = nullptr;
			LabSessionBase* m_Session = nullptr;
			LabParameterSet m_Parameters;
			std::optional<LabParameterId> m_LightingParameter;
			std::unique_ptr<PreparedWorldLightingMutationBase> m_WorldLightingMutation;
			LightingMutationResult m_WorldLightingEvidence;
			uint64_t m_Id = 0;
			uint64_t m_Revision = 0;
			uint64_t m_RebuildGeneration = 0;
			LabChangeImpact m_Impact = LabChangeImpact::Immediate;
			uint32_t m_SessionSerial = 0;
			bool m_HasChanges = false;
			bool m_Committed = false;
		};

		// Local to one safe boundary: preparation does not consume the command or
		// mutate the session. Discarding a plan leaves that reservation unchanged.
		// Commit installs values without invoking Lab callbacks.
		[[nodiscard]] std::optional<PreparedParameterBatch> PrepareParameterBatch(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error,
			WorldLightingControlBase* worldLighting = nullptr, uint64_t contentIdentity = 0) const noexcept;
		void CommitParameterBatch(PreparedParameterBatch& prepared) noexcept;
		struct LightingParameterEffect
		{
			LabParameterId m_Parameter;
			LightingMutationResult m_Result;
		};
		[[nodiscard]] std::optional<LightingParameterEffect> ApplyParameterBatchEffects(
			PreparedParameterBatch&& prepared) noexcept;
		[[nodiscard]] bool ApplyParameterBatch(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error) noexcept;
		void CancelParameterBatch(uint64_t id) noexcept;
		[[nodiscard]] bool SupportsParameterEffect(const LabParameterDesc& desc) const noexcept;
		enum class ParameterRebuildState : uint8_t
		{
			Preparing,
			Published,
			Failed,
			Superseded,
		};
		struct ParameterRebuild
		{
			// This generation identifies an owned operation, not a predicted activation serial.
			uint64_t m_Generation = 0;
			uint64_t m_BatchId = 0;
			uint32_t m_SourceSession = 0;
			uint32_t m_TargetSession = 0;
			ParameterRebuildState m_State = ParameterRebuildState::Preparing;
			LabParameterSet m_Parameters;
			std::string m_Failure;
		};
		[[nodiscard]] const ParameterRebuild* GetParameterRebuild() const noexcept
		{
			return m_ParameterRebuild ? &*m_ParameterRebuild : nullptr;
		}
		[[nodiscard]] const LabParameterSet* GetParameterSourceParameters() const noexcept;
		[[nodiscard]] uint64_t GetParameterRevision() const noexcept { return m_ParameterRevision; }
		void ProcessPendingCommands() noexcept;
		void TickTransitions() noexcept;
		LabSnapshot GetLabSnapshot(
			const LabDiagnosticsFrameContext& context = {}) const noexcept override;
		[[nodiscard]] std::optional<LoadingProgress> GetLoadingProgress() const noexcept;

		LabRunState GetState() const noexcept { return m_State; }
		bool IsInitialized() const noexcept
		{
			return m_ActiveSession && m_State != LabRunState::Uninitialized &&
				m_State != LabRunState::Failed;
		}
		bool IsReady() const noexcept { return m_State == LabRunState::Ready && m_ActiveSession; }
		bool HasPendingSession() const noexcept { return m_PendingSession != nullptr; }
		uint32_t GetWarmupFramesRemaining() const noexcept { return m_WarmupFramesRemaining; }
		uint32_t GetRetiringSessionCount() const noexcept
		{
			return static_cast<uint32_t>(m_RetiringSessions.size());
		}
		std::string_view GetLastError() const noexcept { return m_LastError; }
		const LabCatalog& GetCatalog() const noexcept { return m_Catalog; }
		LabSessionBase* GetActiveSession() noexcept { return m_ActiveSession.get(); }
		const LabSessionBase* GetActiveSession() const noexcept { return m_ActiveSession.get(); }
		LabSessionBase* GetPendingSession() noexcept { return m_PendingSession.get(); }
		const LabSessionBase* GetPendingSession() const noexcept { return m_PendingSession.get(); }
		uint32_t GetTemporalSessionSerial() const noexcept { return m_TemporalSessionSerial; }

		World& GetWorld() noexcept;
		Camera& GetCamera() noexcept;
		CameraController& GetCameraController() noexcept;
		CameraRig& GetCameraRig() noexcept;
		const ViewRenderProfile& GetViewRenderProfile() const noexcept;
		RenderPipelineBase& GetRenderPipeline() noexcept;

	private:
		[[nodiscard]] std::optional<PreparedParameterBatch> PrepareParameterValues(
			std::span<const LabParameterValue> values, std::string& error) const noexcept;
		bool BeginSessionTransition(const LabId& id,
			std::span<const LabParameterValue> values = {},
			std::span<const LabParameterAssignment> assignments = {}, uint64_t parameterRebuild = 0) noexcept;
		bool CommitPendingSession() noexcept;
		bool RestartActiveSessionWithValues(std::span<const LabParameterValue> values) noexcept;
		void PollRetiringSessions() noexcept;
		void RefreshInheritedParameters() noexcept;
		void SetError(std::string message) noexcept;
		static LabChangeImpact MaxImpact(LabChangeImpact lhs, LabChangeImpact rhs) noexcept;

		uint64_t m_ParameterRevision = 1;
		uint64_t m_NextParameterRebuild = 1;
		uint64_t m_PendingParameterRebuild = 0;
		std::optional<ParameterRebuild> m_ParameterRebuild;

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
		uint32_t m_TemporalSessionSerial = 0;
	};
}
