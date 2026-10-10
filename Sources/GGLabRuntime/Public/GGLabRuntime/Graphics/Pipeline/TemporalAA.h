#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gglab
{
	inline constexpr float TemporalAADepthAbsoluteThreshold = 0.05f;
	inline constexpr float TemporalAADepthRelativeThreshold = 0.02f;
	// History confidence falls linearly to zero at 1 / scale display pixels of motion per
	// frame. A pixel without motion keeps its full history weight. Effective-sample
	// accumulation also discounts the carried samples by the confidence, so its scale is
	// lower than the 0.1 that suited compatibility age. At 0.075 no evaluated region was
	// worse than compatibility age at 0.1, and moving content recovered faster after a stop.
	inline constexpr float TemporalAADefaultVelocityWeightScale = 0.075f;
	inline constexpr float TemporalAADefaultLuminanceWeightScale = 0.0f;
	inline constexpr float TemporalAADefaultNeighborhoodClampExpansion = 0.0f;
	// History alpha stores the accumulation state of its accumulation model; both start
	// at one and stay within [initial, max].
	inline constexpr float TemporalHistoryInitialAccumulation = 1.0f;
	inline constexpr float TemporalHistoryMaxAccumulation = 255.0f;
	inline constexpr float TemporalAADefaultMaxHistoryFeedback = 0.97f;
	inline constexpr float TemporalAAMaxHistoryFeedbackCeiling = 0.99f;
	inline constexpr float TemporalAAMaxDepthThreshold = 1.0f;
	inline constexpr float TemporalAAMaxVelocityWeightScale = 1.0f;
	inline constexpr float TemporalAAMaxLuminanceWeightScale = 16.0f;
	inline constexpr float TemporalAAMaxNeighborhoodClampExpansion = 1.0f;
	inline constexpr float TemporalAADefaultHistoryRelaxation = 1.0f;
	inline constexpr float TemporalAAMaxHistoryRelaxation = 4.0f;
	inline constexpr float TemporalAADefaultVarianceClipGamma = 1.0f;
	inline constexpr float TemporalAAMinVarianceClipGamma = 0.25f;
	inline constexpr float TemporalAAMaxVarianceClipGamma = 4.0f;
	// Against footprint-matched references -1 recovered most of the texture detail of
	// -1.5 with a smaller rise in static texture shimmer. It stacks with
	// log2(render / display).
	inline constexpr float TemporalAADefaultTextureLodBiasOffset = -1.0f;
	inline constexpr float TemporalAAMinTextureLodBiasOffset = -2.0f;
	inline constexpr float TemporalAAMaxTextureLodBiasOffset = 1.0f;
	inline constexpr uint32_t TemporalAAUnitRangePairMask = 0xffffu;
	inline constexpr float TemporalAAUnitRangeQuantizationScale =
		static_cast<float>(TemporalAAUnitRangePairMask);
	static_assert(TemporalHistoryMaxAccumulation <= 2048.0f);
	static_assert(TemporalAADefaultMaxHistoryFeedback >= 0.0f &&
		TemporalAADefaultMaxHistoryFeedback < TemporalAAMaxHistoryFeedbackCeiling);
	static_assert(TemporalAAMaxHistoryFeedbackCeiling < 1.0f);
	static_assert(TemporalAAMaxHistoryFeedbackCeiling <=
		TemporalHistoryMaxAccumulation / (TemporalHistoryMaxAccumulation + 1.0f));

	[[nodiscard]] constexpr uint32_t QuantizeTemporalAAUnitRange(float value) noexcept
	{
		return static_cast<uint32_t>(
			std::clamp(value, 0.0f, 1.0f) * TemporalAAUnitRangeQuantizationScale + 0.5f);
	}

	[[nodiscard]] constexpr uint32_t PackTemporalAAUnitRangePair(
		float low, float high) noexcept
	{
		return QuantizeTemporalAAUnitRange(low) |
			(QuantizeTemporalAAUnitRange(high) << 16u);
	}

	[[nodiscard]] constexpr uint32_t PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
		float maxHistoryFeedback, float clampExpansion) noexcept
	{
		const uint32_t feedback = std::min(
			QuantizeTemporalAAUnitRange(maxHistoryFeedback),
			TemporalAAUnitRangePairMask - 1u);
		return feedback | (QuantizeTemporalAAUnitRange(clampExpansion) << 16u);
	}

	[[nodiscard]] constexpr uint32_t PackTemporalAALuminanceWeightAndHistoryRelaxation(
		float luminanceWeightScale, float historyRelaxation) noexcept
	{
		return PackTemporalAAUnitRangePair(
			luminanceWeightScale / TemporalAAMaxLuminanceWeightScale,
			historyRelaxation / TemporalAAMaxHistoryRelaxation);
	}

	[[nodiscard]] constexpr std::array<float, 2> UnpackTemporalAAUnitRangePair(
		uint32_t packedValues) noexcept
	{
		return {
			static_cast<float>(packedValues & TemporalAAUnitRangePairMask) /
				TemporalAAUnitRangeQuantizationScale,
			static_cast<float>(packedValues >> 16u) /
				TemporalAAUnitRangeQuantizationScale,
		};
	}

	[[nodiscard]] inline float ResolveTemporalAAFeedbackSaturationAge(
		float maxHistoryFeedback) noexcept
	{
		if (!std::isfinite(maxHistoryFeedback))
		{
			return TemporalHistoryInitialAccumulation;
		}

		const float maxPackedFeedback =
			static_cast<float>(TemporalAAUnitRangePairMask - 1u) /
			TemporalAAUnitRangeQuantizationScale;
		const float feedback = std::clamp(maxHistoryFeedback, 0.0f, maxPackedFeedback);
		float saturationAge = std::max(std::ceil(feedback / std::max(
			1.0f - feedback, 1.0f / TemporalAAUnitRangeQuantizationScale)),
			TemporalHistoryInitialAccumulation);
		// Correct ceil() at exactly representable discrete weights such as 99 / 100.
		// The diagnostic must report the first integer age whose actual float weight
		// reaches the cap, rather than inheriting division-rounding error from the
		// closed-form estimate.
		while (saturationAge > TemporalHistoryInitialAccumulation &&
			(saturationAge - 1.0f) / saturationAge >= feedback)
		{
			saturationAge -= 1.0f;
		}
		while (saturationAge / (saturationAge + 1.0f) < feedback)
		{
			saturationAge += 1.0f;
		}
		return saturationAge;
	}

	// Sample bound of effective-sample accumulation: the count whose weight N / (N + 1)
	// equals the feedback ceiling, so the ceiling and the carried samples share one bound.
	[[nodiscard]] inline float ResolveTemporalAAMaxHistorySamples(
		float maxHistoryFeedback) noexcept
	{
		if (!std::isfinite(maxHistoryFeedback))
		{
			return TemporalHistoryInitialAccumulation;
		}
		const float maxPackedFeedback =
			static_cast<float>(TemporalAAUnitRangePairMask - 1u) /
			TemporalAAUnitRangeQuantizationScale;
		const float feedback = std::clamp(maxHistoryFeedback, 0.0f, maxPackedFeedback);
		return std::clamp(feedback / (1.0f - feedback), TemporalHistoryInitialAccumulation,
			TemporalHistoryMaxAccumulation);
	}

	// What the history alpha accumulates. Both models weight accepted history by
	// min(N / (N + 1), feedback) times its confidence; they differ in the N they carry.
	// Switching the model resets history: the stored state of one is not the other's.
	enum class TemporalAAHistoryAccumulation : uint8_t
	{
		// Kept for comparison: consecutive accepted frames. Low-confidence frames lower
		// their own weight but not the age, so the weight returns to the ceiling as soon
		// as the confidence does, over history accumulated while it was low.
		CompatibilityAge,
		// Effective sample count: the confidence discounts the carried samples and the
		// current frame adds one, N' = min(confidence * N + 1, feedback / (1 - feedback)).
		// After a camera stop its reference error came within 2% of the converged error 28
		// frames later at native resolution and 8 at Quality, against 52 and 44.
		EffectiveSamples,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAAHistoryAccumulationName(
		TemporalAAHistoryAccumulation accumulation) noexcept
	{
		switch (accumulation)
		{
		case TemporalAAHistoryAccumulation::CompatibilityAge: return "compatibility-age";
		case TemporalAAHistoryAccumulation::EffectiveSamples: return "effective-samples";
		}
		return "unknown";
	}

	// Filter that resamples the previous history color at the reprojected position.
	// A non-integer reprojection filters the accumulated signal again every frame, so
	// the filter's low-pass response compounds over the history lifetime.
	enum class TemporalAAHistoryFilter : uint8_t
	{
		// Kept for comparison: its blur compounds under motion.
		Bilinear,
		// Five-tap approximation of the 4x4 Catmull-Rom kernel, limited to the range of
		// the 2x2 history texels around the position. The limit removes overshoot of
		// the negative lobes beyond the bilinear footprint, such as dark halos around
		// HDR highlights.
		CatmullRomClamped,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAAHistoryFilterName(
		TemporalAAHistoryFilter filter) noexcept
	{
		switch (filter)
		{
		case TemporalAAHistoryFilter::Bilinear: return "bilinear";
		case TemporalAAHistoryFilter::CatmullRomClamped: return "catmull-rom-clamped";
		}
		return "unknown";
	}

	// Reconstruction of the current frame's color at the output pixel centre. The scene
	// is rendered with a sub-pixel jitter, so a point sample sits off the centre by
	// that jitter each frame.
	enum class TemporalAACurrentFilter : uint8_t
	{
		// Kept for comparison: the output follows the jitter phase of each frame.
		Point,
		// Gaussian approximation of Blackman-Harris, exp(-2.29 (d / 0.75)^2), over the
		// 3x3 samples at their jittered positions. Sizes of 0.6 and 1.0 pixels bracket
		// it: narrower lost half of the stability gain, wider blurred thin detail.
		Gaussian,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAACurrentFilterName(
		TemporalAACurrentFilter filter) noexcept
	{
		switch (filter)
		{
		case TemporalAACurrentFilter::Point: return "point";
		case TemporalAACurrentFilter::Gaussian: return "gaussian";
		}
		return "unknown";
	}

	// Which current-frame sample supplies the motion that reprojects a pixel. At a
	// silhouette the centre sample can belong to the background while the edge belongs
	// to the foreground (or the reverse).
	enum class TemporalAAMotionSelection : uint8_t
	{
		// Kept for comparison.
		Center,
		// Motion of the front-most depth sample of the 3x3 neighborhood, with depth
		// validation of that sample at its own position, so motion and validation
		// describe one surface. Validating the centre instead keeps the motion gain
		// but loses the edge stability: background pixels beside
		// geometry then fail background validation whenever the jitter puts geometry
		// under them in the previous frame, resetting their history.
		ClosestDepth,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAAMotionSelectionName(
		TemporalAAMotionSelection selection) noexcept
	{
		switch (selection)
		{
		case TemporalAAMotionSelection::Center: return "center";
		case TemporalAAMotionSelection::ClosestDepth: return "closest-depth";
		}
		return "unknown";
	}

	// Projection of the display raster view with which post-temporal composition draws
	// (transparent, depth-tested debug and post-TAA scene-extension geometry) while
	// Temporal AA owns the jitter. The resolve does not integrate that geometry, so a
	// jittered view moves it by the sub-pixel jitter every frame. A reference frame keeps
	// it jittered: its average removes the jitter of everything it accumulates.
	enum class TemporalAAPostTemporalView : uint8_t
	{
		// Kept for comparison: post-temporal geometry shimmers with the jitter.
		Jittered,
		// Stable post-temporal geometry; its edges can sit up to half a pixel from the
		// jittered scene depth they test against. Against the jittered view it halved the
		// static shimmer of glass guard edges and did not raise reference error or motion
		// flicker in any evaluated region.
		Unjittered,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAAPostTemporalViewName(
		TemporalAAPostTemporalView view) noexcept
	{
		switch (view)
		{
		case TemporalAAPostTemporalView::Jittered: return "jittered";
		case TemporalAAPostTemporalView::Unjittered: return "unjittered";
		}
		return "unknown";
	}

	// Render resolution of the display view relative to its display extent while the
	// Temporal AA resolve upscales. The preset is fixed for a session; changing it
	// changes the render extent, which resets history.
	enum class TemporalAAResolutionPreset : uint8_t
	{
		Native,
		// Render extent 2/3 of the display extent (scale 1 / 1.5).
		Quality,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAAResolutionPresetName(
		TemporalAAResolutionPreset preset) noexcept
	{
		switch (preset)
		{
		case TemporalAAResolutionPreset::Native: return "native";
		case TemporalAAResolutionPreset::Quality: return "quality";
		}
		return "unknown";
	}

	// Render extent over display extent as an exact ratio, so that extents and jitter
	// sequence lengths derive without floating-point rounding.
	struct TemporalAARenderScale
	{
		uint32_t m_Numerator = 1;
		uint32_t m_Denominator = 1;

		bool operator==(const TemporalAARenderScale&) const noexcept = default;
	};

	[[nodiscard]] constexpr TemporalAARenderScale GetTemporalAARenderScale(
		TemporalAAResolutionPreset preset) noexcept
	{
		return preset == TemporalAAResolutionPreset::Quality
			? TemporalAARenderScale{ .m_Numerator = 2, .m_Denominator = 3 }
			: TemporalAARenderScale{};
	}

	// Render extent of one display dimension: rounded to the nearest pixel, at least one.
	[[nodiscard]] constexpr uint32_t ScaleTemporalAAExtent(
		uint32_t displayExtent, TemporalAARenderScale scale) noexcept
	{
		const uint64_t scaled = (static_cast<uint64_t>(displayExtent) * scale.m_Numerator +
			scale.m_Denominator / 2u) / scale.m_Denominator;
		return std::max(static_cast<uint32_t>(scaled), displayExtent > 0 ? 1u : 0u);
	}

	[[nodiscard]] constexpr ViewResolution ResolveTemporalAAViewResolution(
		ViewExtent display, TemporalAAResolutionPreset preset) noexcept
	{
		const TemporalAARenderScale scale = GetTemporalAARenderScale(preset);
		return ViewResolution{
			.m_Render = { ScaleTemporalAAExtent(display.m_Width, scale),
				ScaleTemporalAAExtent(display.m_Height, scale) },
			.m_Display = display,
		};
	}

	// Distinct jitter phases per display pixel grow with the inverse pixel area of the
	// render grid: ceil(8 / r^2) render-pixel Halton(2, 3) samples, 8 at native and 18
	// at Quality.
	[[nodiscard]] constexpr uint32_t GetTemporalAAJitterSequenceLength(
		TemporalAAResolutionPreset preset) noexcept
	{
		const TemporalAARenderScale scale = GetTemporalAARenderScale(preset);
		const uint32_t numeratorSquared = scale.m_Numerator * scale.m_Numerator;
		return (8u * scale.m_Denominator * scale.m_Denominator + numeratorSquared - 1u) /
			numeratorSquared;
	}
	static_assert(GetTemporalAAJitterSequenceLength(TemporalAAResolutionPreset::Native) == 8);
	static_assert(GetTemporalAAJitterSequenceLength(TemporalAAResolutionPreset::Quality) == 18);

	// How accepted history is made compatible with the current local signal, from the
	// YCoCg statistics of the current samples around the output position.
	enum class TemporalAAHistoryRectification : uint8_t
	{
		// Per-channel clamp to the neighborhood minimum and maximum, expanded by the
		// clamp expansion.
		MinMaxClamp,
		// Moves history along the line toward the neighborhood mean until it lies within
		// mean +- gamma * standard deviation.
		VarianceClip,
		// Variance clipping against that box intersected with the expanded min/max box.
		BoundedVarianceClip,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalAAHistoryRectificationName(
		TemporalAAHistoryRectification rectification) noexcept
	{
		switch (rectification)
		{
		case TemporalAAHistoryRectification::MinMaxClamp: return "minmax-clamp";
		case TemporalAAHistoryRectification::VarianceClip: return "variance-clip";
		case TemporalAAHistoryRectification::BoundedVarianceClip: return "bounded-variance-clip";
		}
		return "unknown";
	}

	struct TemporalAASettings
	{
		bool m_Enabled = false;
		float m_DepthAbsoluteThreshold = TemporalAADepthAbsoluteThreshold;
		float m_DepthRelativeThreshold = TemporalAADepthRelativeThreshold;
		float m_MaxHistoryFeedback = TemporalAADefaultMaxHistoryFeedback;
		float m_VelocityWeightScale = TemporalAADefaultVelocityWeightScale;
		float m_LuminanceWeightScale = TemporalAADefaultLuminanceWeightScale;
		float m_NeighborhoodClampExpansion = TemporalAADefaultNeighborhoodClampExpansion;
		// Further min/max box expansion, in box extents per side, for history whose
		// disagreement with the current frame keeps changing sign, as jitter aliasing does,
		// in proportion to how close its samples are to their bound. A disagreement that
		// keeps one sign, such as a lighting change, withdraws it; sky never relaxes.
		float m_HistoryRelaxation = TemporalAADefaultHistoryRelaxation;
		TemporalAAHistoryAccumulation m_HistoryAccumulation =
			TemporalAAHistoryAccumulation::EffectiveSamples;
		TemporalAAHistoryRectification m_HistoryRectification =
			TemporalAAHistoryRectification::MinMaxClamp;
		float m_VarianceClipGamma = TemporalAADefaultVarianceClipGamma;
		TemporalAAHistoryFilter m_HistoryFilter = TemporalAAHistoryFilter::CatmullRomClamped;
		TemporalAACurrentFilter m_CurrentFilter = TemporalAACurrentFilter::Gaussian;
		TemporalAAMotionSelection m_MotionSelection = TemporalAAMotionSelection::ClosestDepth;
		TemporalAAPostTemporalView m_PostTemporalView = TemporalAAPostTemporalView::Unjittered;
		// Requested render resolution; it applies only while the Temporal AA consumer is
		// active and the pipeline's resolve can upscale.
		TemporalAAResolutionPreset m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		// Material texture LOD offset while Temporal AA is active, added to
		// log2(render / display).
		float m_TextureLodBiasOffset = TemporalAADefaultTextureLodBiasOffset;

		bool operator==(const TemporalAASettings&) const noexcept = default;
	};

	[[nodiscard]] inline TemporalAASettings ResolveTemporalAASettings(
		TemporalAASettings settings) noexcept
	{
		const TemporalAASettings defaults{};
		settings.m_DepthAbsoluteThreshold = std::isfinite(settings.m_DepthAbsoluteThreshold)
			? std::clamp(settings.m_DepthAbsoluteThreshold, 0.0f,
				TemporalAAMaxDepthThreshold)
			: defaults.m_DepthAbsoluteThreshold;
		settings.m_DepthRelativeThreshold = std::isfinite(settings.m_DepthRelativeThreshold)
			? std::clamp(settings.m_DepthRelativeThreshold, 0.0f,
				TemporalAAMaxDepthThreshold)
			: defaults.m_DepthRelativeThreshold;
		settings.m_MaxHistoryFeedback = std::isfinite(settings.m_MaxHistoryFeedback)
			? std::clamp(settings.m_MaxHistoryFeedback, 0.0f,
				TemporalAAMaxHistoryFeedbackCeiling)
			: defaults.m_MaxHistoryFeedback;
		settings.m_VelocityWeightScale = std::isfinite(settings.m_VelocityWeightScale)
			? std::clamp(settings.m_VelocityWeightScale, 0.0f,
				TemporalAAMaxVelocityWeightScale)
			: defaults.m_VelocityWeightScale;
		settings.m_LuminanceWeightScale = std::isfinite(settings.m_LuminanceWeightScale)
			? std::clamp(settings.m_LuminanceWeightScale, 0.0f,
				TemporalAAMaxLuminanceWeightScale)
			: defaults.m_LuminanceWeightScale;
		settings.m_NeighborhoodClampExpansion =
			std::isfinite(settings.m_NeighborhoodClampExpansion)
			? std::clamp(settings.m_NeighborhoodClampExpansion, 0.0f,
				TemporalAAMaxNeighborhoodClampExpansion)
			: defaults.m_NeighborhoodClampExpansion;
		settings.m_HistoryRelaxation = std::isfinite(settings.m_HistoryRelaxation)
			? std::clamp(settings.m_HistoryRelaxation, 0.0f, TemporalAAMaxHistoryRelaxation)
			: defaults.m_HistoryRelaxation;
		if (settings.m_HistoryFilter != TemporalAAHistoryFilter::Bilinear &&
			settings.m_HistoryFilter != TemporalAAHistoryFilter::CatmullRomClamped)
		{
			settings.m_HistoryFilter = defaults.m_HistoryFilter;
		}
		if (settings.m_CurrentFilter != TemporalAACurrentFilter::Point &&
			settings.m_CurrentFilter != TemporalAACurrentFilter::Gaussian)
		{
			settings.m_CurrentFilter = defaults.m_CurrentFilter;
		}
		if (settings.m_MotionSelection != TemporalAAMotionSelection::Center &&
			settings.m_MotionSelection != TemporalAAMotionSelection::ClosestDepth)
		{
			settings.m_MotionSelection = defaults.m_MotionSelection;
		}
		if (settings.m_PostTemporalView != TemporalAAPostTemporalView::Jittered &&
			settings.m_PostTemporalView != TemporalAAPostTemporalView::Unjittered)
		{
			settings.m_PostTemporalView = defaults.m_PostTemporalView;
		}
		if (settings.m_ResolutionPreset != TemporalAAResolutionPreset::Native &&
			settings.m_ResolutionPreset != TemporalAAResolutionPreset::Quality)
		{
			settings.m_ResolutionPreset = defaults.m_ResolutionPreset;
		}
		if (settings.m_HistoryAccumulation != TemporalAAHistoryAccumulation::CompatibilityAge &&
			settings.m_HistoryAccumulation != TemporalAAHistoryAccumulation::EffectiveSamples)
		{
			settings.m_HistoryAccumulation = defaults.m_HistoryAccumulation;
		}
		if (settings.m_HistoryRectification != TemporalAAHistoryRectification::MinMaxClamp &&
			settings.m_HistoryRectification != TemporalAAHistoryRectification::VarianceClip &&
			settings.m_HistoryRectification != TemporalAAHistoryRectification::BoundedVarianceClip)
		{
			settings.m_HistoryRectification = defaults.m_HistoryRectification;
		}
		settings.m_VarianceClipGamma = std::isfinite(settings.m_VarianceClipGamma)
			? std::clamp(settings.m_VarianceClipGamma, TemporalAAMinVarianceClipGamma,
				TemporalAAMaxVarianceClipGamma)
			: defaults.m_VarianceClipGamma;
		settings.m_TextureLodBiasOffset = std::isfinite(settings.m_TextureLodBiasOffset)
			? std::clamp(settings.m_TextureLodBiasOffset, TemporalAAMinTextureLodBiasOffset,
				TemporalAAMaxTextureLodBiasOffset)
			: defaults.m_TextureLodBiasOffset;
		return settings;
	}

	enum class SceneExtensionTemporalParticipation : uint8_t
	{
		TemporalIntegrated,
		PostTAA,
		TemporalUnsupported,
	};

	struct TemporalAACapabilityStatus
	{
		bool m_MotionRenderTarget = false;
		bool m_MotionShaderResource = false;
		bool m_ResolvedColorRenderTarget = false;
		bool m_ResolvedColorShaderResource = false;
		bool m_ResolvedColorTypedUavStore = false;
		bool m_HistoryColorShaderResource = false;
		bool m_HistoryColorTypedUavStore = false;
		bool m_HistoryDepthShaderResource = false;
		bool m_HistoryDepthTypedUavStore = false;
		bool m_HistoryReliabilityShaderResource = false;
		bool m_HistoryReliabilityTypedUavStore = false;

		// Device format support only. Required programs and pipeline closures are frame
		// contracts of the selected pipeline, never a reason to disable a requested TAA.
		[[nodiscard]] constexpr bool IsCoreAvailable() const noexcept
		{
			return m_MotionRenderTarget && m_MotionShaderResource &&
				m_ResolvedColorRenderTarget && m_ResolvedColorShaderResource &&
				m_ResolvedColorTypedUavStore && m_HistoryColorShaderResource &&
				m_HistoryColorTypedUavStore && m_HistoryDepthShaderResource &&
				m_HistoryDepthTypedUavStore && m_HistoryReliabilityShaderResource &&
				m_HistoryReliabilityTypedUavStore;
		}

		bool operator==(const TemporalAACapabilityStatus&) const noexcept = default;
	};

	// Temporal consumers of the display view. Each consumer resolves its own eligibility;
	// only an active consumer contributes the services it requires to the frame plan.
	enum class TemporalConsumer : uint8_t
	{
		// Temporal AA resolve of the display color.
		TemporalAA,
		// Supersampled reference accumulation of an evaluation sequence frame.
		Reference,
		Count,
	};
	inline constexpr uint32_t TemporalConsumerCount =
		static_cast<uint32_t>(TemporalConsumer::Count);

	[[nodiscard]] constexpr std::string_view GetTemporalConsumerName(
		TemporalConsumer consumer) noexcept
	{
		switch (consumer)
		{
		case TemporalConsumer::TemporalAA: return "temporal-aa";
		case TemporalConsumer::Reference: return "reference";
		case TemporalConsumer::Count: break;
		}
		return "unknown";
	}

	// Per-frame temporal services. A frame enables exactly the union of the services that
	// its active consumers require, all together or not at all.
	enum class TemporalService : uint8_t
	{
		None = 0u,
		// Sub-pixel jitter of the display view's raster projection. Only a consumer that
		// owns a resolve removing the jitter requires it.
		ProjectionJitter = 1u << 0,
		// Raster motion vectors written with the depth prepass.
		GeometryMotion = 1u << 1,
		// Submitted-frame continuity: previous view and object state and the frame index,
		// committed only after a successful submission.
		FrameContinuity = 1u << 2,
		// Persistent display color and depth history of the Temporal AA resolve.
		ColorDepthHistory = 1u << 3,
	};
	GGLAB_ENUM_FLAGS(TemporalService);
	inline constexpr std::array<TemporalService, 4> TemporalServices{
		TemporalService::ProjectionJitter,
		TemporalService::GeometryMotion,
		TemporalService::FrameContinuity,
		TemporalService::ColorDepthHistory,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalServiceName(
		TemporalService service) noexcept
	{
		switch (service)
		{
		case TemporalService::ProjectionJitter: return "projection-jitter";
		case TemporalService::GeometryMotion: return "geometry-motion";
		case TemporalService::FrameContinuity: return "frame-continuity";
		case TemporalService::ColorDepthHistory: return "color-depth-history";
		default: break;
		}
		return "unknown";
	}

	[[nodiscard]] constexpr TemporalService GetTemporalConsumerRequiredServices(
		TemporalConsumer consumer) noexcept
	{
		switch (consumer)
		{
		case TemporalConsumer::TemporalAA:
			return TemporalService::ProjectionJitter | TemporalService::GeometryMotion |
				TemporalService::FrameContinuity | TemporalService::ColorDepthHistory;
		case TemporalConsumer::Reference:
			// The reference owns a separate jitter sequence and removes it by averaging;
			// time is held, so it needs no motion, continuity or history.
			return TemporalService::ProjectionJitter;
		case TemporalConsumer::Count: break;
		}
		return TemporalService::None;
	}

	enum class TemporalConsumerStatus : uint8_t
	{
		Disabled,
		Unavailable,
		Active,
	};

	// Why a consumer contributes no services. Unavailable reasons are expected states of
	// the device, view or pipeline, not contract failures.
	enum class TemporalConsumerDisableReason : uint8_t
	{
		None,
		NotRequested,
		CoreCapabilityUnavailable,
		DisplayViewIneligible,
		DepthVelocityPathUnavailable,
		SceneExtensionUnsupported,
	};

	[[nodiscard]] constexpr std::string_view GetTemporalConsumerStatusName(
		TemporalConsumerStatus status) noexcept
	{
		switch (status)
		{
		case TemporalConsumerStatus::Disabled: return "disabled";
		case TemporalConsumerStatus::Unavailable: return "unavailable";
		case TemporalConsumerStatus::Active: return "active";
		}
		return "unknown";
	}

	[[nodiscard]] constexpr std::string_view GetTemporalConsumerDisableReasonName(
		TemporalConsumerDisableReason reason) noexcept
	{
		switch (reason)
		{
		case TemporalConsumerDisableReason::None: return "none";
		case TemporalConsumerDisableReason::NotRequested: return "not-requested";
		case TemporalConsumerDisableReason::CoreCapabilityUnavailable:
			return "core-capability-unavailable";
		case TemporalConsumerDisableReason::DisplayViewIneligible:
			return "display-view-ineligible";
		case TemporalConsumerDisableReason::DepthVelocityPathUnavailable:
			return "depth-velocity-path-unavailable";
		case TemporalConsumerDisableReason::SceneExtensionUnsupported:
			return "scene-extension-unsupported";
		}
		return "unknown";
	}

	struct TemporalConsumerPlan
	{
		TemporalConsumerStatus m_Status = TemporalConsumerStatus::Disabled;
		TemporalConsumerDisableReason m_DisableReason =
			TemporalConsumerDisableReason::NotRequested;
		// The consumer's required services while it is active; none otherwise.
		TemporalService m_Services = TemporalService::None;
		bool m_Requested = false;

		[[nodiscard]] constexpr bool IsActive() const noexcept
		{
			return m_Status == TemporalConsumerStatus::Active;
		}

		bool operator==(const TemporalConsumerPlan&) const noexcept = default;
	};

	struct TemporalFramePlanResolveInfo
	{
		TemporalAASettings m_Settings{};
		TemporalAACapabilityStatus m_Capabilities{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		SceneExtensionTemporalParticipation m_SceneExtensionParticipation =
			SceneExtensionTemporalParticipation::TemporalUnsupported;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		bool m_DisplayViewEligible = false;
		bool m_DepthVelocityPathAvailable = false;
		// The pipeline's resolve reconstructs the display extent from a smaller render
		// extent; without it every frame renders at native resolution.
		bool m_TemporalUpscalingAvailable = false;
		// A supersampled reference sample is due this frame. It excludes Temporal AA.
		bool m_ReferenceRequested = false;
	};

	// The display view's temporal plan, resolved once per frame from its consumers.
	struct ResolvedTemporalFramePlan
	{
		TemporalAACapabilityStatus m_Capabilities{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		SceneExtensionTemporalParticipation m_SceneExtensionParticipation =
			SceneExtensionTemporalParticipation::TemporalUnsupported;
		std::array<TemporalConsumerPlan, TemporalConsumerCount> m_Consumers{};
		// Union of the active consumers' services.
		TemporalService m_Services = TemporalService::None;
		// Effective render resolution of the display view: the requested preset while the
		// Temporal AA consumer is active and the resolve can upscale, native otherwise.
		TemporalAAResolutionPreset m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		// Accumulation model of the Temporal AA color history while that consumer is active.
		TemporalAAHistoryAccumulation m_HistoryAccumulation =
			TemporalAAHistoryAccumulation::EffectiveSamples;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		bool m_CoreAvailable = false;
		bool m_DisplayViewEligible = false;
		bool m_DepthVelocityPathAvailable = false;

		[[nodiscard]] constexpr const TemporalConsumerPlan& GetConsumer(
			TemporalConsumer consumer) const noexcept
		{
			return m_Consumers[static_cast<uint32_t>(consumer)];
		}

		[[nodiscard]] constexpr bool IsConsumerActive(TemporalConsumer consumer) const noexcept
		{
			return GetConsumer(consumer).IsActive();
		}

		[[nodiscard]] constexpr bool HasService(TemporalService service) const noexcept
		{
			return Test(m_Services, service);
		}

		// The active consumer whose resolve removes the projection jitter, if any.
		[[nodiscard]] constexpr std::optional<TemporalConsumer> GetProjectionJitterOwner()
			const noexcept
		{
			for (uint32_t index = 0; index < TemporalConsumerCount; ++index)
			{
				if (Test(m_Consumers[index].m_Services, TemporalService::ProjectionJitter))
				{
					return static_cast<TemporalConsumer>(index);
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] constexpr uint32_t GetJitterSequenceLength() const noexcept
		{
			return GetTemporalAAJitterSequenceLength(m_ResolutionPreset);
		}

		bool operator==(const ResolvedTemporalFramePlan&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsTemporalAADisplayViewEligible(
		RenderViewID viewId, uint32_t width, uint32_t height) noexcept
	{
		return width > 0 && height > 0 &&
			(viewId == RenderViewID::Main || IsDebugCameraRenderViewID(viewId));
	}

	[[nodiscard]] constexpr TemporalConsumerPlan ResolveTemporalAAConsumerPlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		TemporalConsumerPlan consumer{ .m_Requested = info.m_Settings.m_Enabled };
		if (!consumer.m_Requested)
		{
			return consumer;
		}

		consumer.m_Status = TemporalConsumerStatus::Unavailable;
		if (!info.m_Capabilities.IsCoreAvailable())
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::CoreCapabilityUnavailable;
			return consumer;
		}
		if (!info.m_DisplayViewEligible)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::DisplayViewIneligible;
			return consumer;
		}
		if (!info.m_DepthVelocityPathAvailable)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::DepthVelocityPathUnavailable;
			return consumer;
		}
		if (info.m_SceneExtensionParticipation ==
			SceneExtensionTemporalParticipation::TemporalUnsupported)
		{
			consumer.m_DisableReason = TemporalConsumerDisableReason::SceneExtensionUnsupported;
			return consumer;
		}

		consumer.m_Status = TemporalConsumerStatus::Active;
		consumer.m_DisableReason = TemporalConsumerDisableReason::None;
		consumer.m_Services = GetTemporalConsumerRequiredServices(TemporalConsumer::TemporalAA);
		return consumer;
	}

	// The reference has no eligibility gate: a requested sample that cannot be
	// accumulated is a frame contract failure of the pipeline.
	[[nodiscard]] constexpr TemporalConsumerPlan ResolveTemporalReferenceConsumerPlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		if (!info.m_ReferenceRequested)
		{
			return {};
		}
		return {
			.m_Status = TemporalConsumerStatus::Active,
			.m_DisableReason = TemporalConsumerDisableReason::None,
			.m_Services = GetTemporalConsumerRequiredServices(TemporalConsumer::Reference),
			.m_Requested = true,
		};
	}

	[[nodiscard]] inline ResolvedTemporalFramePlan ResolveTemporalFramePlan(
		const TemporalFramePlanResolveInfo& info) noexcept
	{
		// Both consumers own a jitter sequence; a reference frame holds Temporal AA off.
		GGLAB_ASSERT_MSG(!info.m_ReferenceRequested || !info.m_Settings.m_Enabled,
			"A temporal reference sample requires Temporal AA to be unrequested.");
		ResolvedTemporalFramePlan plan{
			.m_Capabilities = info.m_Capabilities,
			.m_DisplayViewId = info.m_DisplayViewId,
			.m_SceneExtensionParticipation = info.m_SceneExtensionParticipation,
			.m_ResetIdentity = info.m_ResetIdentity,
			.m_SessionIdentity = info.m_SessionIdentity,
			.m_CoreAvailable = info.m_Capabilities.IsCoreAvailable(),
			.m_DisplayViewEligible = info.m_DisplayViewEligible,
			.m_DepthVelocityPathAvailable = info.m_DepthVelocityPathAvailable,
		};
		plan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::TemporalAA)] =
			ResolveTemporalAAConsumerPlan(info);
		plan.m_Consumers[static_cast<uint32_t>(TemporalConsumer::Reference)] =
			ResolveTemporalReferenceConsumerPlan(info);
		uint32_t jitterOwnerCount = 0;
		for (const TemporalConsumerPlan& consumer : plan.m_Consumers)
		{
			plan.m_Services |= consumer.m_Services;
			jitterOwnerCount +=
				Test(consumer.m_Services, TemporalService::ProjectionJitter) ? 1u : 0u;
		}
		GGLAB_ASSERT_MSG(jitterOwnerCount <= 1,
			"At most one active temporal consumer owns the projection jitter.");
		if (plan.IsConsumerActive(TemporalConsumer::TemporalAA))
		{
			plan.m_HistoryAccumulation = info.m_Settings.m_HistoryAccumulation;
			if (info.m_TemporalUpscalingAvailable)
			{
				plan.m_ResolutionPreset = info.m_Settings.m_ResolutionPreset;
			}
		}
		return plan;
	}

	namespace temporal
	{
		// Jitter sequence length at native resolution.
		inline constexpr uint32_t JitterSampleCount =
			GetTemporalAAJitterSequenceLength(TemporalAAResolutionPreset::Native);

		[[nodiscard]] constexpr double HaltonRadicalInverse(uint32_t index, uint32_t base) noexcept
		{
			double fraction = 1.0;
			double result = 0.0;
			while (index > 0)
			{
				fraction /= static_cast<double>(base);
				result += fraction * static_cast<double>(index % base);
				index /= base;
			}
			return result;
		}

		// Halton(2, 3) indices 1..length, centred on the pixel, in render pixels.
		[[nodiscard]] constexpr Vector2 GetJitterSamplePixels(
			uint32_t sequenceIndex, uint32_t sequenceLength = JitterSampleCount) noexcept
		{
			const uint32_t index = sequenceIndex % std::max(sequenceLength, 1u) + 1u;
			return Vector2(static_cast<float>(HaltonRadicalInverse(index, 2) - 0.5),
				static_cast<float>(HaltonRadicalInverse(index, 3) - 0.5));
		}

		[[nodiscard]] inline Vector2 JitterPixelsToUV(
			const Vector2& jitterPixels, uint32_t width, uint32_t height) noexcept
		{
			const float safeWidth = static_cast<float>(std::max(width, 1u));
			const float safeHeight = static_cast<float>(std::max(height, 1u));
			return Vector2(jitterPixels.m_X / safeWidth, jitterPixels.m_Y / safeHeight);
		}

		[[nodiscard]] inline Vector2 JitterPixelsToNDC(
			const Vector2& jitterPixels, uint32_t width, uint32_t height) noexcept
		{
			const Vector2 jitterUV = JitterPixelsToUV(jitterPixels, width, height);
			return Vector2(2.0f * jitterUV.m_X, -2.0f * jitterUV.m_Y);
		}

		[[nodiscard]] inline Vector4 ApplyJitterToClipPosition(
			const Vector4& unjitteredClip, const Vector2& jitterNDC) noexcept
		{
			return Vector4(unjitteredClip.m_X + jitterNDC.m_X * unjitteredClip.m_W,
				unjitteredClip.m_Y + jitterNDC.m_Y * unjitteredClip.m_W,
				unjitteredClip.m_Z, unjitteredClip.m_W);
		}

		[[nodiscard]] inline Vector2 ReprojectToPreviousUV(
			const Vector2& currentUV, const Vector2& motionUV) noexcept
		{
			return currentUV - motionUV;
		}
	}
}
