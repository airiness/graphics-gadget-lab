#include "Core/Precompiled.h"
#include "Diagnostics/Builders/BuiltinSnapshotProviders.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Builders/DX12ResourceManagerSnapshotBuilder.h"
#include "Diagnostics/Builders/PersistentSceneBufferSnapshotBuilder.h"
#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "Diagnostics/Builders/RHIPipelineSystemSnapshotBuilder.h"
#include "Diagnostics/Builders/TransientResourcePoolSnapshotBuilder.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"
#include "Diagnostics/Snapshots/PersistentSceneBufferSnapshot.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Diagnostics/Snapshots/RHIPipelineSystemSnapshot.h"
#include "Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"

namespace gglab
{
	namespace
	{
		template<typename Snapshot>
		class SnapshotProvider : public SnapshotProviderBase
		{
		public:
			[[nodiscard]] SnapshotId GetId() const noexcept final { return SnapshotIdOf<Snapshot>; }
		};

		class AssetSnapshotProvider final : public SnapshotProvider<AssetSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Assets"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<AssetSnapshot>();
				snapshot = context.m_AssetManager ? BuildAssetSnapshot(*context.m_AssetManager) : AssetSnapshot{};
			}
		};

		class PersistentSceneBufferSnapshotProvider final : public SnapshotProvider<PersistentSceneBufferSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Persistent Scene Buffers"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<PersistentSceneBufferSnapshot>();
				if (context.m_Renderer) BuildPersistentSceneBufferSnapshot(*context.m_Renderer, snapshot);
				else snapshot = {};
			}
		};

		class RenderGraphSnapshotProvider final : public SnapshotProvider<RGSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Render Graph"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<RGSnapshot>();
				if (context.m_RenderGraph) BuildRenderGraphSnapshot(*context.m_RenderGraph, snapshot);
				else snapshot = {};
			}
		};

		class TransientResourcePoolSnapshotProvider final : public SnapshotProvider<TransientResourcePoolSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Transient Resource Pool"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<TransientResourcePoolSnapshot>();
				const auto* pool = context.m_Renderer ? context.m_Renderer->GetTransientResourcePool() : nullptr;
				if (pool) BuildTransientResourcePoolSnapshot(*pool, snapshot);
				else snapshot = {};
			}
		};

		class DX12ResourceManagerSnapshotProvider final : public SnapshotProvider<DX12ResourceManagerSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "DX12 Resources"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<DX12ResourceManagerSnapshot>();
				auto* dx12 = context.m_Renderer ? dynamic_cast<DX12Context*>(context.m_Renderer->GetRHIContext()) : nullptr;
				auto* manager = dx12 ? dx12->GetDX12Device().GetResourceManager() : nullptr;
				if (manager) BuildDX12ResourceManagerSnapshot(*manager, snapshot);
				else snapshot = {};
			}
		};

		class RHIPipelineSystemSnapshotProvider final : public SnapshotProvider<RHIPipelineSystemSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "RHI Pipeline System"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<RHIPipelineSystemSnapshot>();
				auto* rhi = context.m_Renderer ? context.m_Renderer->GetRHIContext() : nullptr;
				auto* system = rhi ? dynamic_cast<DX12PipelineSystem*>(&rhi->GetPipelineSystem()) : nullptr;
				if (system) BuildDX12PipelineSystemSnapshot(
					*system, context.m_Renderer->GetPipelineCache(), snapshot);
				else snapshot = {};
			}
		};
	}

	void RegisterBuiltinSnapshotProviders(DiagnosticsRuntime& runtime) noexcept
	{
		runtime.RegisterProvider(std::make_unique<AssetSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<PersistentSceneBufferSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<RenderGraphSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<TransientResourcePoolSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<DX12ResourceManagerSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<RHIPipelineSystemSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
	}
}
