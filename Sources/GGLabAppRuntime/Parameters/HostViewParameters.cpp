#include "Parameters/HostViewParameters.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAO.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <string>
#include <vector>

namespace gglab
{
	namespace
	{
		struct Binding
		{
			LabParameterDesc m_Desc;
			LabValue (*m_Read)(const ResolvedViewRenderSettings&);
			void (*m_Write)(ResolvedViewRenderSettings&, const LabValue&);
		};

		template<typename Accessor>
		Binding MakeBinding(const char* id, const char* group, Accessor,
			std::optional<LabValue> min = {}, std::optional<LabValue> max = {},
			std::vector<LabEnumItem> enums = {})
		{
			using T = std::remove_cvref_t<decltype(Accessor{}(std::declval<ResolvedViewRenderSettings&>()))>;
			return {
				{ .m_Id = LabParameterId(id), .m_Name = id, .m_Group = group,
					.m_Type = std::is_enum_v<T> ? LabParameterType::Enum :
						std::is_same_v<T, bool> ? LabParameterType::Bool :
							std::is_same_v<T, float> ? LabParameterType::Float : LabParameterType::UInt,
					.m_MinValue = std::move(min), .m_MaxValue = std::move(max),
					.m_EnumItems = std::move(enums) },
				[](const ResolvedViewRenderSettings& settings) -> LabValue
				{
					if constexpr (std::is_enum_v<T>)
					{
						return static_cast<int32_t>(Accessor{}(settings));
					}
					else
					{
						return Accessor{}(settings);
					}
				},
				[](ResolvedViewRenderSettings& settings, const LabValue& value)
				{
					if constexpr (std::is_enum_v<T>)
					{
						Accessor{}(settings) = static_cast<T>(std::get<int32_t>(value));
					}
					else
					{
						Accessor{}(settings) = std::get<T>(value);
					}
				},
			};
		}

		template<typename Enum, typename Name>
		std::vector<LabEnumItem> EnumItems(std::initializer_list<Enum> values, Name name)
		{
			std::vector<LabEnumItem> result;
			for (Enum value : values)
			{
				result.push_back({ static_cast<int32_t>(value), std::string(name(value)) });
			}
			return result;
		}

		const auto& Bindings()
		{
			static const std::array bindings
			{
				MakeBinding("view.taa.enabled", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_Enabled; }),
				MakeBinding("view.taa.max_history_feedback", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_MaxHistoryFeedback; }, 0.0f, TemporalAAMaxHistoryFeedbackCeiling),
				MakeBinding("view.taa.depth_absolute_threshold", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_DepthAbsoluteThreshold; }, 0.0f, TemporalAAMaxDepthThreshold),
				MakeBinding("view.taa.depth_relative_threshold", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_DepthRelativeThreshold; }, 0.0f, TemporalAAMaxDepthThreshold),
				MakeBinding("view.taa.velocity_weight_scale", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_VelocityWeightScale; }, 0.0f, TemporalAAMaxVelocityWeightScale),
				MakeBinding("view.taa.luminance_weight_scale", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_LuminanceWeightScale; }, 0.0f, TemporalAAMaxLuminanceWeightScale),
				MakeBinding("view.taa.neighborhood_clamp_expansion", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_NeighborhoodClampExpansion; }, 0.0f, TemporalAAMaxNeighborhoodClampExpansion),
				MakeBinding("view.taa.history_relaxation", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryRelaxation; }, 0.0f, TemporalAAMaxHistoryRelaxation),
				MakeBinding("view.taa.variance_clip_gamma", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_VarianceClipGamma; }, TemporalAAMinVarianceClipGamma, TemporalAAMaxVarianceClipGamma),
				MakeBinding("view.taa.texture_lod_bias_offset", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_TextureLodBiasOffset; }, TemporalAAMinTextureLodBiasOffset, TemporalAAMaxTextureLodBiasOffset),
				MakeBinding("view.taa.history_accumulation", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryAccumulation; }, {}, {},
					EnumItems({ TemporalAAHistoryAccumulation::CompatibilityAge, TemporalAAHistoryAccumulation::EffectiveSamples },
						&GetTemporalAAHistoryAccumulationName)),
				MakeBinding("view.taa.history_rectification", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryRectification; }, {}, {},
					EnumItems({ TemporalAAHistoryRectification::MinMaxClamp, TemporalAAHistoryRectification::VarianceClip, TemporalAAHistoryRectification::BoundedVarianceClip },
						&GetTemporalAAHistoryRectificationName)),
				MakeBinding("view.taa.history_filter", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryFilter; }, {}, {},
					EnumItems({ TemporalAAHistoryFilter::Bilinear, TemporalAAHistoryFilter::CatmullRomClamped },
						&GetTemporalAAHistoryFilterName)),
				MakeBinding("view.taa.current_filter", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_CurrentFilter; }, {}, {},
					EnumItems({ TemporalAACurrentFilter::Point, TemporalAACurrentFilter::Gaussian },
						&GetTemporalAACurrentFilterName)),
				MakeBinding("view.taa.motion_selection", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_MotionSelection; }, {}, {},
					EnumItems({ TemporalAAMotionSelection::Center, TemporalAAMotionSelection::ClosestDepth },
						&GetTemporalAAMotionSelectionName)),
				MakeBinding("view.taa.post_temporal_view", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_PostTemporalView; }, {}, {},
					EnumItems({ TemporalAAPostTemporalView::Jittered, TemporalAAPostTemporalView::Unjittered },
						&GetTemporalAAPostTemporalViewName)),
				MakeBinding("view.taa.resolution_preset", "Temporal AA",
					[](auto& s) -> auto& { return s.m_TemporalAA.m_ResolutionPreset; }, {}, {},
					EnumItems({ TemporalAAResolutionPreset::Native, TemporalAAResolutionPreset::Quality },
						&GetTemporalAAResolutionPresetName)),
				MakeBinding("view.gtao.enabled", "GTAO",
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_Enabled; }),
				MakeBinding("view.gtao.radius", "GTAO",
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_Radius; }, 0.01f, 10.0f),
				MakeBinding("view.gtao.direction_count", "GTAO",
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_DirectionCount; }, 1u, GTAOMaxDirectionCount),
				MakeBinding("view.gtao.step_count", "GTAO",
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_StepCount; }, 1u, GTAOMaxStepCount),
				MakeBinding("view.gtao.temporal_max_samples", "GTAO",
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_TemporalMaxSamples; }, 1u, GTAOMaxTemporalSamples),
				MakeBinding("view.gtao.temporal_accumulation", "GTAO",
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_TemporalAccumulation; }),
				MakeBinding("view.exposure.ev100", "Exposure",
					[](auto& s) -> auto& { return s.m_Exposure.m_ManualEV100; }, -16.0f, 24.0f),
				MakeBinding("view.exposure.compensation", "Exposure",
					[](auto& s) -> auto& { return s.m_Exposure.m_CompensationEV; }, -10.0f, 10.0f),
			};
			return bindings;
		}
	}

	LabParameterSet DescribeHostViewParameters(const ResolvedViewRenderSettings& settings) noexcept
	{
		LabParameterSet result;
		for (const auto& binding : Bindings())
		{
			auto desc = binding.m_Desc;
			desc.m_DefaultValue = binding.m_Read(settings);
			GGLAB_UNUSED(result.Add(std::move(desc)));
		}
		return result;
	}

	void ApplyHostViewParameters(std::span<const LabParameterValue> values,
		ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept
	{
		for (const auto& value : values)
		{
			const auto& bindings = Bindings();
			const auto binding = std::ranges::find_if(bindings, [&value](const auto& item)
			{
				return item.m_Desc.m_Id == value.m_Id;
			});
			if (binding != bindings.end())
			{
				binding->m_Write(settings, value.m_Value);
			}
		}
		auto& gtao = settings.m_Lighting.m_GTAO;
		gtao.m_FalloffStart = std::clamp(gtao.m_FalloffStart, 0.0f, gtao.m_Radius);
		gtao.m_FalloffEnd = std::clamp(gtao.m_FalloffEnd, gtao.m_FalloffStart, gtao.m_Radius);
		settings.m_Exposure = ResolveManualExposureSettings(settings.m_Exposure.m_ManualEV100,
			settings.m_Exposure.m_CompensationEV, enablePreExposure);
	}
}
