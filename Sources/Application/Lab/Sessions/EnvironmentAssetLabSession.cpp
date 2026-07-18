#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/EnvironmentAssetLabSession.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	struct EnvironmentAssetLabSession::State
	{
		enum class Phase : uint8_t
		{
			WaitForInitialEnvironment,
			WaitForRapidSelection,
			WaitForTransactionalSwitch,
			WaitForDecodeFailure,
			WaitForInvalidShape,
			ObserveFallback,
			WaitForReselection,
			Completed,
		};

		size_t m_PreviousActiveIndex = EnvironmentAssetController::InvalidEntryIndex;
		size_t m_ExpectedActiveIndex = EnvironmentAssetController::InvalidEntryIndex;
		size_t m_ProbeEntryIndex = EnvironmentAssetController::InvalidEntryIndex;
		float m_ElapsedSeconds = 0.0f;
		Phase m_Phase = Phase::WaitForInitialEnvironment;
		bool m_Passed = false;
		std::vector<std::string> m_Errors;
	};

	EnvironmentAssetLabSession::EnvironmentAssetLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>())
	{}

	void EnvironmentAssetLabSession::OnEnter() noexcept
	{
		m_State = std::make_unique<State>();
	}

	void EnvironmentAssetLabSession::OnExit() noexcept
	{
		if (m_Services.m_EnvironmentAssetController)
		{
			m_Services.m_EnvironmentAssetController->Initialize("Assets/Textures/Skybox");
		}
		m_State.reset();
	}

	void EnvironmentAssetLabSession::Update(float deltaTime) noexcept
	{
		GetCamera().Update();
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}

		m_State->m_ElapsedSeconds += deltaTime;
		if (m_State->m_ElapsedSeconds > 120.0f)
		{
			Fail("Environment asset verification timed out.");
			return;
		}

		EnvironmentAssetController& controller =
			*m_Services.m_EnvironmentAssetController;
		const auto entries = controller.GetEntries();
		switch (m_State->m_Phase)
		{
		case State::Phase::WaitForInitialEnvironment:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			const size_t activeIndex = controller.GetActiveEnvironmentIndex();
			if (entries.size() < 4 || activeIndex >= entries.size())
			{
				break;
			}

			const size_t first = (activeIndex + 1) % entries.size();
			const size_t second = (activeIndex + 2) % entries.size();
			const size_t third = (activeIndex + 3) % entries.size();
			if (!controller.SelectEnvironment(first) ||
				!controller.SelectEnvironment(second) ||
				!controller.SelectEnvironment(third))
			{
				Fail("Rapid A-to-B-to-C selection was rejected.");
				return;
			}
			if (controller.GetActiveEnvironmentIndex() != activeIndex ||
				controller.GetPendingEnvironmentIndex() != third)
			{
				Fail("Rapid selection changed the committed source before C became ready.");
				return;
			}
			m_State->m_PreviousActiveIndex = activeIndex;
			m_State->m_ExpectedActiveIndex = third;
			m_State->m_Phase = State::Phase::WaitForRapidSelection;
			break;
		}

		case State::Phase::WaitForRapidSelection:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				if (controller.GetActiveEnvironmentIndex() != m_State->m_PreviousActiveIndex)
				{
					Fail("The active environment changed while rapid-selection C was pending.");
				}
				break;
			}
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("Rapid selection did not commit the last candidate.");
				return;
			}

			const size_t target = (m_State->m_ExpectedActiveIndex + 1) % entries.size();
			if (!controller.SelectEnvironment(target) ||
				controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("Transactional switch did not preserve the active environment.");
				return;
			}
			m_State->m_PreviousActiveIndex = m_State->m_ExpectedActiveIndex;
			m_State->m_ExpectedActiveIndex = target;
			m_State->m_Phase = State::Phase::WaitForTransactionalSwitch;
			break;
		}

		case State::Phase::WaitForTransactionalSwitch:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				if (controller.GetActiveEnvironmentIndex() != m_State->m_PreviousActiveIndex)
				{
					Fail("The active environment changed before the replacement was ready.");
				}
				break;
			}
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("The ready replacement was not committed transactionally.");
				return;
			}
			if (!controller.SelectEnvironmentFile(
				"Shaders/Passes/PassForwardPBR.hlsl",
				"Decode Failure Probe"))
			{
				Fail("Decode-failure probe was rejected before asynchronous loading.");
				return;
			}
			m_State->m_ProbeEntryIndex = controller.GetPendingEnvironmentIndex();
			m_State->m_Phase = State::Phase::WaitForDecodeFailure;
			break;
		}

		case State::Phase::WaitForDecodeFailure:
		{
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("A decode failure replaced the active environment.");
				return;
			}
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			if (m_State->m_ProbeEntryIndex >= entries.size() ||
				entries[m_State->m_ProbeEntryIndex].m_State != EnvironmentAssetEntryState::Failed)
			{
				Fail("The decode-failure candidate did not end in Failed state.");
				return;
			}
			if (!controller.SelectEnvironmentFile(
				"Assets/Textures/UVTest1K.png",
				"Invalid Shape Probe"))
			{
				Fail("Invalid-shape probe was rejected before loading.");
				return;
			}
			m_State->m_ProbeEntryIndex = controller.GetPendingEnvironmentIndex();
			m_State->m_Phase = State::Phase::WaitForInvalidShape;
			break;
		}

		case State::Phase::WaitForInvalidShape:
		{
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("An invalid-shape candidate replaced the active environment.");
				return;
			}
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			if (m_State->m_ProbeEntryIndex >= entries.size() ||
				entries[m_State->m_ProbeEntryIndex].m_State !=
					EnvironmentAssetEntryState::InvalidShape)
			{
				Fail("The non-2:1 candidate did not end in InvalidShape state.");
				return;
			}

			controller.Reset();
			const EnvironmentTextureSource& source = m_Services.m_Renderer->
				GetEnvironmentLightingSystem()->GetBakeSource();
			if (controller.GetActiveEnvironment() ||
				source.m_Type != EnvironmentTextureSourceType::Cubemap ||
				!IsReservedTextureId(source.m_Content.m_Id))
			{
				Fail("Reset did not synchronously commit the pinned fallback environment.");
				return;
			}
			m_State->m_Phase = State::Phase::ObserveFallback;
			break;
		}

		case State::Phase::ObserveFallback:
			controller.Initialize("Assets/Textures/Skybox");
			m_State->m_Phase = State::Phase::WaitForReselection;
			break;

		case State::Phase::WaitForReselection:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			if (!controller.GetActiveEnvironment() ||
				m_Services.m_Renderer->GetEnvironmentLightingSystem()->
					GetBakeSource().m_Type != EnvironmentTextureSourceType::Equirectangular)
			{
				Fail("Environment reselection did not replace the fallback.");
				return;
			}
			Complete();
			break;
		}

		case State::Phase::Completed:
			break;
		}
	}

	void EnvironmentAssetLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Environment Asset Verification";
		if (!m_State)
		{
			return;
		}
		diagnostics.m_Metrics = {
			{ .m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_ElapsedSeconds) },
			{ .m_Name = "Selection phase", .m_Value = std::to_string(std::to_underlying(m_State->m_Phase)) },
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "Transactional environment selection",
			.m_Status = m_State->m_Phase != State::Phase::Completed ?
				LabDiagnosticCheckStatus::Pending :
				m_State->m_Passed ? LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Failed,
			.m_Detail = m_State->m_Phase != State::Phase::Completed ?
				"Verification is running." :
				m_State->m_Passed ? "All environment asset invariants passed." :
					std::format("{} invariant errors.", m_State->m_Errors.size()),
		});
		for (const std::string& error : m_State->m_Errors)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Invariant",
				.m_Status = LabDiagnosticCheckStatus::Failed,
				.m_Detail = error,
			});
		}
	}

	void EnvironmentAssetLabSession::Fail(std::string error) noexcept
	{
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}
		m_State->m_Errors.push_back(std::move(error));
		m_State->m_Passed = false;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_ERROR("ENVIRONMENT ASSET ACCEPTANCE FAIL: {}", m_State->m_Errors.back());
	}

	void EnvironmentAssetLabSession::Complete() noexcept
	{
		GGLAB_ASSERT(m_State);
		m_State->m_Passed = true;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_INFO(
			"ENVIRONMENT ASSET ACCEPTANCE PASS: rapid selection, transactional replacement, failure isolation, shape validation, fallback reset, and reselection invariants passed in {:.2f} s.",
			m_State->m_ElapsedSeconds);
	}

	LabId EnvironmentAssetLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.environment_assets");
	}

	LabDescriptor EnvironmentAssetLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Environment Asset Lab",
			.m_Category = "Systems",
			.m_Description = "Validates transactional HDR environment selection and fallback behavior.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> EnvironmentAssetLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<EnvironmentAssetLabSession>(createInfo);
	}
}
