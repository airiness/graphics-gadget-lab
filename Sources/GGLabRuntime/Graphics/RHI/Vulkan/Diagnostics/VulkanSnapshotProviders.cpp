#include "Graphics/RHI/Vulkan/Diagnostics/VulkanSnapshotProviders.h"

#include "Diagnostics/Builders/RHIPipelineSystemSnapshotBuilder.h"
#include "Diagnostics/Builders/VulkanBackendSnapshotBuilder.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "Diagnostics/Snapshots/RHIPipelineSystemSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/VulkanBackendSnapshot.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"

#include <memory>
#include <string_view>

namespace gglab
{
	namespace
	{
		class VulkanBackendSnapshotProvider final
			: public TypedSnapshotProviderBase<VulkanBackendSnapshot>
		{
		public:
			explicit VulkanBackendSnapshotProvider(VulkanContext& context) noexcept :
				m_Context(context)
			{
			}

			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Vulkan Backend";
			}
			void Capture(const SnapshotContext&, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<VulkanBackendSnapshot>();
				BuildVulkanBackendSnapshot(m_Context, snapshot);
			}

		private:
			VulkanContext& m_Context;
		};

		class VulkanPipelineSystemSnapshotProvider final
			: public TypedSnapshotProviderBase<RHIPipelineSystemSnapshot>
		{
		public:
			VulkanPipelineSystemSnapshotProvider(
				VulkanPipelineSystem& system, PipelineCache* pipelineCache) noexcept :
				m_System(system), m_PipelineCache(pipelineCache)
			{
			}

			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "RHI Pipeline System";
			}
			void Capture(const SnapshotContext&, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<RHIPipelineSystemSnapshot>();
				BuildVulkanPipelineSystemSnapshot(m_System, m_PipelineCache, snapshot);
			}

		private:
			VulkanPipelineSystem& m_System;
			PipelineCache* m_PipelineCache = nullptr;
		};
	}

	void RegisterVulkanSnapshotProviders(DiagnosticsRuntime& runtime, VulkanContext& context,
		PipelineCache* pipelineCache) noexcept
	{
		auto& pipelineSystem = static_cast<VulkanPipelineSystem&>(context.GetPipelineSystem());
		runtime.RegisterProvider(std::make_unique<VulkanBackendSnapshotProvider>(context),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<VulkanPipelineSystemSnapshotProvider>(
			pipelineSystem, pipelineCache), SnapshotUpdatePolicy::EveryFrame);
	}
}
