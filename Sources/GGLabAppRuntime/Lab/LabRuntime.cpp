#include "Lab/LabRuntime.h"
#include "AppRuntimeLog.h"
#include "GGLabRuntime/Core/Time.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	namespace
	{
		LabIdSnapshot MakeLabIdSnapshot(const LabId& id)
		{
			return { .m_Name = id.m_Name };
		}

		LabParameterIdSnapshot MakeLabParameterIdSnapshot(const LabParameterId& id)
		{
			return { .m_Name = id.m_Name };
		}

		LabSnapshotRunState ToSnapshotRunState(LabRunState state) noexcept
		{
			switch (state)
			{
			case LabRunState::Uninitialized: return LabSnapshotRunState::Uninitialized;
			case LabRunState::Loading: return LabSnapshotRunState::Loading;
			case LabRunState::WarmingUp: return LabSnapshotRunState::WarmingUp;
			case LabRunState::Ready: return LabSnapshotRunState::Ready;
			case LabRunState::Capturing: return LabSnapshotRunState::Capturing;
			case LabRunState::Completed: return LabSnapshotRunState::Completed;
			case LabRunState::Failed: return LabSnapshotRunState::Failed;
			}
			return LabSnapshotRunState::Failed;
		}

		LabSnapshotParameterType ToSnapshotParameterType(LabParameterType type) noexcept
		{
			switch (type)
			{
			case LabParameterType::Bool: return LabSnapshotParameterType::Bool;
			case LabParameterType::Int: return LabSnapshotParameterType::Int;
			case LabParameterType::UInt: return LabSnapshotParameterType::UInt;
			case LabParameterType::Float: return LabSnapshotParameterType::Float;
			case LabParameterType::Enum: return LabSnapshotParameterType::Enum;
			case LabParameterType::Vector3: return LabSnapshotParameterType::Vector3;
			case LabParameterType::Color: return LabSnapshotParameterType::Color;
			}
			return LabSnapshotParameterType::Bool;
		}

		LabDescriptorSnapshot MakeDescriptorSnapshot(const LabDescriptor& descriptor)
		{
			return {
				.m_Id = MakeLabIdSnapshot(descriptor.m_Id),
				.m_DisplayName = descriptor.m_DisplayName,
				.m_Category = descriptor.m_Category,
				.m_Description = descriptor.m_Description,
				.m_SchemaVersion = descriptor.m_SchemaVersion,
			};
		}

		LabParameterDescSnapshot MakeParameterDescSnapshot(const LabParameterDesc& desc)
		{
			LabParameterDescSnapshot snapshot{
				.m_Id = MakeLabParameterIdSnapshot(desc.m_Id),
				.m_Name = desc.m_Name,
				.m_Group = desc.m_Group,
				.m_Type = ToSnapshotParameterType(desc.m_Type),
				.m_EditPolicy = desc.m_EditPolicy == LabParameterEditPolicy::CommitOnEditEnd
					? LabSnapshotParameterEditPolicy::CommitOnEditEnd
					: LabSnapshotParameterEditPolicy::Continuous,
				.m_DefaultValue = desc.m_DefaultValue,
				.m_MinValue = desc.m_MinValue,
				.m_MaxValue = desc.m_MaxValue,
			};
			snapshot.m_EnumItems.reserve(desc.m_EnumItems.size());
			for (const LabEnumItem& item : desc.m_EnumItems)
			{
				snapshot.m_EnumItems.push_back({
					.m_Value = item.m_Value,
					.m_Name = item.m_Name,
					});
			}
			return snapshot;
		}
	}

	LabRuntime::LabRuntime(const LabSessionCreateInfo& createInfo) noexcept :
		m_CreateInfo(createInfo)
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "LabRuntime requires valid create info.");
		m_CreateInfo.m_RunConfig.Sanitize();
	}

	LabRuntime::~LabRuntime()
	{
		Shutdown();
	}

	bool LabRuntime::RegisterLab(LabDescriptor descriptor, LabSessionFactory factory) noexcept
	{
		if (m_State != LabRunState::Uninitialized)
		{
			GGLAB_LOG_ERROR("Labs must be registered before LabRuntime initialization.");
			return false;
		}
		return m_Catalog.Register(std::move(descriptor), factory);
	}

	bool LabRuntime::Initialize(const LabId& startupLab) noexcept
	{
		if (m_State != LabRunState::Uninitialized)
		{
			return IsReady();
		}

		if (!m_CreateInfo.IsValid())
		{
			SetError("LabRuntime create info is invalid.");
			return false;
		}

		return BeginSessionTransition(startupLab);
	}

	void LabRuntime::Shutdown() noexcept
	{
		if (m_IsEntered && m_ActiveSession)
		{
			m_ActiveSession->OnExit();
		}
		m_IsEntered = false;
		if (m_PendingSession)
		{
			m_PendingSession->CancelPrepare();
		}
		m_PendingSession.reset();
		m_ActiveSession.reset();
		m_RetiringSessions.clear();
		m_State = LabRunState::Uninitialized;
		m_FrameInSession = 0;
		m_WarmupFramesRemaining = 0;
		m_EffectiveDeltaTime = 0.0f;
		m_LastFrameFeedback = {};
		m_HasFrameFeedback = false;
	}

	void LabRuntime::OnEnter() noexcept
	{
		if (!m_IsEntered && m_ActiveSession)
		{
			m_IsEntered = true;
			m_ActiveSession->OnEnter();
		}
	}

	void LabRuntime::OnExit() noexcept
	{
		if (m_IsEntered && m_ActiveSession)
		{
			m_ActiveSession->OnExit();
			m_IsEntered = false;
		}
	}

	void LabRuntime::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (width == 0 || height == 0)
		{
			return;
		}

		m_CreateInfo.m_WindowWidth = width;
		m_CreateInfo.m_WindowHeight = height;
		if (m_ActiveSession)
		{
			m_ActiveSession->OnResize(width, height);
		}
		if (m_PendingSession)
		{
			m_PendingSession->OnResize(width, height);
		}
	}

	void LabRuntime::Update() noexcept
	{
		GGLAB_ASSERT_MSG(m_ActiveSession, "LabRuntime requires an active session before update.");
		if (m_ActiveSession)
		{
			const LabRunConfig& config = m_CreateInfo.m_RunConfig;
			m_EffectiveDeltaTime =
				config.m_UseFixedDeltaTime
				? config.m_FixedDeltaTime
				: static_cast<float>(m_CreateInfo.m_Services.m_Time->GetDeltaTime());
			m_ActiveSession->Update(m_EffectiveDeltaTime);
		}
	}

	void LabRuntime::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
	{
		m_LastFrameFeedback = feedback;
		m_HasFrameFeedback = true;
		if (m_ActiveSession)
		{
			m_ActiveSession->OnFrameSubmitted(feedback);
			++m_FrameInSession;
		}

		if (m_State == LabRunState::WarmingUp &&
			feedback.m_RenderSceneStatus == RenderSceneBuildStatus::Ready &&
			feedback.m_SubmittedFence.IsValid())
		{
			if (m_WarmupFramesRemaining > 0)
			{
				--m_WarmupFramesRemaining;
			}
			if (m_WarmupFramesRemaining == 0)
			{
				m_State = LabRunState::Ready;
			}
		}
	}

	void LabRuntime::ProcessPendingCommands() noexcept
	{
		const LabCommandBatch commands = m_CommandQueue.Consume();
		if (commands.m_SwitchTarget)
		{
			GGLAB_UNUSED(BeginSessionTransition(*commands.m_SwitchTarget));
			TickTransitions();
			return;
		}

		if (commands.m_RunConfig && m_ActiveSession)
		{
			m_CreateInfo.m_RunConfig = *commands.m_RunConfig;
			m_CreateInfo.m_RunConfig.Sanitize();
			const std::vector<LabParameterValue> values =
				m_ActiveSession->GetParameters().CaptureValues();
			GGLAB_UNUSED(RestartActiveSessionWithValues(values));
			TickTransitions();
			return;
		}

		if (commands.m_RestartRequested && m_ActiveSession)
		{
			const std::vector<LabParameterValue> values =
				m_ActiveSession->GetParameters().CaptureValues();
			GGLAB_UNUSED(RestartActiveSessionWithValues(values));
			TickTransitions();
			return;
		}

		if (!m_ActiveSession)
		{
			TickTransitions();
			return;
		}

		LabChangeImpact impact = LabChangeImpact::Immediate;
		bool parametersChanged = false;
		if (commands.m_ResetParametersRequested)
		{
			impact = m_ActiveSession->ResetParameters();
			parametersChanged = true;
		}

		for (const LabParameterValue& change : commands.m_ParameterChanges)
		{
			LabChangeImpact changeImpact = LabChangeImpact::Immediate;
			if (m_ActiveSession->SetParameter(change.m_Id, change.m_Value, &changeImpact))
			{
				impact = MaxImpact(impact, changeImpact);
				parametersChanged = true;
			}
			else
			{
				GGLAB_LOG_WARN("Lab parameter '{}' was rejected.", change.m_Id.GetName());
			}
		}

		if (commands.m_RebuildSceneRequested)
		{
			impact = MaxImpact(impact, LabChangeImpact::RebuildScene);
			parametersChanged = true;
		}

		if (!parametersChanged)
		{
			TickTransitions();
			return;
		}

		if (impact != LabChangeImpact::Immediate)
		{
			const std::vector<LabParameterValue> values =
				m_ActiveSession->GetParameters().CaptureValues();
			GGLAB_UNUSED(RestartActiveSessionWithValues(values));
		}
		else
		{
			m_ActiveSession->ApplyParameterChanges(impact);
		}
		TickTransitions();
	}

	void LabRuntime::TickTransitions() noexcept
	{
		PollRetiringSessions();
		if (!m_PendingSession)
		{
			return;
		}

		m_PendingSession->TickPrepare();
		const LoadingProgress progress = m_PendingSession->GetPreparationProgress();
		if (progress.HasFailed())
		{
			const std::string labName = m_PendingSession->GetDescriptor().m_DisplayName;
			m_PendingSession->CancelPrepare();
			m_PendingSession.reset();
			m_State = m_ActiveSession ? m_StateBeforeTransition : LabRunState::Failed;
			SetError(std::format("Failed to prepare lab '{}': {}", labName, progress.m_Detail));
			return;
		}
		if (progress.IsReady())
		{
			GGLAB_UNUSED(CommitPendingSession());
		}
	}

	LabSnapshot LabRuntime::GetLabSnapshot() const noexcept
	{
		LabSnapshot snapshot{};
		snapshot.m_State = ToSnapshotRunState(m_State);
		snapshot.m_FrameInSession = m_FrameInSession;
		snapshot.m_RunConfig = {
			.m_RandomSeed = m_CreateInfo.m_RunConfig.m_RandomSeed,
			.m_WarmupFrames = m_CreateInfo.m_RunConfig.m_WarmupFrames,
			.m_UseFixedDeltaTime = m_CreateInfo.m_RunConfig.m_UseFixedDeltaTime,
			.m_FixedDeltaTime = m_CreateInfo.m_RunConfig.m_FixedDeltaTime,
		};
		snapshot.m_WarmupFramesRemaining = m_WarmupFramesRemaining;
		snapshot.m_EffectiveDeltaTime = m_EffectiveDeltaTime;
		snapshot.m_LastFrame = {
			.m_RenderSceneStatus = m_LastFrameFeedback.m_RenderSceneStatus,
			.m_SubmittedFenceValue = m_LastFrameFeedback.m_SubmittedFence.m_Value,
			.m_ApplicationFrameIndex = m_LastFrameFeedback.m_FrameIndex,
			.m_BackBufferIndex = m_LastFrameFeedback.m_BackBufferIndex,
			.m_HasFeedback = m_HasFrameFeedback,
		};
		snapshot.m_LastError = m_LastError;
		snapshot.m_HasPendingCommands = !m_CommandQueue.IsEmpty();
		snapshot.m_HasPendingSession = m_PendingSession != nullptr;
		snapshot.m_RetiringSessionCount = GetRetiringSessionCount();
		snapshot.m_IsHostActive = m_IsEntered;
		if (m_PendingSession)
		{
			const LabDescriptor& pendingDescriptor = m_PendingSession->GetDescriptor();
			const LoadingProgress progress = m_PendingSession->GetPreparationProgress();
			snapshot.m_PendingLabId = MakeLabIdSnapshot(pendingDescriptor.m_Id);
			snapshot.m_PendingLabName = pendingDescriptor.m_DisplayName;
			snapshot.m_LoadingFraction = progress.m_Fraction;
			snapshot.m_LoadingStage = progress.m_Stage;
			snapshot.m_LoadingDetail = progress.m_Detail;
		}

		snapshot.m_AvailableLabs.reserve(m_Catalog.GetCount());
		for (uint32_t index = 0; index < m_Catalog.GetCount(); ++index)
		{
			if (const LabDescriptor* descriptor = m_Catalog.GetDescriptor(index))
			{
				snapshot.m_AvailableLabs.push_back(MakeDescriptorSnapshot(*descriptor));
			}
		}

		if (!m_ActiveSession)
		{
			return snapshot;
		}

		const LabDescriptor& descriptor = m_ActiveSession->GetDescriptor();
		snapshot.m_ActiveLabId = MakeLabIdSnapshot(descriptor.m_Id);
		snapshot.m_ActiveLabName = descriptor.m_DisplayName;
		snapshot.m_Category = descriptor.m_Category;
		snapshot.m_Description = descriptor.m_Description;
		snapshot.m_SchemaVersion = descriptor.m_SchemaVersion;

		const auto parameters = m_ActiveSession->GetParameters().GetParameters();
		snapshot.m_Parameters.reserve(parameters.size());
		for (const LabParameter& parameter : parameters)
		{
			snapshot.m_Parameters.push_back({
				.m_Desc = MakeParameterDescSnapshot(parameter.m_Desc),
				.m_Value = parameter.m_Value,
				});
		}
		m_ActiveSession->BuildDiagnostics(snapshot.m_Diagnostics);
		return snapshot;
	}

	std::optional<LoadingProgress> LabRuntime::GetLoadingProgress() const noexcept
	{
		if (!m_PendingSession)
		{
			return std::nullopt;
		}

		LoadingProgress progress = m_PendingSession->GetPreparationProgress();
		progress.m_Title =
			std::format("Loading Lab: {}", m_PendingSession->GetDescriptor().m_DisplayName);
		return progress;
	}

	World& LabRuntime::GetWorld() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetWorld();
	}

	Camera& LabRuntime::GetCamera() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetCameraRig().GetActiveCamera();
	}

	CameraController& LabRuntime::GetCameraController() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetCameraRig().GetActiveCameraController();
	}

	CameraRig& LabRuntime::GetCameraRig() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetCameraRig();
	}

	const ViewRenderProfile& LabRuntime::GetViewRenderProfile() const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetViewRenderProfile();
	}

	RenderPipelineBase& LabRuntime::GetRenderPipeline() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetRenderPipeline();
	}

	bool LabRuntime::BeginSessionTransition(
		const LabId& id, std::span<const LabParameterValue> values) noexcept
	{
		if (!m_PendingSession)
		{
			m_StateBeforeTransition = m_State;
		}
		else
		{
			m_PendingSession->CancelPrepare();
			m_PendingSession.reset();
		}
		m_State = LabRunState::Loading;
		if (!m_Catalog.Find(id))
		{
			m_State = m_ActiveSession ? m_StateBeforeTransition : LabRunState::Failed;
			SetError(std::format("Lab '{}' is not registered.", id.GetName()));
			return false;
		}

		m_PendingSession = m_Catalog.Create(id, m_CreateInfo);
		if (!m_PendingSession || !m_PendingSession->IsValid())
		{
			m_PendingSession.reset();
			m_State = m_ActiveSession ? m_StateBeforeTransition : LabRunState::Failed;
			SetError(std::format("Failed to create lab '{}'.", id.GetName()));
			return false;
		}

		LabChangeImpact restoredImpact = LabChangeImpact::Immediate;
		for (const LabParameterValue& value : values)
		{
			LabChangeImpact valueImpact = LabChangeImpact::Immediate;
			if (m_PendingSession->SetParameter(value.m_Id, value.m_Value, &valueImpact))
			{
				restoredImpact = MaxImpact(restoredImpact, valueImpact);
			}
		}
		m_PendingSession->ApplyRestoredParametersForPrepare(restoredImpact);
		m_PendingSession->OnResize(m_CreateInfo.m_WindowWidth, m_CreateInfo.m_WindowHeight);
		m_PendingSession->BeginPrepare();
		m_LastError.clear();
		return true;
	}

	bool LabRuntime::CommitPendingSession() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_PendingSession.get());
		m_PendingSession->CommitPrepare();
		if (m_IsEntered && m_ActiveSession)
		{
			m_ActiveSession->OnExit();
		}
		if (m_ActiveSession)
		{
			m_RetiringSessions.push_back({
				.m_Instance = std::move(m_ActiveSession),
				.m_RetireFence =
					m_HasFrameFeedback ? m_LastFrameFeedback.m_SubmittedFence : RHIFencePoint{},
				});
		}

		m_ActiveSession = std::move(m_PendingSession);
		++m_TemporalSessionSerial;
		GGLAB_ASSERT_MSG(m_TemporalSessionSerial != 0,
			"Lab temporal session serial overflowed its valid range.");
		m_ActiveSession->OnResize(m_CreateInfo.m_WindowWidth, m_CreateInfo.m_WindowHeight);
		if (m_IsEntered)
		{
			m_ActiveSession->OnEnter();
		}

		m_LastError.clear();
		m_WarmupFramesRemaining = m_CreateInfo.m_RunConfig.m_WarmupFrames;
		m_State = m_WarmupFramesRemaining > 0 ? LabRunState::WarmingUp : LabRunState::Ready;
		m_FrameInSession = 0;
		m_EffectiveDeltaTime = 0.0f;
		m_LastFrameFeedback = {};
		m_HasFrameFeedback = false;
		GGLAB_LOG_INFO("Activated lab '{}'.", m_ActiveSession->GetDescriptor().m_Id.GetName());
		return true;
	}

	bool LabRuntime::RestartActiveSessionWithValues(
		std::span<const LabParameterValue> values) noexcept
	{
		if (!m_ActiveSession)
		{
			return false;
		}

		const LabId activeId = m_ActiveSession->GetDescriptor().m_Id;
		return BeginSessionTransition(activeId, values);
	}

	void LabRuntime::PollRetiringSessions() noexcept
	{
		auto* renderer = m_CreateInfo.m_Services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		RHIDevice* device = renderer ? renderer->GetDevice() : nullptr;
		if (!device)
		{
			return;
		}
		std::erase_if(m_RetiringSessions,
			[device](const RetiringSession& retiring) noexcept
			{
				return !retiring.m_RetireFence.IsValid() ||
					device->IsFencePointCompleted(retiring.m_RetireFence);
			});
	}

	void LabRuntime::SetError(std::string message) noexcept
	{
		m_LastError = std::move(message);
		if (!m_ActiveSession)
		{
			m_State = LabRunState::Failed;
		}
		GGLAB_LOG_ERROR("{}", m_LastError);
	}

	LabChangeImpact LabRuntime::MaxImpact(LabChangeImpact lhs, LabChangeImpact rhs) noexcept
	{
		return static_cast<LabChangeImpact>(
			std::max(static_cast<uint8_t>(lhs), static_cast<uint8_t>(rhs)));
	}
}
