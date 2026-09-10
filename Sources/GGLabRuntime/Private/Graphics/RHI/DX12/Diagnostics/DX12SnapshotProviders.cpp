#include "Graphics/RHI/DX12/Diagnostics/DX12SnapshotProviders.h"

#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "Diagnostics/Snapshots/RHIPipelineSystemSnapshot.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/DX12BackendSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/Diagnostics/DX12BackendSnapshotBuilder.h"
#include "Graphics/RHI/DX12/Diagnostics/DX12PipelineSystemSnapshotBuilder.h"
#include "Graphics/RHI/DX12/Diagnostics/DX12ResourceManagerSnapshotBuilder.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"

#include <memory>
#include <string_view>

namespace gglab
{
	namespace
	{
		class DX12ResourceManagerSnapshotProvider final
			: public TypedSnapshotProviderBase<DX12ResourceManagerSnapshot>
		{
		public:
			explicit DX12ResourceManagerSnapshotProvider(
				DX12ResourceManager& manager) noexcept :
				m_Manager(manager)
			{
			}

			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "DX12 Resources";
			}
			void Capture(const SnapshotContext&, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<DX12ResourceManagerSnapshot>();
				BuildDX12ResourceManagerSnapshot(m_Manager, snapshot);
			}

		private:
			DX12ResourceManager& m_Manager;
		};

		class DX12BackendSnapshotProvider final
			: public TypedSnapshotProviderBase<DX12BackendSnapshot>
		{
		public:
			explicit DX12BackendSnapshotProvider(DX12Context& context) noexcept :
				m_Context(context)
			{
			}

			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "DirectX 12 Backend";
			}
			void Capture(const SnapshotContext&, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<DX12BackendSnapshot>();
				BuildDX12BackendSnapshot(m_Context, snapshot);
			}

		private:
			DX12Context& m_Context;
		};

		class DX12PipelineSystemSnapshotProvider final
			: public TypedSnapshotProviderBase<RHIPipelineSystemSnapshot>
		{
		public:
			DX12PipelineSystemSnapshotProvider(
				DX12PipelineSystem& system, PipelineCache* pipelineCache) noexcept :
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
				BuildDX12PipelineSystemSnapshot(m_System, m_PipelineCache, snapshot);
			}

		private:
			DX12PipelineSystem& m_System;
			PipelineCache* m_PipelineCache = nullptr;
		};
	}

	void RegisterDX12SnapshotProviders(DiagnosticsRuntime& runtime, DX12Context& context,
		PipelineCache* pipelineCache) noexcept
	{
		auto* resourceManager = context.GetDX12Device().GetResourceManager();
		GGLAB_ASSERT_NOT_NULL(resourceManager);
		auto& pipelineSystem = static_cast<DX12PipelineSystem&>(context.GetPipelineSystem());
		runtime.RegisterProvider(std::make_unique<DX12ResourceManagerSnapshotProvider>(
			*resourceManager), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<DX12BackendSnapshotProvider>(context),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<DX12PipelineSystemSnapshotProvider>(
			pipelineSystem, pipelineCache), SnapshotUpdatePolicy::EveryFrame);
	}
}
