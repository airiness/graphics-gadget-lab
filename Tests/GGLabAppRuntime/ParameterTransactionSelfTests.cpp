#include "ParameterTransactionSelfTests.h"
#include "Lab/LabParameter.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ViewControlSnapshot.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace gglab
{
	namespace
	{
		LabParameterSet MakeParameters(SelfTestContext& context) noexcept
		{
			LabParameterSet parameters;
			context.Check(parameters.Add({ .m_Id = LabParameterId("radius"), .m_Name = "Radius",
				.m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f,
				.m_MinValue = LabValue(0.0f), .m_MaxValue = LabValue(4.0f) }), "Register radius");
			context.Check(parameters.Add({ .m_Id = LabParameterId("layout"), .m_Name = "Layout",
				.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
				.m_DefaultValue = uint32_t(2), .m_MinValue = LabValue(uint32_t(1)),
				.m_MaxValue = LabValue(uint32_t(8)) }), "Register layout");
			context.Check(parameters.Add({ .m_Id = LabParameterId("enabled"), .m_Name = "Enabled",
				.m_Type = LabParameterType::Bool, .m_DefaultValue = false }), "Register toggle");
			context.Check(parameters.Add({ .m_Id = LabParameterId("mode"), .m_Name = "Mode",
				.m_Type = LabParameterType::Enum, .m_DefaultValue = int32_t(0),
				.m_EnumItems = { { 0, "Off" }, { 2, "On" } } }), "Register sparse enum");
			context.Check(parameters.Add({ .m_Id = LabParameterId("vector"), .m_Name = "Vector",
				.m_Type = LabParameterType::Vector3, .m_DefaultValue = Vector3(0.0f),
				.m_MinValue = LabValue(Vector3(-1.0f)), .m_MaxValue = LabValue(Vector3(1.0f)) }),
				"Register component ranges");
			context.Check(parameters.Add({ .m_Id = LabParameterId("color"), .m_Name = "Color",
				.m_Type = LabParameterType::Color, .m_DefaultValue = Color(0.5f) }), "Register color");
			context.Check(parameters.Add({ .m_Id = LabParameterId("offset"), .m_Name = "Offset",
				.m_Type = LabParameterType::Int, .m_DefaultValue = int32_t(0),
				.m_MinValue = LabValue(int32_t(-2)), .m_MaxValue = LabValue(int32_t(2)) }),
				"Register signed range");
			return parameters;
		}

		void RunBatchTests(SelfTestContext& context) noexcept
		{
			LabParameterSet parameters = MakeParameters(context);
			const auto rejected = [&](LabParameterValue invalid)
				{
					const std::array batch
					{
						LabParameterValue{ LabParameterId("radius"), 2.0f },
						std::move(invalid),
					};
					std::string error;
					context.Check(!parameters.ApplyBatch(batch, error) && !error.empty(),
						"An invalid tail rejects the whole typed batch");
					context.Check(parameters.Get(LabParameterId("radius"), 0.0f) == 1.0f &&
						!parameters.Find(LabParameterId("radius"))->m_Dirty,
						"A rejected batch changes neither an earlier valid value nor its dirty bit");
				};
			rejected({ LabParameterId("missing"), false });
			rejected({ LabParameterId("enabled"), int32_t(1) });
			rejected({ LabParameterId("layout"), uint32_t(9) });
			rejected({ LabParameterId("layout"), uint32_t(0) });
			rejected({ LabParameterId("layout"), int32_t(2) });
			rejected({ LabParameterId("offset"), int32_t(-3) });
			rejected({ LabParameterId("mode"), int32_t(1) });
			rejected({ LabParameterId("vector"), Vector3(0.0f, 2.0f, 0.0f) });
			rejected({ LabParameterId("vector"), Vector3(-2.0f, 0.0f, 0.0f) });
			rejected({ LabParameterId("vector"), Vector3(0.0f, 0.0f,
				std::numeric_limits<float>::infinity()) });
			rejected({ LabParameterId("color"), Color(0.5f, -0.1f, 0.5f, 1.0f) });
			rejected({ LabParameterId("color"), Color(0.5f, 0.5f, 0.5f, 1.1f) });
			rejected({ LabParameterId("color"), Color(std::numeric_limits<float>::quiet_NaN()) });
			rejected({ LabParameterId("radius"), 3.0f });

			std::string error;
			const auto rejectedRadius = [&](float value)
				{
					const std::array batch{ LabParameterValue{ LabParameterId("radius"), value } };
					context.Check(!parameters.ApplyBatch(batch, error) &&
						parameters.Get(LabParameterId("radius"), 0.0f) == 1.0f,
						"Float batches reject non-finite and out-of-range values without clamping");
				};
			rejectedRadius(std::numeric_limits<float>::quiet_NaN());
			rejectedRadius(std::numeric_limits<float>::infinity());
			rejectedRadius(-std::numeric_limits<float>::infinity());
			rejectedRadius(-1.0f);
			rejectedRadius(4.1f);
			context.Check(!parameters.ApplyBatch({}, error), "An empty batch is not an accepted transaction");

			const std::array batch
			{
				LabParameterValue{ LabParameterId("radius"), 4.0f },
				LabParameterValue{ LabParameterId("layout"), uint32_t(2) },
				LabParameterValue{ LabParameterId("vector"), Vector3(-1.0f, 0.0f, 1.0f) },
				LabParameterValue{ LabParameterId("color"), Color(0.0f, 1.0f, 0.5f, 1.0f) },
				LabParameterValue{ LabParameterId("offset"), int32_t(-2) },
				LabParameterValue{ LabParameterId("enabled"), true },
				LabParameterValue{ LabParameterId("mode"), int32_t(2) },
			};
			const auto applied = parameters.ApplyBatch(batch, error);
			context.Check(applied && error.empty() && applied->m_Changed.size() == 6 &&
				applied->m_Impact == LabChangeImpact::Immediate &&
				!parameters.Find(LabParameterId("layout"))->m_Dirty,
				"A valid mixed batch applies exact boundary values without rebuilding unchanged layout");
			context.Check(parameters.Get(LabParameterId("radius"), 0.0f) == 4.0f &&
				parameters.Get(LabParameterId("enabled"), false) &&
				parameters.Get(LabParameterId("mode"), int32_t(-1)) == 2 &&
				LabValuesEqual(parameters.Find(LabParameterId("vector"))->m_Value, batch[2].m_Value) &&
				LabValuesEqual(parameters.Find(LabParameterId("color"))->m_Value, batch[3].m_Value),
				"Every typed item retains exactly the accepted value");
			const auto repeated = parameters.ApplyBatch(batch, error);
			context.Check(repeated && repeated->m_Changed.empty() &&
				repeated->m_Impact == LabChangeImpact::Immediate,
				"Reapplying the same batch is NoChange, including vectors and colors");
			const std::array rebuild{ LabParameterValue{ LabParameterId("layout"), uint32_t(8) } };
			const auto rebuilt = parameters.ApplyBatch(rebuild, error);
			context.Check(rebuilt && rebuilt->m_Impact == LabChangeImpact::RestartSession &&
				rebuilt->m_Changed.size() == 1, "Changed layout preserves its owning Lab rebuild impact");
			context.Check(parameters.Set(LabParameterId("radius"), 9.0f) &&
				parameters.Get(LabParameterId("radius"), 0.0f) == 4.0f,
				"Existing interactive edits retain their clamping policy");
			const auto& layout = parameters.Find(LabParameterId("layout"))->m_Desc;
			context.Check(GetParameterEffectClass(layout) == ParameterEffectClass::Rebuild,
				"Rebuild impact cannot inherit an Immediate effect declaration");
			LabParameterDesc deferred{ .m_EffectClass = ParameterEffectClass::Deferred };
			context.Check(GetParameterEffectClass(deferred) == ParameterEffectClass::Deferred,
				"An Immediate change impact may explicitly declare a deferred domain effect");
			LabParameterDesc colorRange{ .m_Type = LabParameterType::Color,
				.m_MinValue = LabValue(Color(0.25f)), .m_MaxValue = LabValue(Color(0.75f)) };
			context.Check(!LabParameterSet::ValidateValue(colorRange, Color(0.1f), error) &&
				LabParameterSet::ValidateValue(colorRange, Color(0.25f), error),
				"Color validation respects declared component ranges as well as normalized storage");
		}

		void RunInheritedParameterTests(SelfTestContext& context) noexcept
		{
			const LabParameterId id("samples");
			const auto make = [&]()
				{
					LabParameterSet parameters;
					context.Check(parameters.Add({ .m_Id = id, .m_Name = "Samples",
						.m_Type = LabParameterType::UInt, .m_DefaultValue = uint32_t(512),
						.m_InheritOnEnter = true, .m_MinValue = LabValue(uint32_t(1)),
						.m_MaxValue = LabValue(uint32_t(4096)) }), "Register an entered inherited parameter");
					return parameters;
				};
			auto inherited = make();
			context.Check(inherited.BindInheritedDefault(id, uint32_t(64)) &&
				inherited.Get(id, uint32_t(0)) == 64 && !inherited.Find(id)->m_Dirty &&
				inherited.CaptureRestoredValues().empty(),
				"Entry binds the live baseline without converting inheritance into a restored override");
			context.Check(!inherited.BindInheritedDefault(id, uint32_t(0)) &&
				!inherited.BindInheritedDefault(LabParameterId("missing"), uint32_t(64)) &&
				inherited.Get(id, uint32_t(0)) == 64,
				"An invalid entered baseline is rejected without modifying the parameter");
			auto explicitValue = make();
			context.Check(explicitValue.Set(id, uint32_t(512)) &&
				explicitValue.BindInheritedDefault(id, uint32_t(64)) &&
				explicitValue.Get(id, uint32_t(0)) == 512 &&
				std::get<uint32_t>(explicitValue.Find(id)->m_Desc.m_DefaultValue) == 64 &&
				explicitValue.CaptureRestoredValues().size() == 1,
				"An explicit startup value equal to the placeholder survives a different entered baseline");
			context.Check(explicitValue.FollowInheritedValue(id, uint32_t(512)) &&
				!explicitValue.Find(id)->m_Dirty && explicitValue.CaptureRestoredValues().empty() &&
				!explicitValue.FollowInheritedValue(id, uint32_t(512)),
				"Even a same-value external writer releases explicit intent exactly once");
			context.Check(explicitValue.FollowInheritedValue(id, uint32_t(128)) &&
				explicitValue.Get(id, uint32_t(0)) == 128 &&
				std::get<uint32_t>(explicitValue.Find(id)->m_Desc.m_DefaultValue) == 64,
				"Following an external writer updates current value while preserving the entered reset default");
			GGLAB_UNUSED(explicitValue.ResetAll());
			context.Check(explicitValue.Get(id, uint32_t(0)) == 64 &&
				explicitValue.Find(id)->m_Dirty && explicitValue.CaptureRestoredValues().size() == 1,
				"Reset explicitly requests the entered baseline and survives restart");
			const auto ordinary = MakeParameters(context);
			context.Check(ordinary.CaptureRestoredValues().size() == ordinary.CaptureValues().size(),
				"Ordinary Lab parameters retain the existing restart snapshot policy");
		}

		void RunHostViewProjectionTests(SelfTestContext& context) noexcept
		{
			ResolvedViewRenderSettings view;
			view.m_TemporalAA.m_Enabled = true;
			view.m_Lighting.m_GTAO.m_Power = 2.0f;
			view.m_Lighting.m_EnableAerialPerspective = false;
			view.m_PostProcess.m_Bloom.m_Intensity = 0.2f;
			auto parameters = DescribeHostViewParameters(view);
			const std::array values
			{
				LabParameterValue{ LabParameterId("view.taa.enabled"), false },
				LabParameterValue{ LabParameterId("view.taa.max_history_feedback"), 0.8f },
				LabParameterValue{ LabParameterId("view.taa.depth_absolute_threshold"), 0.1f },
				LabParameterValue{ LabParameterId("view.taa.depth_relative_threshold"), 0.2f },
				LabParameterValue{ LabParameterId("view.taa.velocity_weight_scale"), 0.3f },
				LabParameterValue{ LabParameterId("view.taa.luminance_weight_scale"), 0.4f },
				LabParameterValue{ LabParameterId("view.taa.neighborhood_clamp_expansion"), 0.5f },
				LabParameterValue{ LabParameterId("view.taa.history_relaxation"), 0.0f },
				LabParameterValue{ LabParameterId("view.taa.variance_clip_gamma"), 1.5f },
				LabParameterValue{ LabParameterId("view.taa.texture_lod_bias_offset"), -0.5f },
				LabParameterValue{ LabParameterId("view.taa.history_accumulation"), static_cast<int32_t>(TemporalAAHistoryAccumulation::CompatibilityAge) },
				LabParameterValue{ LabParameterId("view.taa.history_rectification"), static_cast<int32_t>(TemporalAAHistoryRectification::BoundedVarianceClip) },
				LabParameterValue{ LabParameterId("view.taa.history_filter"), static_cast<int32_t>(TemporalAAHistoryFilter::Bilinear) },
				LabParameterValue{ LabParameterId("view.taa.current_filter"), static_cast<int32_t>(TemporalAACurrentFilter::Point) },
				LabParameterValue{ LabParameterId("view.taa.motion_selection"), static_cast<int32_t>(TemporalAAMotionSelection::Center) },
				LabParameterValue{ LabParameterId("view.taa.post_temporal_view"), static_cast<int32_t>(TemporalAAPostTemporalView::Jittered) },
				LabParameterValue{ LabParameterId("view.taa.resolution_preset"), static_cast<int32_t>(TemporalAAResolutionPreset::Quality) },
				LabParameterValue{ LabParameterId("view.gtao.enabled"), false },
				LabParameterValue{ LabParameterId("view.gtao.radius"), 0.5f },
				LabParameterValue{ LabParameterId("view.gtao.direction_count"), uint32_t(3) },
				LabParameterValue{ LabParameterId("view.gtao.step_count"), uint32_t(4) },
				LabParameterValue{ LabParameterId("view.gtao.temporal_accumulation"), false },
				LabParameterValue{ LabParameterId("view.gtao.temporal_max_samples"), uint32_t(7) },
				LabParameterValue{ LabParameterId("view.exposure.ev100"), 5.0f },
				LabParameterValue{ LabParameterId("view.exposure.compensation"), 2.0f },
			};
			std::string error;
			context.Check(parameters.CaptureValues().size() == values.size() &&
				parameters.ApplyBatch(values, error).has_value(),
				"Every Host view binding accepts a strictly validated typed value");
			ApplyHostViewParameters(values, view, true);
			const TemporalAASettings expectedTAA
			{
				.m_Enabled = false,
				.m_DepthAbsoluteThreshold = 0.1f,
				.m_DepthRelativeThreshold = 0.2f,
				.m_MaxHistoryFeedback = 0.8f,
				.m_VelocityWeightScale = 0.3f,
				.m_LuminanceWeightScale = 0.4f,
				.m_NeighborhoodClampExpansion = 0.5f,
				.m_HistoryRelaxation = 0.0f,
				.m_HistoryAccumulation = TemporalAAHistoryAccumulation::CompatibilityAge,
				.m_HistoryRectification = TemporalAAHistoryRectification::BoundedVarianceClip,
				.m_VarianceClipGamma = 1.5f,
				.m_HistoryFilter = TemporalAAHistoryFilter::Bilinear,
				.m_CurrentFilter = TemporalAACurrentFilter::Point,
				.m_MotionSelection = TemporalAAMotionSelection::Center,
				.m_PostTemporalView = TemporalAAPostTemporalView::Jittered,
				.m_ResolutionPreset = TemporalAAResolutionPreset::Quality,
				.m_TextureLodBiasOffset = -0.5f,
			};
			context.Check(view.m_TemporalAA == expectedTAA,
				"Host names project all TAA fields into the shared typed settings semantics");
			const auto& gtao = view.m_Lighting.m_GTAO;
			context.Check(!gtao.m_Enabled && gtao.m_Radius == 0.5f &&
				gtao.m_DirectionCount == 3 && gtao.m_StepCount == 4 &&
				!gtao.m_TemporalAccumulation && gtao.m_TemporalMaxSamples == 7 &&
				gtao.m_FalloffStart == 0.5f && gtao.m_FalloffEnd == 0.5f && gtao.m_Power == 2.0f &&
				!view.m_Lighting.m_EnableAerialPerspective && view.m_PostProcess.m_Bloom.m_Intensity == 0.2f,
				"Host GTAO patches resolve dependent falloff and preserve unrelated content settings");
			context.Check(view.m_Exposure.m_ManualEV100 == 5.0f &&
				view.m_Exposure.m_CompensationEV == 2.0f && view.m_Exposure.m_EffectiveEV100 == 3.0f &&
				view.m_Exposure.m_ExposureScale == 1.0f / (1.2f * 8.0f) &&
				view.m_Exposure.m_PreExposure == view.m_Exposure.m_ExposureScale,
				"Host exposure inputs produce one consistent exposure and storage scale");
			const auto projected = DescribeHostViewParameters(view);
			for (const auto& value : values)
			{
				const auto* parameter = projected.Find(value.m_Id);
				context.Check(parameter && LabValuesEqual(parameter->m_Value, value.m_Value),
					"Host discovery reads back each effective typed field");
			}
		}

		void RunHostViewAttributionTests(SelfTestContext& context) noexcept
		{
			ResolvedViewRenderSettings view;
			view.m_TemporalAA.m_MaxHistoryFeedback = 0.6f;
			view.m_Lighting.m_GTAO.m_Radius = 2.0f;
			const std::array overrides
			{
				LabParameterValue{ LabParameterId("view.taa.max_history_feedback"), 0.6f },
				LabParameterValue{ LabParameterId("view.gtao.radius"), 2.0f },
			};
			const auto find = [](std::span<const HostViewProjectionEntry> projection, std::string_view id)
				{
					const auto found = std::ranges::find(projection, LabParameterId(id), &HostViewProjectionEntry::m_Id);
					return found != projection.end() ? &*found : nullptr;
				};
			ViewRenderSettingsPatch sequence;
			sequence.m_TemporalAA.m_MaxHistoryFeedback = 0.3f;
			sequence.m_Lighting.m_GTAO.m_StepCount = 3u;
			const auto projection = DescribeHostViewProjection(view, { .m_TemporalAA = true }, overrides,
				{ .m_Sequence = &sequence });
			const auto* feedback = find(projection, "view.taa.max_history_feedback");
			const auto* taaEnabled = find(projection, "view.taa.enabled");
			const auto* radius = find(projection, "view.gtao.radius");
			const auto* steps = find(projection, "view.gtao.step_count");
			const auto* ev100 = find(projection, "view.exposure.ev100");
			context.Check(projection.size() == 25 && feedback && taaEnabled && radius && steps && ev100,
				"The projection covers every Host view binding");
			if (!feedback || !taaEnabled || !radius || !steps || !ev100)
			{
				return;
			}
			context.Check(taaEnabled->m_Source == HostViewValueSource::Tooling &&
				steps->m_InheritedSource == HostViewValueSource::Content &&
				ev100->m_Source == HostViewValueSource::Camera && !ev100->m_Override,
				"Inherited values come from the active tooling block, content or the display camera");
			context.Check(radius->m_Source == HostViewValueSource::Host && radius->m_Override &&
				std::get<float>(*radius->m_Override) == 2.0f && std::get<float>(radius->m_Value) == 2.0f,
				"A present Host override is attributed to host and reports its stored value");
			context.Check(feedback->m_Source == HostViewValueSource::Sequence &&
				feedback->m_InheritedSource == HostViewValueSource::Tooling && feedback->m_Override &&
				steps->m_Source == HostViewValueSource::Sequence && !steps->m_Override,
				"A legacy sequence patch is attributed after Host overrides, whatever the stored values");

			ViewRenderSettingsPatch reference;
			reference.m_TemporalAA.m_Enabled = false;
			reference.m_Lighting.m_GTAO.m_TemporalAccumulation = false;
			const auto masked = DescribeHostViewProjection(view, {}, overrides, { .m_ForcedReference = &reference });
			const auto* maskedEnabled = find(masked, "view.taa.enabled");
			const auto* maskedAccumulation = find(masked, "view.gtao.temporal_accumulation");
			const auto* maskedFeedback = find(masked, "view.taa.max_history_feedback");
			context.Check(maskedEnabled && maskedEnabled->m_Source == HostViewValueSource::ForcedReference &&
				maskedEnabled->m_InheritedSource == HostViewValueSource::Content &&
				maskedAccumulation && maskedAccumulation->m_Source == HostViewValueSource::ForcedReference &&
				maskedFeedback && maskedFeedback->m_Source == HostViewValueSource::Host,
				"Reference sampling attributes only its forced fields");

			context.Check(GetHostViewValueSourceName(HostViewValueSource::Content) == "content" &&
				GetHostViewValueSourceName(HostViewValueSource::Tooling) == "tooling" &&
				GetHostViewValueSourceName(HostViewValueSource::Camera) == "camera" &&
				GetHostViewValueSourceName(HostViewValueSource::Host) == "host" &&
				GetHostViewValueSourceName(HostViewValueSource::Sequence) == "sequence" &&
				GetHostViewValueSourceName(HostViewValueSource::ForcedReference) == "forced-reference",
				"Value sources keep their transport names");
		}

		void RunViewControlSnapshotTests(SelfTestContext& context) noexcept
		{
			ApplicationViewSettingsResolution inherited;
			inherited.m_View.m_TemporalAA.m_Enabled = true;
			inherited.m_View.m_Lighting.m_GTAO.m_Radius = 2.0f;
			inherited.m_View.m_Exposure.m_ManualEV100 = 12.0f;
			inherited.m_ToolingBlocks.m_TemporalAA = true;
			std::array overrides
			{
				LabParameterValue{ LabParameterId("view.taa.enabled"), true },
				LabParameterValue{ LabParameterId("view.gtao.radius"), 1.5f },
				LabParameterValue{ LabParameterId("view.exposure.ev100"), 14.0f },
			};
			auto resolved = inherited.m_View;
			ApplyHostViewParameters(overrides, resolved, true);
			ViewRenderSettingsPatch reference;
			reference.m_TemporalAA.m_Enabled = false;
			resolved.m_TemporalAA.m_Enabled = false;
			FrameCaptureFrameState planned;
			planned.m_FrameSerial = 42;
			planned.m_HostViewControl = { "run", 2, 7 };
			planned.m_SettleKey.m_Temporal.m_Identity.m_DisplayViewId = RenderViewID::DebugCamera0;
			planned.m_HostViewProjection = DescribeHostViewProjection(resolved,
				inherited.m_ToolingBlocks, overrides, { .m_ForcedReference = &reference });
			planned.m_Temporal.m_Consumers = { { "temporal-aa", false, "disabled", "not-requested" } };
			auto previous = planned;
			previous.m_FrameSerial = 41;
			previous.m_HostViewControl.m_ContentIdentity = 1;
			previous.m_Outcome = FrameCaptureFrameOutcome::Aborted;
			const auto snapshot = BuildViewControlSnapshot(inherited, overrides, planned, &previous);
			const auto find = [](const auto& entries, std::string_view id) -> const HostViewProjectionEntry&
				{
					return *std::ranges::find(entries, LabParameterId(id), &HostViewProjectionEntry::m_Id);
				};
			context.Check(snapshot.m_Inherited.Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.0f &&
				std::get<float>(find(snapshot.m_HostView, "view.gtao.radius").m_Value) == 1.5f &&
				std::get<float>(find(snapshot.m_HostView, "view.exposure.ev100").m_Value) == 14.0f,
				"GUI snapshots keep inherited values separate from normalized Host-resolved values");
			const auto& taa = find(snapshot.m_HostView, "view.taa.enabled");
			context.Check(taa.m_Override && std::get<bool>(*taa.m_Override) &&
				taa.m_Source == HostViewValueSource::Host && taa.m_InheritedSource == HostViewValueSource::Tooling &&
				std::get<bool>(taa.m_Value) &&
				find(snapshot.m_PlannedFrame.m_Values, "view.taa.enabled").m_Source == HostViewValueSource::ForcedReference &&
				!std::get<bool>(find(snapshot.m_PlannedFrame.m_Values, "view.taa.enabled").m_Value),
				"Equal-value pins remain explicit Host intent even when a reference masks the planned frame");
			context.Check(snapshot.m_PlannedFrame.m_FrameSerial == 42 &&
				snapshot.m_PlannedFrame.m_Outcome == ViewControlFrameOutcome::Planned &&
				snapshot.m_PlannedFrame.m_DisplayViewId == RenderViewID::DebugCamera0 &&
				snapshot.m_PreviousFrame->m_Outcome == ViewControlFrameOutcome::Aborted &&
				snapshot.m_PreviousFrame->m_Control.m_ContentIdentity == 1 &&
				snapshot.m_Control.m_ContentIdentity == 2,
				"Planned and prior aborted observations preserve independent frame and content identities");
			inherited.m_View.m_Lighting.m_GTAO.m_Radius = 9.0f;
			overrides[0].m_Value = false;
			planned.m_HostViewProjection.clear();
			planned.m_Temporal.m_Consumers.clear();
			previous.m_Outcome = FrameCaptureFrameOutcome::Submitted;
			context.Check(snapshot.m_HostView.size() == 25 && snapshot.m_PlannedFrame.m_Values.size() == 25 &&
				snapshot.m_PlannedFrame.m_Consumers.front().m_Status == "disabled" &&
				snapshot.m_Inherited.Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.0f &&
				snapshot.m_PreviousFrame->m_Outcome == ViewControlFrameOutcome::Aborted,
				"GUI observation owns its values and evidence after caller mutation or frame-state replacement");
			const auto first = BuildViewControlSnapshot(inherited, {}, planned, nullptr);
			context.Check(!first.m_PreviousFrame && first.m_PlannedFrame.m_Values.empty() &&
				first.m_PlannedFrame.m_Consumers.empty(),
				"Missing frame evidence stays unavailable rather than being filled from current intent");
		}

		ParameterEffectReceipt BeginReceipt(ParameterEffectClass effectClass) noexcept
		{
			return *ParameterEffectReceipt::Begin(LabParameterId("test.parameter"), effectClass, 7, 10);
		}

		void RunControlRevisionTests(SelfTestContext& context) noexcept
		{
			auto live = MakeParameters(context);
			const LabParameterId radius("radius");
			const auto revision = live.GetControlRevision();
			auto candidate = live;
			GGLAB_UNUSED(candidate.Set(radius, 2.0f));
			context.Check(live.GetControlRevision() == revision && live.Get(radius, 0.0f) == 1.0f,
				"Preparing a parameter snapshot changes no live control revision");
			const auto saved = live;
			GGLAB_UNUSED(live.Set(radius, 3.0f));
			GGLAB_UNUSED(live.Set(radius, 1.0f));
			const auto afterAba = live.GetControlRevision();
			live = saved;
			context.Check(afterAba > revision && live.GetControlRevision() > afterAba &&
				live.Get(radius, 0.0f) == 1.0f,
				"ABA and restoring an older parameter snapshot cannot rewind authored control identity");
			std::string error;
			const std::array equal{ LabParameterValue{ radius, 1.0f } };
			const auto stable = live.GetControlRevision();
			const auto noChange = live.ApplyBatch(equal, error);
			const std::array invalid{ LabParameterValue{ radius, uint32_t(1) } };
			context.Check(noChange && noChange->m_Changed.empty() && !live.ApplyBatch(invalid, error) &&
				live.GetControlRevision() == stable,
				"Strict rejection and semantic NoChange preserve the authored control revision");
			live = std::move(candidate);
			context.Check(live.GetControlRevision() > stable && live.Get(radius, 0.0f) == 2.0f,
				"Moving a prepared value set into a live owner advances rather than replaces its revision");
			LabParameterSet schema;
			GGLAB_UNUSED(schema.Add({ .m_Id = radius, .m_Name = "Radius",
				.m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f }));
			auto desc = schema.Find(radius)->m_Desc;
			desc.m_Impact = LabChangeImpact::RestartSession;
			LabParameterSet replacement;
			GGLAB_UNUSED(replacement.Add(std::move(desc)));
			const auto beforeSchema = schema.GetControlRevision();
			schema = std::move(replacement);
			context.Check(schema.GetControlRevision() > beforeSchema,
				"Replacing declaration policy invalidates control identity even when all values are equal");
		}

		void RunReceiptTests(SelfTestContext& context) noexcept
		{
			const ParameterStateIdentity immediate{ .m_Revision = 7 };
			const ParameterStateIdentity lighting{ 7, "lighting", 21 };
			context.Check(!ParameterEffectReceipt::Begin({}, ParameterEffectClass::Immediate, 7, 10) &&
				!ParameterEffectReceipt::Begin(LabParameterId("x"), ParameterEffectClass::Immediate, 0, 10) &&
				!ParameterEffectReceipt::Begin(LabParameterId("x"), ParameterEffectClass::Immediate, 7, 0),
				"Receipts require a parameter, non-zero batch revision and render frame serial");

			auto receipt = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(!receipt.RecordPublished(lighting, 10) &&
				!receipt.RecordSubmittedFrame(lighting, 10) &&
				!receipt.RecordApplied(ParameterEffectClass::Deferred, lighting, 9) &&
				!receipt.RecordApplied(ParameterEffectClass::Deferred, immediate, 10),
				"Deferred effects require ordered application with owning-domain generation evidence");
			context.Check(receipt.RecordApplied(ParameterEffectClass::Deferred, lighting, 11) &&
				!receipt.GetSnapshot().m_Published && !receipt.RecordSubmittedFrame(lighting, 12),
				"Applied does not mean published or consumed");
			for (const auto& wrong : std::array{
				ParameterStateIdentity{ 8, "lighting", 21 },
				ParameterStateIdentity{ 7, "other", 21 },
				ParameterStateIdentity{ 7, "lighting", 22 } })
			{
				context.Check(!receipt.RecordPublished(wrong, 12),
					"Publication rejects a different revision, domain or generation");
			}
			context.Check(!receipt.RecordPublished(lighting, 10) &&
				receipt.RecordPublished(lighting, 14) && receipt.RecordPublished(lighting, 15) &&
				receipt.GetSnapshot().m_Published == 14,
				"Publication records the first matching domain publication frame");
			for (const auto& wrong : std::array{
				ParameterStateIdentity{ 8, "lighting", 21 },
				ParameterStateIdentity{ 7, "other", 21 },
				ParameterStateIdentity{ 7, "lighting", 20 } })
			{
				context.Check(!receipt.RecordSubmittedFrame(wrong, 15) &&
					!receipt.GetSnapshot().m_FirstConsumed,
					"A later successful submission of another state never establishes firstConsumed");
			}
			// Frame 15 aborts. There is deliberately no successful-submission callback.
			context.Check(!receipt.RecordSubmittedFrame(lighting, 13) &&
				receipt.RecordApplied(ParameterEffectClass::Deferred, lighting, 16) &&
				receipt.GetSnapshot().m_Applied == 11 && !receipt.GetSnapshot().m_FirstConsumed,
				"An aborted consuming frame and repeated application preserve evidence without consumption");
			context.Check(receipt.RecordSubmittedFrame(lighting, 16) &&
				receipt.GetSnapshot().m_Requested == 10 && receipt.GetSnapshot().m_Applied == 11 &&
				receipt.GetSnapshot().m_Published == 14 && receipt.GetSnapshot().m_FirstConsumed == 16,
				"The first successfully submitted frame with the matching identity supplies t0");
			context.Check(!receipt.RecordSubmittedFrame(lighting, 17) && !receipt.Supersede() &&
				!receipt.Fail("late failure") && !receipt.Finish() &&
				receipt.GetSnapshot().m_FirstConsumed == 16,
				"Consumed evidence is terminal and cannot be rewritten by later reports");

			auto beforePublication = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(beforePublication.RecordApplied(ParameterEffectClass::Deferred, lighting, 10) &&
				beforePublication.Supersede() && !beforePublication.RecordPublished(lighting, 12) &&
				!beforePublication.GetSnapshot().m_Published &&
				!beforePublication.GetSnapshot().m_FirstConsumed,
				"Superseding an in-flight generation never invents publication or consumption");
			auto afterPublication = BeginReceipt(ParameterEffectClass::Immediate);
			context.Check(afterPublication.RecordApplied(ParameterEffectClass::Immediate, immediate, 10) &&
				afterPublication.GetSnapshot().m_Published == 10 && afterPublication.Supersede() &&
				!afterPublication.RecordSubmittedFrame(immediate, 11) &&
				afterPublication.GetSnapshot().m_Published == 10 &&
				!afterPublication.GetSnapshot().m_FirstConsumed,
				"An immediate value superseded after an aborted frame retains published but no firstConsumed");
			auto noChange = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(noChange.RecordNoChange(10) && noChange.GetSnapshot().IsTerminal() &&
				noChange.GetSnapshot().m_State == ParameterEffectState::NoChange &&
				!noChange.GetSnapshot().m_Published && !noChange.RecordApplied(
					ParameterEffectClass::Deferred, lighting, 10),
				"NoChange is terminal without starting another generation or publication");
			auto mismatch = BeginReceipt(ParameterEffectClass::Immediate);
			context.Check(!mismatch.RecordApplied(ParameterEffectClass::Deferred, lighting, 10) &&
				mismatch.GetSnapshot().m_State == ParameterEffectState::Failed &&
				mismatch.GetSnapshot().m_Failure == "EffectMismatch" &&
			mismatch.GetSnapshot().m_Applied == 10 && mismatch.GetSnapshot().m_Identity == lighting &&
			!mismatch.GetSnapshot().m_Published,
				"Actual deferred work contradicting an Immediate declaration fails explicitly");
			auto notReached = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(notReached.RecordApplied(ParameterEffectClass::Deferred, lighting, 10) &&
				notReached.Finish() && notReached.GetSnapshot().m_State == ParameterEffectState::EffectNotReached &&
				!notReached.RecordPublished(lighting, 12),
				"Finishing the measurement without matching consumption closes EffectNotReached");
			auto failed = BeginReceipt(ParameterEffectClass::Rebuild);
			context.Check(!failed.Fail("") && failed.Fail("The owning domain failed.") &&
				failed.GetSnapshot().m_State == ParameterEffectState::Failed && !failed.Supersede(),
				"Domain failure is explicit and terminal");
			auto rebuilt = BeginReceipt(ParameterEffectClass::Rebuild);
			const ParameterStateIdentity session{ 7, "session", 3 };
			context.Check(rebuilt.RecordApplied(ParameterEffectClass::Rebuild, session, 11) &&
				rebuilt.RecordPublished(session, 20) && rebuilt.RecordSubmittedFrame(session, 21),
				"Rebuild receipts require session publication before matching consumption");
		}
	}

	void RunParameterTransactionSelfTests(SelfTestContext& context) noexcept
	{
		RunBatchTests(context);
		RunInheritedParameterTests(context);
		RunHostViewProjectionTests(context);
		RunHostViewAttributionTests(context);
		RunViewControlSnapshotTests(context);
		RunReceiptTests(context);
		RunControlRevisionTests(context);
	}
}
