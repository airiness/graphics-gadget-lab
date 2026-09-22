#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace gglab
{
	struct DirectionalShadowCascade
	{
		RenderView m_View{};
		RenderQueue m_RenderQueue{};
		// Positive main-camera view-space depths, independent of the light view depth range.
		float m_SplitNear = 0.0f;
		float m_SplitFar = 0.0f;
		DirectionalShadowProjectionInfo m_Projection{};
		DirectionalShadowResolvedBias m_Bias{};
	};

	// Frame-owned values. Cascade identity is its position in this set, not a
	// RenderViewID slot. No pointers into the containing frame survive a move.
	struct DirectionalShadowCascadeSet
	{
		// Assigned when flattening ViewGPU data; no cascade view index is valid until then.
		static constexpr uint32_t UnassignedViewBaseOffset =
			std::numeric_limits<uint32_t>::max();

		std::vector<DirectionalShadowCascade> m_Cascades;
		// Relative to SceneCB.ViewBaseIndex, assigned when flattening ViewGPU data.
		uint32_t m_ViewBaseOffset = UnassignedViewBaseOffset;

		[[nodiscard]] bool HasViewRange() const noexcept
		{
			return m_ViewBaseOffset != UnassignedViewBaseOffset;
		}

		// Null-safe cascade lookup for release paths that must not assume a count.
		[[nodiscard]] const DirectionalShadowCascade* TryGetCascade(
			uint32_t cascadeIndex) const noexcept
		{
			return cascadeIndex < m_Cascades.size() ? &m_Cascades[cascadeIndex] : nullptr;
		}

		[[nodiscard]] uint32_t GetViewIndex(uint32_t cascadeIndex) const noexcept
		{
			GGLAB_ASSERT(cascadeIndex < m_Cascades.size());
			GGLAB_ASSERT(HasViewRange());
			return m_ViewBaseOffset + cascadeIndex;
		}
	};
	[[nodiscard]] DirectionalShadowCascadeSet BuildDirectionalShadowCascades(
		const RenderView& mainView, const Vector3& lightDirection,
		const DirectionalShadowSettings& settings) noexcept;

	[[nodiscard]] DirectionalShadowGPU BuildDirectionalShadowGPU(
		const DirectionalShadowCascadeSet& cascades) noexcept;
}
