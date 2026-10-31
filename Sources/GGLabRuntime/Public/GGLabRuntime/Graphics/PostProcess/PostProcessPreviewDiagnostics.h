#pragma once

#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <cstdint>

namespace gglab
{
	// Provenance of the whole-image estimate used by a GI preview. Pending tiles
	// do not advance samples. This describes calculation, not convergence.
	struct GIPreviewSnapshot
	{
		uint64_t m_Generation = 0;
		uint32_t m_Samples = 0;
		uint32_t m_TargetSamples = 0;
		uint32_t m_MaximumDepth = 0;
	};

	// Copied observation, not GPU resource ownership. The descriptor may only be
	// submitted through the GUI adapter during the current tooling draw. Do not
	// cache it across frames, resize, resource retirement or runtime shutdown.
	struct PostProcessPreviewDiagnostics
	{
		PostProcessDebugSelection m_Selected{};
		PostProcessDebugSelection m_Published{};
		RHIDescriptorHandle m_SrvDescriptor{};
		uint64_t m_UpdateCount = 0;
		uint64_t m_FrameSerial = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		float m_ExposureEV = 0.0f;
		GIPreviewSnapshot m_GISnapshot{};
		bool m_SourceAvailable = false;
		bool m_Requested = false;
		bool m_HasPublished = false;
	};
}
