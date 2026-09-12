#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/RenderSceneBuilder.h"

namespace gglab
{
	// GPU resources adopted by the renderer for one active frame. The frame
	// context exposes only the CPU scene value; upload-fence ownership and the
	// scene GPU allocation handoff stay inside the renderer.
	struct RenderFrameGpuResources
	{
		RHIFencePoint m_UploadFencePoint{};
		RenderSceneGpuAllocations m_SceneGpuAllocations{};

		void AdoptFrom(RenderSceneGpuAllocations& sourceAllocations,
			const RHIFencePoint& uploadFencePoint) noexcept;
		bool IsEmpty() const noexcept
		{
			return !m_UploadFencePoint.IsValid() && m_SceneGpuAllocations.IsEmpty();
		}
		void Reset() noexcept { *this = {}; }
	};

	inline void RenderFrameGpuResources::AdoptFrom(
		RenderSceneGpuAllocations& sourceAllocations,
		const RHIFencePoint& uploadFencePoint) noexcept
	{
		if (uploadFencePoint.IsValid())
		{
			GGLAB_ASSERT_MSG(!m_UploadFencePoint.IsValid() ||
				m_UploadFencePoint == uploadFencePoint,
				"A render frame cannot adopt resources from different upload submissions.");
			m_UploadFencePoint = uploadFencePoint;
		}
		if (sourceAllocations.IsEmpty())
		{
			return;
		}

		GGLAB_ASSERT_MSG(m_SceneGpuAllocations.IsEmpty(),
			"A render frame cannot replace scene GPU allocations before retirement.");
		if (!m_SceneGpuAllocations.IsEmpty())
		{
			return;
		}
		m_SceneGpuAllocations = sourceAllocations;
		sourceAllocations = {};
	}
}
