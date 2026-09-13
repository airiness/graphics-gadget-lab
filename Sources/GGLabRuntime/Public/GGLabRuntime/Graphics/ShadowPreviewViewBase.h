#pragma once

#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <cstdint>

namespace gglab
{
	// A copied allocation observation, not GPU ownership or proof of completed contents.
	// Submit the descriptor through the GUI adapter only during the current tooling draw;
	// do not retain it across frames, resource replacement, retirement or shutdown.
	struct ShadowPreviewDiagnostics
	{
		RHIDescriptorHandle m_SrvDescriptor{};
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		bool m_Allocated = false;
	};

	// Non-owning, render-thread query. The pipeline allocates and imports the preview
	// before tooling draw; RenderGraph orders its producer, GUI reads and final export.
	// Consumers must also check the current frame's shadow snapshot for an active source.
	// Querying never allocates, publishes contents, waits for the GPU or changes lifetime.
	class ShadowPreviewViewBase
	{
	public:
		virtual ~ShadowPreviewViewBase() = default;
		[[nodiscard]] virtual ShadowPreviewDiagnostics GetShadowPreviewDiagnostics()
			const noexcept = 0;
	};
}
