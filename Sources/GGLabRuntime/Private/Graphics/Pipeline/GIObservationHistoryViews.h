#pragma once

#include "Graphics/Pipeline/TemporalHistoryTextures.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

#include <array>
#include <cstddef>

namespace gglab
{
	// Graph-local policy projected from the observation, not another progress owner.
	struct GIObservationHistoryAccess
	{
		bool m_PreviousValid = false;
		bool m_Writing = false;
		bool m_BeginSweep = false;
	};

	template <size_t SurfaceCount>
	struct GIObservationHistoryViews
	{
		std::array<RGTextureViewId, SurfaceCount> m_Previous{};
		std::array<RGTextureViewId, SurfaceCount> m_Next{};
	};

	// Import, export, publication and resource retention remain with the observation.
	// Update the caller's next versions so its export validates these exact writes.
	template <size_t SurfaceCount>
	[[nodiscard]] GIObservationHistoryViews<SurfaceCount> DeclareGIObservationHistoryViews(
		RenderGraph::RGBuilder& builder,
		std::array<typename TemporalHistoryTextures<SurfaceCount>::RenderGraphSurface, SurfaceCount>& surfaces,
		const GIObservationHistoryAccess& access) noexcept
	{
		GIObservationHistoryViews<SurfaceCount> views;
		for (size_t index = 0; index < surfaces.size(); ++index)
		{
			if (access.m_PreviousValid)
			{
				views.m_Previous[index] = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(surfaces[index].m_Previous, RGTextureAccess::Sample, RHIStage::ComputeShader));
			}
			if (access.m_Writing)
			{
				auto& next = surfaces[index].m_Next;
				if (access.m_BeginSweep)
				{
					// The first tile defines the entire pending version, including untouched pixels.
					builder.WriteInPlace(next, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				}
				else
				{
					// Subsequent tiles must preserve the pending contents from earlier submissions.
					builder.ReadWriteInPlace(next, RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
				}
				views.m_Next[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(next);
			}
		}
		return views;
	}
}
