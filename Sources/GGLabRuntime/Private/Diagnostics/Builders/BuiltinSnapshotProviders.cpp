#include "Diagnostics/Builders/BuiltinSnapshotProviders.h"
#include "GGLabRuntime/Diagnostics/AssetSnapshotRead.h"
#include "Diagnostics/Builders/ForwardPlusDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/GTAODiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/IBLDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/PersistentSceneBufferSnapshotBuilder.h"
#include "Diagnostics/Builders/PostProcessDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "Diagnostics/Builders/RenderQueueSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderViewSnapshot.h"
#include "Diagnostics/Builders/SamplerRegistrySnapshotBuilder.h"
#include "Diagnostics/Builders/ShadowDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/TransientResourcePoolSnapshotBuilder.h"
#include "Diagnostics/Builders/TaskSystemSnapshotBuilder.h"
#include "Diagnostics/Builders/TemporalAADiagnosticsSnapshotBuilder.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ForwardPlusDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/GTAODiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/IBLDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/PersistentSceneBufferSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/PostProcessDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/SamplerRegistrySnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TaskSystemSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TemporalAADiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/Renderer.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

#include <memory>
#include <string_view>

namespace gglab
{
	namespace
	{
		class RenderViewSnapshotProvider final : public TypedSnapshotProviderBase<RenderViewSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Render Views"; }
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<RenderViewSnapshot>();
				snapshot.m_Views.assign(context.m_RenderViews.begin(), context.m_RenderViews.end());
			}
		};

		class RenderQueueSnapshotProvider final : public TypedSnapshotProviderBase<RenderQueueSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Render Queues"; }
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				store.GetOrCreate<RenderQueueSnapshot>() = BuildRenderQueueSnapshot(context.m_RenderQueues);
			}
		};
		class AssetSnapshotProvider final : public TypedSnapshotProviderBase<AssetSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Assets"; }
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<AssetSnapshot>();
				snapshot = context.m_AssetManager ? BuildAssetSnapshot(*context.m_AssetManager)
					: AssetSnapshot{};
			}
		};

		class TaskSystemSnapshotProvider final : public TypedSnapshotProviderBase<TaskSystemSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Task System";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<TaskSystemSnapshot>();
				if (context.m_TaskSystem)
				{
					BuildTaskSystemSnapshot(*context.m_TaskSystem, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}
		};

		class PersistentSceneBufferSnapshotProvider final
			: public TypedSnapshotProviderBase<PersistentSceneBufferSnapshot>
		{
		public:
			explicit PersistentSceneBufferSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Persistent Scene Buffers";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<PersistentSceneBufferSnapshot>();
				if (!context.m_RenderHost || !m_Renderer)
				{
					snapshot = {};
					return;
				}
				BuildPersistentSceneBufferSnapshot(*m_Renderer, snapshot);
			}

		private:
			Renderer* m_Renderer;
		};

		class IBLDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<IBLDiagnosticsSnapshot>
		{
		public:
			explicit IBLDiagnosticsSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "IBL Diagnostics";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<IBLDiagnosticsSnapshot>();
				snapshot = (context.m_RenderHost && m_Renderer)
					? BuildIBLDiagnosticsSnapshot(
						*m_Renderer, context.m_EnvironmentAssetController)
					: IBLDiagnosticsSnapshot{};
			}

		private:
			Renderer* m_Renderer;
		};

		class RenderGraphSnapshotProvider final : public TypedSnapshotProviderBase<RGSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Render Graph";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<RGSnapshot>();
				if (context.m_RenderGraph)
				{
					BuildRenderGraphSnapshot(*context.m_RenderGraph, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}
		};

		class ShadowDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<ShadowDiagnosticsSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Shadow Diagnostics";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<ShadowDiagnosticsSnapshot>();
				snapshot = context.m_RenderGraph
					? BuildShadowDiagnosticsSnapshot(*context.m_RenderGraph)
					: ShadowDiagnosticsSnapshot{};
			}
		};

		class PostProcessDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<PostProcessDiagnosticsSnapshot>
		{
		public:
			explicit PostProcessDiagnosticsSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Post Process Diagnostics";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<PostProcessDiagnosticsSnapshot>();
				if (context.m_RenderHost && m_Renderer && context.m_RenderGraph)
				{
					snapshot = BuildPostProcessDiagnosticsSnapshot(
						*m_Renderer, *context.m_RenderGraph);
				}
				else
				{
					snapshot = {};
				}
			}

		private:
			Renderer* m_Renderer;
		};

		class ForwardPlusDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<ForwardPlusDiagnosticsSnapshot>
		{
		public:
			explicit ForwardPlusDiagnosticsSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Forward+ Diagnostics";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<ForwardPlusDiagnosticsSnapshot>();
				if (context.m_RenderHost && m_Renderer && context.m_RenderGraph)
				{
					snapshot = BuildForwardPlusDiagnosticsSnapshot(
						*m_Renderer, *context.m_RenderGraph);
				}
				else
				{
					snapshot = {};
				}
			}

		private:
			Renderer* m_Renderer;
		};

		class GTAODiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<GTAODiagnosticsSnapshot>
		{
		public:
			explicit GTAODiagnosticsSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "GTAO Diagnostics";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<GTAODiagnosticsSnapshot>();
				if (!context.m_RenderHost || !m_Renderer || !context.m_RenderGraph)
				{
					snapshot = {};
					return;
				}
				const GTAOSettings* authoringSettings = context.m_AuthoringViewRenderProfile
					? &context.m_AuthoringViewRenderProfile->m_Lighting.m_GTAO
					: nullptr;
				const GTAOSettings* requestedSettings = context.m_EffectiveViewRenderProfile
					? &context.m_EffectiveViewRenderProfile->m_Lighting.m_GTAO
					: nullptr;
				snapshot = BuildGTAODiagnosticsSnapshot(*m_Renderer,
					*context.m_RenderGraph, authoringSettings, requestedSettings,
					context.m_GTAOOverrideActive);
			}

		private:
			Renderer* m_Renderer;
		};

		class TemporalAADiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<TemporalAADiagnosticsSnapshot>
		{
		public:
			explicit TemporalAADiagnosticsSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Temporal AA Diagnostics";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<TemporalAADiagnosticsSnapshot>();
				if (!context.m_RenderHost || !m_Renderer || !context.m_RenderGraph)
				{
					snapshot = {};
					return;
				}
				const RenderView* displayView = nullptr;
				if (context.m_TemporalFramePlan)
				{
					const size_t viewIndex =
						utils::ToIndex(context.m_TemporalFramePlan->m_DisplayViewId);
					if (viewIndex < context.m_RenderViews.size())
					{
						displayView = &context.m_RenderViews[viewIndex];
					}
				}
				const TemporalAASettings* authoringSettings =
					context.m_AuthoringViewRenderProfile
					? &context.m_AuthoringViewRenderProfile->m_TemporalAA
					: nullptr;
				const TemporalAASettings* requestedSettings =
					context.m_EffectiveViewRenderProfile
					? &context.m_EffectiveViewRenderProfile->m_TemporalAA
					: nullptr;
				snapshot = BuildTemporalAADiagnosticsSnapshot(*m_Renderer,
					*context.m_RenderGraph, context.m_TemporalFramePlan, displayView,
					authoringSettings, requestedSettings);
			}

		private:
			Renderer* m_Renderer;
		};

		class TransientResourcePoolSnapshotProvider final
			: public TypedSnapshotProviderBase<TransientResourcePoolSnapshot>
		{
		public:
			explicit TransientResourcePoolSnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Transient Resource Pool";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<TransientResourcePoolSnapshot>();
				if (!context.m_RenderHost || !m_Renderer)
				{
					snapshot = {};
					return;
				}
				const auto* pool = m_Renderer->GetTransientResourcePool();
				if (pool)
				{
					BuildTransientResourcePoolSnapshot(*pool, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}

		private:
			Renderer* m_Renderer;
		};

		class SamplerRegistrySnapshotProvider final
			: public TypedSnapshotProviderBase<SamplerRegistrySnapshot>
		{
		public:
			explicit SamplerRegistrySnapshotProvider(Renderer* renderer) noexcept :
				m_Renderer(renderer)
			{
			}
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Sampler Registry";
			}
			void Capture(const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<SamplerRegistrySnapshot>();
				if (!context.m_RenderHost || !m_Renderer)
				{
					snapshot = {};
					return;
				}
				const SamplerRegistry* registry = m_Renderer->GetSamplerRegistryService();
				if (registry)
				{
					BuildSamplerRegistrySnapshot(*registry, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}

		private:
			Renderer* m_Renderer;
		};
	}

	void RegisterBuiltinSnapshotProviders(
		DiagnosticsRuntime& runtime, Renderer* renderer) noexcept
	{
		runtime.RegisterProvider(
			std::make_unique<RenderViewSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<RenderQueueSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<AssetSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<TaskSystemSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<IBLDiagnosticsSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<PersistentSceneBufferSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<RenderGraphSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<ShadowDiagnosticsSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<PostProcessDiagnosticsSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<ForwardPlusDiagnosticsSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<GTAODiagnosticsSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<TemporalAADiagnosticsSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<TransientResourcePoolSnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<SamplerRegistrySnapshotProvider>(renderer),
			SnapshotUpdatePolicy::EveryFrame);
	}
}
