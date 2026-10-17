#include "ApplicationFrameworkSelfTests.h"

#include "ApplicationContentRegistration.h"
#include "Demo/DemoManager.h"
#include "Lab/LabParameter.h"
#include "Lab/LabSessionBase.h"
#include "GGLabTestCore/SelfTest.h"
#include "GGLabRuntime/Graphics/RenderHost.h"

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace gglab
{
	namespace
	{
		class StubRenderHost final : public RenderHost
		{
		public:
			[[nodiscard]] bool IsInitialized() const noexcept override { return false; }
			void Finalize() noexcept override {}
			void OnResize(uint32_t, uint32_t) noexcept override {}
			void OnSuspend() noexcept override {}
			void OnResume() noexcept override {}
			[[nodiscard]] bool IsSuspended() const noexcept override { return false; }
			[[nodiscard]] RHIContext* GetRHIContext() const noexcept override { return nullptr; }
			[[nodiscard]] RenderFrame BeginFrame() noexcept override
			{
				return RenderFrame(RHIFrameBeginStatus::Fatal);
			}
			[[nodiscard]] RenderFrameBuildResult BuildFrame(const RenderFrameBuildRequest&) noexcept override
			{
				return {};
			}
			[[nodiscard]] TemporalFrameTransaction& BeginTemporalFrame(RenderFrame&,
				const ResolvedTemporalFramePlan&, const ViewResolution&, float,
				const std::optional<TemporalReferenceSample>&) noexcept override
			{
				return m_Transaction;
			}
			void InvalidateTemporalFrameAfterLateContractFailure(RenderFrame&) noexcept override {}
			void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept override {}
			[[nodiscard]] RenderGraph::CreateInfo CreateRenderGraphCreateInfo()
				const noexcept override
			{
				return {};
			}
			[[nodiscard]] const TemporalAACapabilityStatus& GetTemporalAACapabilityStatus()
				const noexcept override
			{
				return m_Capabilities;
			}
			[[nodiscard]] const GTAOCapabilityStatus& GetGTAOCapabilityStatus()
				const noexcept override
			{
				return m_GTAOCapabilities;
			}
			void Render(RenderFrame&, RenderGraph&, const RenderFrameContext&) noexcept override {}
			[[nodiscard]] RHIFrameEndResult EndFrame(RenderFrame&) noexcept override
			{
				return RHIFrameEndResult::Fatal();
			}
			[[nodiscard]] EnvironmentLightingViewBase* GetEnvironmentLightingView()
				const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] EnvironmentLightingControlBase* GetEnvironmentLightingControl()
				const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] IBLCacheControlBase* GetIBLCacheControl() const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] IBLPreviewViewBase* GetIBLPreviewView() const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] IBLPreviewControlBase* GetIBLPreviewControl() const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] PostProcessPreviewViewBase* GetPostProcessPreviewView()
				const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] PostProcessPreviewControlBase* GetPostProcessPreviewControl()
				const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] ShadowPreviewViewBase* GetShadowPreviewView() const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] GpuProfilingViewBase* GetGpuProfilingView() const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] GpuProfilingControlBase* GetGpuProfilingControl() const noexcept override
			{
				return nullptr;
			}
			[[nodiscard]] FrameCaptureControlBase* GetFrameCaptureControl() const noexcept override
			{
				return nullptr;
			}

		private:
			void AbortFrame(uint64_t) noexcept override {}
			TemporalFrameTransaction m_Transaction{};
			TemporalAACapabilityStatus m_Capabilities{};
			GTAOCapabilityStatus m_GTAOCapabilities{};
		};

		std::unique_ptr<DemoBase> CreateNullDemo(const DemoCreateInfo&, const LabStartupSelection&, std::span<const LabRegistration>) noexcept
		{
			return nullptr;
		}

		std::unique_ptr<LabSessionBase> CreateNullLab(const LabSessionCreateInfo&) noexcept
		{
			return nullptr;
		}

		LabRegistration MakeLabRegistration(std::string_view id) noexcept
		{
			return {
				.m_Descriptor = {
					.m_Id = LabId(id),
					.m_DisplayName = std::string(id),
					.m_Category = "SelfTest",
					.m_Description = "Registration policy fixture",
				},
				.m_Factory = &CreateNullLab,
			};
		}

		ApplicationDemoRegistration MakeDemoRegistration(
			std::string_view id, bool providesLabRuntime = false) noexcept
		{
			return {
				.m_Id = std::string(id),
				.m_Factory = &CreateNullDemo,
				.m_ProvidesLabRuntime = providesLabRuntime,
			};
		}

		struct TransitionTestState final
		{
			bool m_Constructed = false;
			bool m_PrepareBegan = false;
			bool m_HostStateApplied = false;
			bool m_PrepareTicked = false;
			bool m_PrepareTickObservedHostState = false;
			bool m_PrepareCommitted = false;
		};

		class TransitionTestDemo final : public DemoBase
		{
		public:
			explicit TransitionTestDemo(TransitionTestState* state) noexcept :
				m_State(state)
			{
			}

			std::string_view GetName() const noexcept override { return "Transition Test"; }
			void BeginPrepare() noexcept override { m_State->m_PrepareBegan = true; }
			void TickPrepare() noexcept override
			{
				m_State->m_PrepareTicked = true;
				m_State->m_PrepareTickObservedHostState = m_State->m_HostStateApplied;
			}
			void CommitPrepare() noexcept override { m_State->m_PrepareCommitted = true; }
			void Update() noexcept override {}

			World& GetWorld() noexcept override { std::terminate(); }
			Camera& GetCamera() noexcept override { std::terminate(); }
			CameraController& GetCameraController() noexcept override { std::terminate(); }
			CameraRig& GetCameraRig() noexcept override { std::terminate(); }
			const ViewRenderProfile& GetViewRenderProfile() const noexcept override
			{
				std::terminate();
			}
			RenderPipelineBase& GetRenderPipeline() noexcept override { std::terminate(); }

		private:
			TransitionTestState* m_State = nullptr;
		};

		void RunDemoTransitionSynchronizationSelfTests(SelfTestContext& context) noexcept
		{
			DemoManager manager(nullptr);
			TransitionTestState state{};
			const uint32_t demoIndex = manager.RegisterDemo("test.demo.transition",
				[&state]() -> std::unique_ptr<DemoBase>
				{
					state.m_Constructed = true;
					return std::make_unique<TransitionTestDemo>(&state);
				});
			manager.RequestActiveDemo(demoIndex);
			manager.BeginTransitionTick();
			context.Check(state.m_Constructed && state.m_PrepareBegan &&
				!state.m_PrepareTicked && !state.m_PrepareCommitted &&
				manager.GetDemo(demoIndex) != nullptr,
				"Demo transition exposes newly created pending content before its first preparation tick");

			state.m_HostStateApplied = true;
			const bool transitionSucceeded = manager.CompleteTransitionTick();
			context.Check(transitionSucceeded && state.m_PrepareTickObservedHostState &&
				state.m_PrepareCommitted && manager.GetActiveIndex() == demoIndex,
				"Host state applied at the transition seam reaches the first preparation and commit");
			manager.PrepareForAssetShutdown();
		}

		void RunLabParameterParseSelfTests(SelfTestContext& context) noexcept
		{
			const auto parse = [](const LabParameterDesc& desc, std::string_view text)
			{
				std::string error;
				std::optional<LabValue> value = ParseLabParameterValue(desc, text, error);
				return std::pair{ std::move(value), std::move(error) };
			};
			const LabParameterDesc count{ .m_Id = LabParameterId("test.count"), .m_Name = "Count",
				.m_Type = LabParameterType::UInt, .m_DefaultValue = uint32_t(2),
				.m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(8)) };
			const auto [inRange, inRangeError] = parse(count, "8");
			context.Check(inRange && std::get<uint32_t>(*inRange) == 8 && inRangeError.empty(),
				"Textual Lab parameters accept an in-range unsigned value");
			context.Check(!parse(count, "9").first && !parse(count, "0").first &&
				!parse(count, "-1").first && !parse(count, "3x").first && !parse(count, "").first,
				"Textual Lab parameters reject out-of-range or malformed numbers instead of clamping");

			const LabParameterDesc scale{ .m_Id = LabParameterId("test.scale"), .m_Name = "Scale",
				.m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f,
				.m_MinValue = LabValue(0.0f), .m_MaxValue = LabValue(4.0f) };
			const auto [zero, zeroError] = parse(scale, "0");
			context.Check(zero && std::get<float>(*zero) == 0.0f && !parse(scale, "nan").first &&
				!parse(scale, "inf").first && !parse(scale, "4.5").first,
				"Textual float parameters require a finite in-range value");

			const LabParameterDesc mode{ .m_Id = LabParameterId("test.mode"), .m_Name = "Mode",
				.m_Type = LabParameterType::Enum, .m_DefaultValue = int32_t(0),
				.m_EnumItems = { { .m_Value = 0, .m_Name = "Off" },{ .m_Value = 2, .m_Name = "Sanitized" } } };
			const auto [byName, byNameError] = parse(mode, "sanitized");
			const auto [byValue, byValueError] = parse(mode, "2");
			context.Check(byName && std::get<int32_t>(*byName) == 2 && byValue &&
				std::get<int32_t>(*byValue) == 2 && !parse(mode, "1").first &&
				!parse(mode, "Boundary").first,
				"Textual enum parameters accept item names or values and reject unknown items");

			const LabParameterDesc toggle{ .m_Id = LabParameterId("test.toggle"), .m_Name = "Toggle",
				.m_Type = LabParameterType::Bool, .m_DefaultValue = false };
			const LabParameterDesc color{ .m_Id = LabParameterId("test.color"), .m_Name = "Color",
				.m_Type = LabParameterType::Color, .m_DefaultValue = Color(1.0f, 1.0f, 1.0f, 1.0f) };
			const auto [enabled, enabledError] = parse(toggle, "TRUE");
			context.Check(enabled && std::get<bool>(*enabled) && !parse(toggle, "yes").first &&
				!parse(color, "1").first,
				"Textual bool parameters are explicit and composite types have no textual form");
		}
	}

	void RunApplicationFrameworkSelfTests(SelfTestContext& context) noexcept
	{
		RunDemoTransitionSynchronizationSelfTests(context);
		RunLabParameterParseSelfTests(context);

		ApplicationContentRegistration empty;
		context.Check(!empty.IsValid(),
			"A host cannot start with an empty content registration set");

		ApplicationContentRegistration minimal;
		minimal.m_Demos.push_back(MakeDemoRegistration("test.demo.minimal"));
		const ApplicationContentSelection minimalSelection = ResolveApplicationContentSelection(minimal, "test.demo.minimal", std::nullopt);
		context.Check(minimal.IsValid() && minimal.m_Labs.empty() &&
			minimalSelection.Succeeded(),
			"A minimal host can register one supported Demo and omit optional Lab content");
		ShaderProgramDemandSet minimalDemands;
		context.Check(AppendSelectedContentShaderProgramDemand(minimalSelection, minimalDemands) && minimalDemands.GetPrograms().empty(),
			"Content without shader demand contributes an empty stable snapshot");

		ApplicationContentRegistration invalidShaderDemand = minimal;
		invalidShaderDemand.m_Demos.front().m_ShaderPrograms.push_back({});
		context.Check(!invalidShaderDemand.IsValid(),
			"Content registration rejects invalid shader program identities");

		context.Check(ResolveApplicationContentSelection(
			minimal, "test.demo.unknown", std::nullopt).m_Status == ApplicationContentSelectionStatus::StartupDemoUnavailable,
			"An unavailable startup Demo ID is rejected before runtime startup");

		ApplicationContentRegistration duplicateDemo = minimal;
		duplicateDemo.m_Demos.push_back(MakeDemoRegistration("test.demo.minimal"));
		context.Check(!duplicateDemo.IsValid(), "Duplicate Demo registration IDs are rejected");

		ApplicationContentRegistration duplicateLab = minimal;
		duplicateLab.m_Labs.push_back(MakeLabRegistration("test.lab.duplicate"));
		duplicateLab.m_Labs.push_back(MakeLabRegistration("test.lab.duplicate"));
		context.Check(!duplicateLab.IsValid(),
			"Duplicate Lab registration IDs are rejected by the shared Lab catalog contract");

		ApplicationContentRegistration labSubset;
		labSubset.m_Demos.push_back(MakeDemoRegistration("test.demo.lab", true));
		labSubset.m_Labs.push_back(MakeLabRegistration("test.lab.supported"));
		context.Check(ResolveApplicationContentSelection(labSubset, "test.demo.lab", "test.lab.supported").Succeeded(),
			"A host-selected Lab subset resolves its supported startup Lab");
		labSubset.m_Labs.front().m_ShaderPrograms.push_back({
			.m_ProgramId = "gglab.shader.test",
			.m_VariantId = "compute",
			.m_Stage = ShaderStage::Compute,
		});
		const ApplicationContentSelection shaderSelection = ResolveApplicationContentSelection(labSubset, "test.demo.lab", "test.lab.supported");
		ShaderProgramDemandSet selectedDemands;
		context.Check(shaderSelection.Succeeded() &&
			AppendSelectedContentShaderProgramDemand(shaderSelection, selectedDemands) &&
			selectedDemands.GetPrograms().size() == 1,
			"Only the selected content contributes its stable shader program demand");
		context.Check(ResolveApplicationContentSelection(
			labSubset, "test.demo.lab", "test.lab.unavailable").m_Status == ApplicationContentSelectionStatus::StartupLabUnavailable,
			"An unavailable startup Lab ID is rejected before Demo transition");
		context.Check(ResolveApplicationContentSelection(
			labSubset, "test.demo.lab", std::nullopt).m_Status == ApplicationContentSelectionStatus::StartupLabUnavailable,
			"A Lab-hosting Demo requires an explicit host-selected startup Lab");
	}
}
