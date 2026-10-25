#include "Parameters/HostViewParameters.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"

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
		// The settings block that supplies a binding's inherited value.
		enum class Block : uint8_t
		{
			TemporalAA,
			GTAO,
			Exposure,
		};

		[[nodiscard]] const char* GetBlockGroup(Block block) noexcept
		{
			switch (block)
			{
			case Block::TemporalAA:
				return "Temporal AA";
			case Block::GTAO:
				return "GTAO";
			case Block::Exposure:
				return "Exposure";
			}
			return "";
		}

		struct Binding
		{
			LabParameterDesc m_Desc;
			Block m_Block = Block::TemporalAA;
			LabValue (*m_Read)(const ResolvedViewRenderSettings&);
			void (*m_Write)(ViewRenderSettingsPatch&, const LabValue&);
			bool (*m_IsSet)(const ViewRenderSettingsPatch&);
		};

		template<typename Accessor>
		Binding MakeBinding(const char* id, Block block, Accessor,
			std::optional<LabValue> min = {}, std::optional<LabValue> max = {},
			std::vector<LabEnumItem> enums = {})
		{
			using T = std::remove_cvref_t<decltype(Accessor{}(std::declval<ResolvedViewRenderSettings&>()))>;
			return {
				{ .m_Id = LabParameterId(id), .m_Name = id, .m_Group = GetBlockGroup(block),
					.m_Type = std::is_enum_v<T> ? LabParameterType::Enum :
						std::is_same_v<T, bool> ? LabParameterType::Bool :
							std::is_same_v<T, float> ? LabParameterType::Float : LabParameterType::UInt,
					.m_MinValue = std::move(min), .m_MaxValue = std::move(max),
					.m_EnumItems = std::move(enums) },
				block,
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
				[](ViewRenderSettingsPatch& settings, const LabValue& value)
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
				[](const ViewRenderSettingsPatch& patch) { return Accessor{}(patch).has_value(); }
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
				MakeBinding("view.taa.enabled", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_Enabled; }),
				MakeBinding("view.taa.max_history_feedback", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_MaxHistoryFeedback; }, 0.0f, TemporalAAMaxHistoryFeedbackCeiling),
				MakeBinding("view.taa.depth_absolute_threshold", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_DepthAbsoluteThreshold; }, 0.0f, TemporalAAMaxDepthThreshold),
				MakeBinding("view.taa.depth_relative_threshold", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_DepthRelativeThreshold; }, 0.0f, TemporalAAMaxDepthThreshold),
				MakeBinding("view.taa.velocity_weight_scale", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_VelocityWeightScale; }, 0.0f, TemporalAAMaxVelocityWeightScale),
				MakeBinding("view.taa.luminance_weight_scale", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_LuminanceWeightScale; }, 0.0f, TemporalAAMaxLuminanceWeightScale),
				MakeBinding("view.taa.neighborhood_clamp_expansion", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_NeighborhoodClampExpansion; }, 0.0f, TemporalAAMaxNeighborhoodClampExpansion),
				MakeBinding("view.taa.history_relaxation", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryRelaxation; }, 0.0f, TemporalAAMaxHistoryRelaxation),
				MakeBinding("view.taa.variance_clip_gamma", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_VarianceClipGamma; }, TemporalAAMinVarianceClipGamma, TemporalAAMaxVarianceClipGamma),
				MakeBinding("view.taa.texture_lod_bias_offset", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_TextureLodBiasOffset; }, TemporalAAMinTextureLodBiasOffset, TemporalAAMaxTextureLodBiasOffset),
				MakeBinding("view.taa.history_accumulation", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryAccumulation; }, {}, {},
					EnumItems({ TemporalAAHistoryAccumulation::CompatibilityAge, TemporalAAHistoryAccumulation::EffectiveSamples },
						&GetTemporalAAHistoryAccumulationName)),
				MakeBinding("view.taa.history_rectification", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryRectification; }, {}, {},
					EnumItems({ TemporalAAHistoryRectification::MinMaxClamp, TemporalAAHistoryRectification::VarianceClip, TemporalAAHistoryRectification::BoundedVarianceClip },
						&GetTemporalAAHistoryRectificationName)),
				MakeBinding("view.taa.history_filter", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_HistoryFilter; }, {}, {},
					EnumItems({ TemporalAAHistoryFilter::Bilinear, TemporalAAHistoryFilter::CatmullRomClamped },
						&GetTemporalAAHistoryFilterName)),
				MakeBinding("view.taa.current_filter", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_CurrentFilter; }, {}, {},
					EnumItems({ TemporalAACurrentFilter::Point, TemporalAACurrentFilter::Gaussian },
						&GetTemporalAACurrentFilterName)),
				MakeBinding("view.taa.motion_selection", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_MotionSelection; }, {}, {},
					EnumItems({ TemporalAAMotionSelection::Center, TemporalAAMotionSelection::ClosestDepth },
						&GetTemporalAAMotionSelectionName)),
				MakeBinding("view.taa.post_temporal_view", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_PostTemporalView; }, {}, {},
					EnumItems({ TemporalAAPostTemporalView::Jittered, TemporalAAPostTemporalView::Unjittered },
						&GetTemporalAAPostTemporalViewName)),
				MakeBinding("view.taa.resolution_preset", Block::TemporalAA,
					[](auto& s) -> auto& { return s.m_TemporalAA.m_ResolutionPreset; }, {}, {},
					EnumItems({ TemporalAAResolutionPreset::Native, TemporalAAResolutionPreset::Quality },
						&GetTemporalAAResolutionPresetName)),
				MakeBinding("view.gtao.enabled", Block::GTAO,
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_Enabled; }),
				MakeBinding("view.gtao.radius", Block::GTAO,
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_Radius; }, GTAOMinRadius, GTAOMaxRadius),
				MakeBinding("view.gtao.direction_count", Block::GTAO,
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_DirectionCount; }, 1u, GTAOMaxDirectionCount),
				MakeBinding("view.gtao.step_count", Block::GTAO,
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_StepCount; }, 1u, GTAOMaxStepCount),
				MakeBinding("view.gtao.temporal_max_samples", Block::GTAO,
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_TemporalMaxSamples; }, 1u, GTAOMaxTemporalSamples),
				MakeBinding("view.gtao.temporal_accumulation", Block::GTAO,
					[](auto& s) -> auto& { return s.m_Lighting.m_GTAO.m_TemporalAccumulation; }),
				MakeBinding("view.exposure.ev100", Block::Exposure,
					[](auto& s) -> auto& { return s.m_Exposure.m_ManualEV100; },
					Camera::MinimumManualEV100, Camera::MaximumManualEV100),
				MakeBinding("view.exposure.compensation", Block::Exposure,
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

	std::optional<LabValue> ReadHostViewParameter(const LabParameterId& id,
		const ResolvedViewRenderSettings& settings) noexcept
	{
		const auto& bindings = Bindings();
		const auto found = std::ranges::find_if(bindings,
			[&](const auto& binding) { return binding.m_Desc.m_Id == id; });
		return found != bindings.end() ? std::optional(found->m_Read(settings)) : std::nullopt;
	}

	bool HasHostViewParameterPatch(const LabParameterId& id,
		const ViewRenderSettingsPatch& patch) noexcept
	{
		const auto& bindings = Bindings();
		const auto found = std::ranges::find_if(bindings,
			[&](const auto& binding) { return binding.m_Desc.m_Id == id; });
		return found != bindings.end() && found->m_IsSet(patch);
	}

	void ApplyHostViewParameters(std::span<const LabParameterValue> values,
		ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept
	{
		ViewRenderSettingsPatch patch;
		for (const auto& value : values)
		{
			const auto& bindings = Bindings();
			const auto binding = std::ranges::find_if(bindings, [&value](const auto& item)
			{
				return item.m_Desc.m_Id == value.m_Id;
			});
			if (binding != bindings.end())
			{
				binding->m_Write(patch, value.m_Value);
			}
		}
		ApplyViewRenderSettingsPatch(patch, settings, enablePreExposure);
	}

	std::string_view GetHostViewValueSourceName(HostViewValueSource source) noexcept
	{
		switch (source)
		{
		case HostViewValueSource::Content:
			return "content";
		case HostViewValueSource::Tooling:
			return "tooling";
		case HostViewValueSource::Camera:
			return "camera";
		case HostViewValueSource::Host:
			return "host";
		case HostViewValueSource::Sequence:
			return "sequence";
		case HostViewValueSource::ForcedReference:
			return "forced-reference";
		}
		return "unknown";
	}

	std::vector<HostViewProjectionEntry> DescribeHostViewProjection(
		const ResolvedViewRenderSettings& settings, const ApplicationToolingBlocks& tooling,
		std::span<const LabParameterValue> overrides, const HostViewFramePatches& patches)
	{
		GGLAB_ASSERT_MSG(!patches.m_Sequence || !patches.m_ForcedReference,
			"Reference sampling replaces legacy sequence patches in a frame.");
		std::vector<HostViewProjectionEntry> result;
		result.reserve(Bindings().size());
		for (const auto& binding : Bindings())
		{
			const LabParameterId& id = binding.m_Desc.m_Id;
			HostViewProjectionEntry& entry = result.emplace_back(HostViewProjectionEntry{
				.m_Id = id,
				.m_Type = binding.m_Desc.m_Type,
				.m_Value = binding.m_Read(settings),
			});
			switch (binding.m_Block)
			{
			case Block::TemporalAA:
				entry.m_InheritedSource = tooling.m_TemporalAA ?
					HostViewValueSource::Tooling :
					HostViewValueSource::Content;
				break;
			case Block::GTAO:
				entry.m_InheritedSource = tooling.m_GTAO ?
					HostViewValueSource::Tooling :
					HostViewValueSource::Content;
				break;
			case Block::Exposure:
				entry.m_InheritedSource = HostViewValueSource::Camera;
				break;
			}
			entry.m_Source = entry.m_InheritedSource;
			const auto present = std::ranges::find(overrides, id, &LabParameterValue::m_Id);
			if (present != overrides.end())
			{
				entry.m_Override = present->m_Value;
				entry.m_Source = HostViewValueSource::Host;
			}
			if (patches.m_ForcedReference && binding.m_IsSet(*patches.m_ForcedReference))
			{
				entry.m_Source = HostViewValueSource::ForcedReference;
			}
			else if (patches.m_Sequence && binding.m_IsSet(*patches.m_Sequence))
			{
				entry.m_Source = HostViewValueSource::Sequence;
			}
		}
		return result;
	}
}
