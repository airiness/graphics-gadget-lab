#include "Graphics/Pipeline/GTAO.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace gglab
{
	GTAOSurfaceSelection SelectGTAOHalfResolutionSurface(
		const std::array<GTAOSurfaceCandidate, 4>& candidates,
		DepthConvention convention) noexcept
	{
		GTAOSurfaceSelection selection{};
		for (uint32_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
		{
			const auto& candidate = candidates[candidateIndex];
			if (screen_space::IsDepthBackground(candidate.m_RawDepth, convention) ||
				!std::isfinite(candidate.m_ViewZ) || candidate.m_ViewZ <= 0.0f)
			{
				continue;
			}

			if (!selection.m_IsValid ||
				screen_space::IsDepthNearer(
					candidate.m_RawDepth, selection.m_RawDepth, convention))
			{
				selection = {
					.m_SelectedIndex = candidateIndex,
					.m_RawDepth = candidate.m_RawDepth,
					.m_ViewZ = candidate.m_ViewZ,
					.m_IsValid = true,
				};
			}
		}
		return selection;
	}

	float GTAOInterleavedGradientNoise(uint32_t pixelX, uint32_t pixelY) noexcept
	{
		const auto fract = [](float value) noexcept { return value - std::floor(value); };
		return fract(52.9829189f *
			fract(0.06711056f * static_cast<float>(pixelX) +
				0.00583715f * static_cast<float>(pixelY)));
	}
}
