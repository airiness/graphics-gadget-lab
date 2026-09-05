#include "Diagnostics/Builders/BackendSnapshotProviders.h"

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/Diagnostics/DX12SnapshotProviders.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/Diagnostics/VulkanSnapshotProviders.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#endif

namespace gglab
{
	void RegisterBackendSnapshotProviders(DiagnosticsRuntime& runtime, RHIContext& context,
		PipelineCache* pipelineCache) noexcept
	{
		switch (context.GetDevice().GetBackendType())
		{
		case RHIBackendType::DX12:
		{
			auto* dx12 = dynamic_cast<DX12Context*>(&context);
			GGLAB_ASSERT_MSG(dx12,
				"DX12 diagnostics registration requires a DX12Context implementation.");
			if (dx12)
			{
				RegisterDX12SnapshotProviders(runtime, *dx12, pipelineCache);
			}
			break;
		}
		case RHIBackendType::Vulkan:
#if GGLAB_ENABLE_VULKAN
		{
			auto* vulkan = dynamic_cast<VulkanContext*>(&context);
			GGLAB_ASSERT_MSG(vulkan,
				"Vulkan diagnostics registration requires a VulkanContext implementation.");
			if (vulkan)
			{
				RegisterVulkanSnapshotProviders(runtime, *vulkan, pipelineCache);
			}
			break;
		}
#else
			GGLAB_ASSERT_MSG(false,
				"Vulkan diagnostics registration requires the Vulkan backend build.");
			break;
#endif
		default:
			GGLAB_ASSERT_MSG(false,
				"Diagnostics registration requires a supported active RHI backend.");
			break;
		}
	}
}
