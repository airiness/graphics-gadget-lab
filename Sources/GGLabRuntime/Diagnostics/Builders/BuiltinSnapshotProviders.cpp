#include "Diagnostics/Builders/BuiltinSnapshotProviders.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Builders/ForwardPlusDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/GTAODiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/IBLDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/PersistentSceneBufferSnapshotBuilder.h"
#include "Diagnostics/Builders/PostProcessDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "Diagnostics/Builders/SamplerRegistrySnapshotBuilder.h"
#include "Diagnostics/Builders/TransientResourcePoolSnapshotBuilder.h"
#include "Diagnostics/Builders/TaskSystemSnapshotBuilder.h"
#include "Diagnostics/Builders/TemporalAADiagnosticsSnapshotBuilder.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Diagnostics/Snapshots/ForwardPlusDiagnosticsSnapshot.h"
#include "Diagnostics/Snapshots/GTAODiagnosticsSnapshot.h"
#include "Diagnostics/Snapshots/IBLDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/PersistentSceneBufferSnapshot.h"
#include "Diagnostics/Snapshots/PostProcessDiagnosticsSnapshot.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Diagnostics/Snapshots/SamplerRegistrySnapshot.h"
#include "Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TaskSystemSnapshot.h"
#include "Diagnostics/Snapshots/TemporalAADiagnosticsSnapshot.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"

#include <memory>
#include <string_view>

namespace gglab
{
	namespace
	{
		class AssetSnapshotProvider final : public TypedSnapshotProviderBase<AssetSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override { return "Assets"; }
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
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
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
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
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Persistent Scene Buffers";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<PersistentSceneBufferSnapshot>();
				if (context.m_Renderer)
				{
					BuildPersistentSceneBufferSnapshot(*context.m_Renderer, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}
		};

		class IBLDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<IBLDiagnosticsSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "IBL Diagnostics";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<IBLDiagnosticsSnapshot>();
				snapshot = context.m_Renderer ? BuildIBLDiagnosticsSnapshot(*context.m_Renderer,
					context.m_EnvironmentAssetController)
					: IBLDiagnosticsSnapshot{};
			}
		};

		class RenderGraphSnapshotProvider final : public TypedSnapshotProviderBase<RGSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Render Graph";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
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

		class PostProcessDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<PostProcessDiagnosticsSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Post Process Diagnostics";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<PostProcessDiagnosticsSnapshot>();
				if (context.m_Renderer && context.m_RenderGraph)
				{
					snapshot = BuildPostProcessDiagnosticsSnapshot(
						*context.m_Renderer, *context.m_RenderGraph);
				}
				else
				{
					snapshot = {};
				}
			}
		};

		class ForwardPlusDiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<ForwardPlusDiagnosticsSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Forward+ Diagnostics";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<ForwardPlusDiagnosticsSnapshot>();
				if (context.m_Renderer && context.m_RenderGraph)
				{
					snapshot = BuildForwardPlusDiagnosticsSnapshot(
						*context.m_Renderer, *context.m_RenderGraph);
				}
				else
				{
					snapshot = {};
				}
			}
		};

		class GTAODiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<GTAODiagnosticsSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "GTAO Diagnostics";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<GTAODiagnosticsSnapshot>();
				if (!context.m_Renderer || !context.m_RenderGraph)
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
				snapshot = BuildGTAODiagnosticsSnapshot(*context.m_Renderer,
					*context.m_RenderGraph, authoringSettings, requestedSettings,
					context.m_GTAOOverrideActive);
			}
		};

		class TemporalAADiagnosticsSnapshotProvider final
			: public TypedSnapshotProviderBase<TemporalAADiagnosticsSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Temporal AA Diagnostics";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<TemporalAADiagnosticsSnapshot>();
				if (!context.m_Renderer || !context.m_RenderGraph)
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
				snapshot = BuildTemporalAADiagnosticsSnapshot(*context.m_Renderer,
					*context.m_RenderGraph, context.m_TemporalFramePlan, displayView,
					authoringSettings, requestedSettings);
			}
		};

		class TransientResourcePoolSnapshotProvider final
			: public TypedSnapshotProviderBase<TransientResourcePoolSnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Transient Resource Pool";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<TransientResourcePoolSnapshot>();
				const auto* pool =
					context.m_Renderer ? context.m_Renderer->GetTransientResourcePool() : nullptr;
				if (pool)
				{
					BuildTransientResourcePoolSnapshot(*pool, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}
		};

		class SamplerRegistrySnapshotProvider final
			: public TypedSnapshotProviderBase<SamplerRegistrySnapshot>
		{
		public:
			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Sampler Registry";
			}
			void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override
			{
				auto& snapshot = store.GetOrCreate<SamplerRegistrySnapshot>();
				const SamplerRegistry* registry =
					context.m_Renderer ? context.m_Renderer->GetSamplerRegistry() : nullptr;
				if (registry)
				{
					BuildSamplerRegistrySnapshot(*registry, snapshot);
				}
				else
				{
					snapshot = {};
				}
			}
		};
	}

	void RegisterBuiltinSnapshotProviders(DiagnosticsRuntime& runtime) noexcept
	{
		runtime.RegisterProvider(
			std::make_unique<AssetSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<TaskSystemSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<IBLDiagnosticsSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<PersistentSceneBufferSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<RenderGraphSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<PostProcessDiagnosticsSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<ForwardPlusDiagnosticsSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<GTAODiagnosticsSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<TemporalAADiagnosticsSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(std::make_unique<TransientResourcePoolSnapshotProvider>(),
			SnapshotUpdatePolicy::EveryFrame);
		runtime.RegisterProvider(
			std::make_unique<SamplerRegistrySnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
	}
}
