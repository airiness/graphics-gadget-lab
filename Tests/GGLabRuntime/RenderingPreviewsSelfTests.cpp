#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "Diagnostics/Builders/TransientResourcePoolSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/IBLBakeConfig.h"
#include "GGLabRuntime/Graphics/IBLPreviewControlBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewDiagnostics.h"
#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"
#include "GGLabRuntime/Graphics/IBLPreviewViewBase.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewDiagnostics.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Resource/RenderTextureIndex.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/RHI/RHIUploadPool.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/RenderPassIBLClear.h"
#include "Graphics/RenderPass/RenderPassIBLPreview.h"
#include "Graphics/RenderPass/RenderPassPostProcessPreview.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <span>

namespace gglab::rendering_test
{
	void RunIBLPreviewContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		device.m_UseControlledFenceCompletion = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({
			.m_Device = &device,
			.m_TransientResourcePool = &pool,
			.m_SamplerRegistry = &samplers,
		});
		const IBLPreviewViewBase& view = registry;
		IBLPreviewControlBase& control = registry;
		const auto empty = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(empty.m_Environment.m_BakeState == IBLBakeState::Unavailable &&
			!empty.m_BrdfLut.m_SrvDescriptor.IsValid() &&
			!empty.m_EnvironmentPreview.m_Texture.m_SrvDescriptor.IsValid() &&
			empty.m_EnvironmentPreview.m_UpdateCount == 0 && device.m_CreateTextureCount == 0,
			"IBL preview queries before allocation neither allocate nor claim initialized content");

		control.RequestIBLPreview(IBLPreviewType::Count);
		control.RequestIBLPreview(static_cast<IBLPreviewType>(255));
		context.Check(!view.GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview.m_Requested,
			"Invalid public preview requests are ignored");
		control.RequestIBLPreview(IBLPreviewType::Environment);
		control.RequestIBLPreview(IBLPreviewType::Environment);
		context.Check(registry.ObserveIBLPreviewRequest(IBLPreviewType::Environment).m_Requested &&
			registry.ObserveIBLPreviewRequest(IBLPreviewType::Environment).m_Requested,
			"Repeated preview requests remain observable until a submitted draw acknowledges them");
		registry.PublishIBLPreview(IBLPreviewType::Environment, registry.ObserveIBLPreviewRequest(IBLPreviewType::Environment), IBLPreviewLayout::Cross, 0);
		registry.PublishIBLPreview(IBLPreviewType::Irradiance, registry.ObserveIBLPreviewRequest(IBLPreviewType::Irradiance), IBLPreviewLayout::Cross, 0);
		registry.PublishIBLPreview(IBLPreviewType::PrefilteredSpecular, registry.ObserveIBLPreviewRequest(IBLPreviewType::PrefilteredSpecular), IBLPreviewLayout::Cross, 0);
		control.RequestIBLPreview(IBLPreviewType::Environment);
		registry.AcknowledgeIBLPreviewRequest(IBLPreviewType::Environment,
			registry.ObserveIBLPreviewRequest(IBLPreviewType::Environment).m_Generation);
		context.Check(!registry.IsIBLPreviewDirty(IBLPreviewType::Environment) &&
			!view.GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview.m_Requested,
			"A clean preview acknowledges its generation without scheduling another update");
		control.SetIBLEnvironmentPreviewLayout(IBLPreviewLayout::Count);
		control.SetIBLIrradiancePreviewLayout(static_cast<IBLPreviewLayout>(99));
		control.SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout::Count);
		context.Check(!view.GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview.m_Dirty &&
			!view.GetIBLPreviewResourcesDiagnostics().m_IrradiancePreview.m_Dirty &&
			!view.GetIBLPreviewResourcesDiagnostics().m_PrefilteredSpecularPreview.m_Dirty,
			"Invalid layouts preserve clean preview state");
		control.SetIBLEnvironmentPreviewLayout(IBLPreviewLayout::Cross);
		control.SetIBLIrradiancePreviewLayout(IBLPreviewLayout::Cross);
		control.SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout::Cross);
		const auto unchanged = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(unchanged.m_EnvironmentPreview.m_Dirty &&
			!unchanged.m_IrradiancePreview.m_Dirty &&
			!unchanged.m_PrefilteredSpecularPreview.m_Dirty,
			"Same-layout controls preserve the established per-preview dirty semantics");
		control.SetIBLIrradiancePreviewLayout(IBLPreviewLayout::Grid2x3);
		control.SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout::Grid2x3);
		control.SetIBLEnvironmentPreviewMip(1000);
		control.SetIBLPrefilteredSpecularPreviewMip(2000);
		const auto selected = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(selected.m_EnvironmentPreview.m_SelectedMip == 1000 &&
			selected.m_PrefilteredSpecularPreview.m_SelectedMip == 2000 &&
			selected.m_IrradiancePreview.m_Dirty &&
			selected.m_PrefilteredSpecularPreview.m_Dirty &&
			unchanged.m_PrefilteredSpecularPreview.m_SelectedMip == 0 &&
			device.m_CreateTextureCount == 0,
			"Controls preserve requested mips for pass-side clamping without allocating or rewriting copies");

		RenderResourceRegistry::IBLResourceCreateInfo info;
		registry.EnsureIblResources(info);
		const auto allocated = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(device.m_CreateTextureCount == 7 &&
			allocated.m_Environment.m_Width == 512 && allocated.m_Environment.m_ArraySize == 6 &&
			allocated.m_Environment.m_MipLevels == 10 &&
			allocated.m_PrefilteredSpecular.m_MipLevels == 5 &&
			allocated.m_BrdfLut.m_SrvDescriptor.IsValid() &&
			allocated.m_EnvironmentPreview.m_Texture.m_Width == 1024 &&
			allocated.m_EnvironmentPreview.m_Texture.m_Height == 768 &&
			allocated.m_EnvironmentPreview.m_Texture.m_SrvDescriptor.IsValid() &&
			!registry.HasInitializedActiveIBL(),
			"IBL preview queries expose active allocation metadata without initializing GPU content");
		registry.EnsureIblResources(info);
		context.Check(device.m_CreateTextureCount == 7 &&
			view.GetIBLPreviewResourcesDiagnostics().m_Environment.m_SrvDescriptor.m_Index == allocated.m_Environment.m_SrvDescriptor.m_Index,
			"Compatible allocations and repeated queries preserve descriptor identity");

		IBLBakeConfig config;
		config.m_EnvironmentCubemapSize = 64;
		registry.EnsureIBLBakeResources(config);
		const auto staging = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(device.m_CreateTextureCount == 11 &&
			staging.m_Environment.m_Width == allocated.m_Environment.m_Width &&
			staging.m_Environment.m_SrvDescriptor.m_Index == allocated.m_Environment.m_SrvDescriptor.m_Index,
			"Staging bake allocations remain hidden from the tooling view");
		registry.PublishIBLBakeResources();
		const auto published = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(published.m_Environment.m_Width == 64 &&
			published.m_Environment.m_SrvDescriptor.m_Index != allocated.m_Environment.m_SrvDescriptor.m_Index &&
			published.m_Environment.m_BakeState == IBLBakeState::Ready &&
			published.m_EnvironmentPreview.m_Texture.m_SrvDescriptor.m_Index ==
			allocated.m_EnvironmentPreview.m_Texture.m_SrvDescriptor.m_Index &&
			published.m_EnvironmentPreview.m_Dirty && registry.HasInitializedActiveIBL(),
			"Runtime publication switches the active source while preserving preview ownership");

		control.RequestIBLPreview(IBLPreviewType::Irradiance);
		const RHIFencePoint retireFence{ RHIFenceHandle{ 1, 1 }, 9 };
		registry.ReleaseAll(retireFence);
		const auto released = view.GetIBLPreviewResourcesDiagnostics();
		context.Check(released.m_Environment.m_BakeState == IBLBakeState::Unavailable &&
			!released.m_Environment.m_SrvDescriptor.IsValid() &&
			!released.m_IrradiancePreview.m_Texture.m_SrvDescriptor.IsValid() &&
			released.m_EnvironmentPreview.m_SelectedMip == 1000 &&
			released.m_IrradiancePreview.m_Requested && device.m_CreateTextureCount == 11,
			"Release empties borrowed resource metadata while retaining existing requested state");
		pool.Tick();
		TransientResourcePoolSnapshot retirement;
		BuildTransientResourcePoolSnapshot(pool, retirement);
		context.Check(retirement.m_TextureCounts.m_PendingRetirement == 11 &&
			retirement.m_TextureCounts.m_Available == 0 && device.m_DestroyTextureCount == 0,
			"Active and staging IBL resources remain pending until the owner fence completes");
		device.m_CompletedFenceValue = 9;
		pool.Tick();
		BuildTransientResourcePoolSnapshot(pool, retirement);
		context.Check(retirement.m_TextureCounts.m_PendingRetirement == 0 &&
			retirement.m_TextureCounts.m_Available == 11,
			"IBL resources become reusable only after fence completion");
	}

	void RunShadowPreviewContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		device.m_UseControlledFenceCompletion = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({
			.m_Device = &device,
			.m_TransientResourcePool = &pool,
			.m_SamplerRegistry = &samplers,
		});
		const ShadowPreviewViewBase& view = registry;
		ShadowPreviewControlBase& control = registry;
		context.Check(!registry.ObserveShadowPreviewRequest().m_Requested, "Shadow previews start without rendering demand");
		control.RequestShadowPreview();
		control.RequestShadowPreview();
		context.Check(registry.ObserveShadowPreviewRequest().m_Requested &&
			registry.ObserveShadowPreviewRequest().m_Requested &&
			device.m_CreateTextureCount == 0, "Shadow preview requests coalesce without allocating resources");
		const auto empty = view.GetShadowPreviewDiagnostics();
		context.Check(!empty.m_Allocated && !empty.m_SrvDescriptor.IsValid() &&
			empty.m_Width == 0 && empty.m_Height == 0 &&
			empty.m_Format == RHIFormat::Unknown && device.m_CreateTextureCount == 0,
			"Shadow preview query is safe before pipeline allocation and does not allocate");

		constexpr auto Index = RenderResourceRegistry::TextureIndex::Preview_Shadow_DirectionalShadowMap;
		registry.EnsureShadowPreviewResources();
		const auto allocated = view.GetShadowPreviewDiagnostics();
		context.Check(allocated.m_Allocated && allocated.m_SrvDescriptor.IsValid() &&
			allocated.m_Width == DefaultDirectionalShadowMapPreviewSize &&
			allocated.m_Height == DefaultDirectionalShadowMapPreviewSize &&
			allocated.m_Format == RHIFormat::R8G8B8A8Unorm && registry.IsDirty(Index),
			"Shadow preview query exposes the pipeline allocation without initializing contents");
		const auto repeated = view.GetShadowPreviewDiagnostics();
		context.Check(repeated.m_SrvDescriptor.m_Index == allocated.m_SrvDescriptor.m_Index &&
			device.m_CreateTextureCount == 1 && registry.IsDirty(Index),
			"Repeated shadow queries leave allocation identity and pending initialization unchanged");
		registry.PublishShadowPreview(registry.ObserveShadowPreviewRequest().m_Generation,
			registry.GetTextureContentRevision(Index));
		registry.EnsureShadowPreviewResources();
		context.Check(view.GetShadowPreviewDiagnostics().m_SrvDescriptor.m_Index ==
			allocated.m_SrvDescriptor.m_Index && !registry.IsDirty(Index) &&
			device.m_CreateTextureCount == 1,
			"Compatible pipeline allocation and tooling queries preserve initialized preview resources");

		const RHIFencePoint retireFence{ RHIFenceHandle{ 1, 1 }, 9 };
		registry.EnsureShadowPreviewResources(256, &retireFence);
		const auto resized = view.GetShadowPreviewDiagnostics();
		context.Check(resized.m_Allocated && resized.m_Width == 256 && resized.m_Height == 256 &&
			resized.m_SrvDescriptor.IsValid() &&
			resized.m_SrvDescriptor.m_Index != allocated.m_SrvDescriptor.m_Index &&
			allocated.m_Width == DefaultDirectionalShadowMapPreviewSize &&
			registry.IsDirty(Index) && device.m_CreateTextureCount == 2,
			"Shadow preview replacement changes live metadata without rewriting copied observations");
		registry.ReleaseAll(retireFence);
		const auto released = view.GetShadowPreviewDiagnostics();
		context.Check(!released.m_Allocated && !released.m_SrvDescriptor.IsValid() &&
			released.m_Width == 0 && released.m_Height == 0 &&
			released.m_Format == RHIFormat::Unknown && device.m_CreateTextureCount == 2,
			"Shadow preview query after release stays empty instead of recreating a resource");
		pool.Tick();
		TransientResourcePoolSnapshot retirement;
		BuildTransientResourcePoolSnapshot(pool, retirement);
		context.Check(retirement.m_TextureCounts.m_PendingRetirement == 2 &&
			retirement.m_TextureCounts.m_Available == 0 && device.m_DestroyTextureCount == 0,
			"Shadow preview allocations remain pending until their owner's retirement fence completes");
		device.m_CompletedFenceValue = 9;
		pool.Tick();
		BuildTransientResourcePoolSnapshot(pool, retirement);
		context.Check(retirement.m_TextureCounts.m_PendingRetirement == 0 &&
			retirement.m_TextureCounts.m_Available == 2,
			"Shadow preview allocations become reusable only after fence completion");
	}

	void RunPostProcessPreviewContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		device.m_UseControlledFenceCompletion = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({
			.m_Device = &device,
			.m_TransientResourcePool = &pool,
			.m_SamplerRegistry = &samplers,
		});
		const PostProcessPreviewViewBase& view = registry;
		PostProcessPreviewControlBase& control = registry;
		const auto empty = view.GetPostProcessPreviewDiagnostics();
		context.Check(!empty.m_HasPublished && !empty.m_Requested &&
			!empty.m_SrvDescriptor.IsValid() && empty.m_Width == 0 && empty.m_Height == 0,
			"Preview query safely observes an unallocated source without a descriptor lookup");

		control.SetPostProcessPreviewSelection({ PostProcessDebugTap::BloomPyramid, 999 });
		control.SetPostProcessPreviewExposureEV(20.0f);
		const auto selected = view.GetPostProcessPreviewDiagnostics();
		context.Check(selected.m_Selected.m_BloomPyramidLevel == MaxBloomPyramidLevels - 1 &&
			selected.m_ExposureEV == 8.0f && !selected.m_Requested &&
			device.m_CreateTextureCount == 0,
			"Preview controls clamp requested values without allocating or scheduling GPU work");
		control.SetPostProcessPreviewSelection({ PostProcessDebugTap::Count, 0 });
		control.SetPostProcessPreviewExposureEV(-20.0f);
		context.Check(view.GetPostProcessPreviewDiagnostics().m_Selected == selected.m_Selected &&
			view.GetPostProcessPreviewDiagnostics().m_ExposureEV == -8.0f,
			"Preview controls ignore invalid taps and retain the existing exposure bounds");
		control.RequestPostProcessPreview();
		control.RequestPostProcessPreview();
		context.Check(view.GetPostProcessPreviewDiagnostics().m_Requested &&
			registry.ObservePostProcessPreviewRequest().m_Requested &&
			registry.ObservePostProcessPreviewRequest().m_Requested && device.m_CreateTextureCount == 0,
			"Repeated preview requests coalesce and only the Runtime owner acknowledges them");

		registry.EnsurePostProcessPreviewResources(1024, 512);
		const auto allocated = view.GetPostProcessPreviewDiagnostics();
		context.Check(allocated.m_Width == 512 && allocated.m_Height == 256 &&
			allocated.m_Format == RHIFormat::R8G8B8A8Unorm &&
			allocated.m_SrvDescriptor.IsValid() && !allocated.m_HasPublished &&
			device.m_CreateTextureCount == 1,
			"Preview observation distinguishes allocated descriptors from recorded contents");
		registry.PublishPostProcessPreview(selected.m_Selected, PostProcessPreviewChannel::PostProcessing, 42, registry.ObservePostProcessPreviewRequest());
		const auto published = view.GetPostProcessPreviewDiagnostics();
		const PostProcessDebugSelection nextSelection{ PostProcessDebugTap::SceneDepthRaw, 0 };
		control.SetPostProcessPreviewSelection(nextSelection);
		control.RequestPostProcessPreview();
		const auto pending = view.GetPostProcessPreviewDiagnostics();
		context.Check(pending.m_Selected == nextSelection && pending.m_Requested &&
			pending.m_HasPublished && pending.m_Published == selected.m_Selected &&
			pending.m_UpdateCount == 1 && pending.m_FrameSerial == 42 &&
			published.m_Selected == selected.m_Selected &&
			!published.m_Requested && device.m_CreateTextureCount == 1,
			"A new preview request preserves published identity and independently copied observations");
		registry.InvalidatePostProcessPreview(nextSelection);
		context.Check(view.GetPostProcessPreviewDiagnostics().m_HasPublished,
			"Invalidating a different preview tap does not erase the published image");
		registry.InvalidatePostProcessPreview(selected.m_Selected);
		context.Check(!view.GetPostProcessPreviewDiagnostics().m_HasPublished &&
			published.m_HasPublished,
			"Preview invalidation changes live availability without rewriting copied metadata");

		const RHIFencePoint retireFence{ RHIFenceHandle{ 1, 1 }, 9 };
		registry.PublishPostProcessPreview(nextSelection, PostProcessPreviewChannel::PostProcessing, 43, registry.ObservePostProcessPreviewRequest());
		registry.EnsurePostProcessPreviewResources(512, 512, &retireFence);
		const auto resized = view.GetPostProcessPreviewDiagnostics();
		context.Check(!resized.m_HasPublished && resized.m_Width == 512 &&
			resized.m_Height == 512 && resized.m_SrvDescriptor.IsValid() &&
			resized.m_SrvDescriptor.m_Index != published.m_SrvDescriptor.m_Index &&
			published.m_Height == 256 && device.m_CreateTextureCount == 2,
			"Resizing a preview publishes new metadata and invalidates the old content identity");
		registry.ReleaseAll(retireFence);
		const auto released = view.GetPostProcessPreviewDiagnostics();
		context.Check(!released.m_HasPublished && !released.m_Requested &&
			!released.m_SrvDescriptor.IsValid() && released.m_Width == 0 &&
			released.m_Height == 0,
			"Preview release removes descriptor availability and pending requests");
		pool.Tick();
		TransientResourcePoolSnapshot retirement;
		BuildTransientResourcePoolSnapshot(pool, retirement);
		context.Check(device.m_DestroyTextureCount == 0 &&
			retirement.m_TextureCounts.m_PendingRetirement == 2 &&
			retirement.m_TextureCounts.m_Available == 0,
			"Preview resource retirement waits for the owner's submission fence");
		device.m_CompletedFenceValue = 9;
		pool.Tick();
		BuildTransientResourcePoolSnapshot(pool, retirement);
		context.Check(retirement.m_TextureCounts.m_PendingRetirement == 0 &&
			retirement.m_TextureCounts.m_Available == 2,
			"Retired preview resources become reusable only after fence completion");

		const PostProcessDebugSelection aoSelection{ PostProcessDebugTap::GTAOFinalAO, 0 };
		const PostProcessDebugSelection taaSelection{ PostProcessDebugTap::TemporalHistoryWeight, 0 };
		control.SetPostProcessPreviewSelection(aoSelection, PostProcessPreviewChannel::AmbientOcclusion);
		control.SetPostProcessPreviewSelection(taaSelection, PostProcessPreviewChannel::TemporalAA);
		control.SetPostProcessPreviewExposureEV(2.0f, PostProcessPreviewChannel::TemporalAA);
		control.RequestPostProcessPreview(PostProcessPreviewChannel::AmbientOcclusion);
		const auto ao = view.GetPostProcessPreviewDiagnostics(PostProcessPreviewChannel::AmbientOcclusion);
		const auto taa = view.GetPostProcessPreviewDiagnostics(PostProcessPreviewChannel::TemporalAA);
		context.Check(ao.m_Selected == aoSelection && ao.m_Requested &&
			taa.m_Selected == taaSelection && !taa.m_Requested && taa.m_ExposureEV == 2.0f &&
			!registry.IsPostProcessPreviewRequested(PostProcessPreviewChannel::PostProcessing) &&
			registry.ObservePostProcessPreviewRequest(PostProcessPreviewChannel::AmbientOcclusion).m_Requested &&
			!registry.IsPostProcessPreviewRequested(PostProcessPreviewChannel::TemporalAA),
			"Feature previews keep independent selections, exposure and requests");
	}

	void RunPreviewSubmissionContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({ .m_Device = &device,
			.m_TransientResourcePool = &pool, .m_SamplerRegistry = &samplers });
		const RGCommitContext submitted{ 73, { RHIFenceHandle{ 1, 1 }, 42 } };
		for (uint32_t i = 0; i < static_cast<uint32_t>(PostProcessPreviewChannel::Count); ++i)
		{
			const auto channel = static_cast<PostProcessPreviewChannel>(i);
			const auto index = RenderResourceRegistry::GetPostProcessPreviewTextureIndex(channel);
			registry.RequestPostProcessPreview(channel);
			registry.EnsurePostProcessPreviewResources(64, 64, nullptr, channel);
			const auto observed = registry.ObservePostProcessPreviewRequest(channel);
			const auto selection = registry.GetPostProcessPreviewSelection(channel);
			RGDeferredCommitList discarded;
			RGExecuteContext execute(discarded);
			execute.DeferCommit([&registry, selection, channel, observed](const RGCommitContext& commit) noexcept
				{
					registry.PublishPostProcessPreview(selection, channel, commit.m_FrameSerial, observed);
				});
			discarded.Discard();
			context.Check(registry.IsPostProcessPreviewRequested(channel) && registry.IsDirty(index) &&
				!registry.HasPublishedPostProcessPreview(channel),
				"Discard preserves each channel's demand and undefined content without publication");
			registry.RequestPostProcessPreview(channel);
			registry.SetPostProcessPreviewExposureEV(1.0f, channel);
			registry.PublishPostProcessPreview(selection, channel, submitted.m_FrameSerial, observed);
			context.Check(registry.IsPostProcessPreviewRequested(channel) && registry.IsDirty(index) &&
				registry.GetPublishedTextureContentRevision(index) == observed.m_ContentRevision &&
				registry.GetPublishedPostProcessPreviewSelection(channel) == selection,
				"An older submitted preview publishes its identity while preserving newer demand and content");
			const auto current = registry.ObservePostProcessPreviewRequest(channel);
			registry.PublishPostProcessPreview(selection, channel, 74, current);
			context.Check(!registry.IsPostProcessPreviewRequested(channel) && !registry.IsDirty(index),
				"The matching preview revision and request generation settle independently");
		}

		constexpr auto GIChannel = PostProcessPreviewChannel::GlobalIllumination;
		const GIPreviewSnapshot giSnapshot{ .m_Generation = 19, .m_Samples = 64,
			.m_TargetSamples = 256, .m_MaximumDepth = 4 };
		RGDeferredCommitList giDiscarded;
		RGExecuteContext giExecute(giDiscarded);
		giExecute.DeferCommit([&registry, giSnapshot](const RGCommitContext&) noexcept
			{
				registry.PublishGIPreviewSnapshot(giSnapshot);
			});
		giDiscarded.Discard();
		context.Check(registry.GetPostProcessPreviewDiagnostics(GIChannel).m_GISnapshot.m_Generation == 0,
			"Discarded GI preview work cannot publish source provenance");
		registry.PublishGIPreviewSnapshot(giSnapshot);
		registry.SetGIPreviewSourceAvailable();
		registry.BeginPreviewFrame();
		const auto frozen = registry.GetPostProcessPreviewDiagnostics(GIChannel);
		context.Check(!frozen.m_SourceAvailable && frozen.m_GISnapshot.m_Generation == 19 &&
			frozen.m_GISnapshot.m_Samples == 64 && frozen.m_GISnapshot.m_MaximumDepth == 4 &&
			registry.GetPostProcessPreviewDiagnostics().m_GISnapshot.m_Generation == 0,
			"A pipeline change hides GI without relabeling the retained frozen snapshot or another channel");

		registry.EnsureIblResources();
		for (uint32_t i = 0; i < static_cast<uint32_t>(IBLPreviewType::Count); ++i)
		{
			const auto type = static_cast<IBLPreviewType>(i);
			registry.RequestIBLPreview(type);
			const auto observed = registry.ObserveIBLPreviewRequest(type);
			RGDeferredCommitList discarded;
			RGExecuteContext execute(discarded);
			execute.DeferCommit([&registry, type, observed](const RGCommitContext&) noexcept
				{
					registry.PublishIBLPreview(type, observed, IBLPreviewLayout::Cross, 0);
				});
			discarded.Discard();
			context.Check(registry.IsIBLPreviewRequested(type) && registry.IsIBLPreviewDirty(type) &&
				registry.GetIBLPreviewUpdateCount(type) == 0,
				"Discard leaves every IBL preview pending and dirty");
			registry.RequestIBLPreview(type);
			registry.MarkIBLPreviewDirty(type);
			registry.PublishIBLPreview(type, observed, IBLPreviewLayout::Cross, 0);
			context.Check(registry.IsIBLPreviewRequested(type) && registry.IsIBLPreviewDirty(type),
				"A stale IBL draw cannot clear a newer request or content revision");
			registry.PublishIBLPreview(type, registry.ObserveIBLPreviewRequest(type), IBLPreviewLayout::Grid2x3, 1);
			registry.RequestIBLPreview(type);
			const auto clean = registry.ObserveIBLPreviewRequest(type);
			registry.AcknowledgeIBLPreviewRequest(type, clean.m_Generation);
			context.Check(!registry.IsIBLPreviewRequested(type) && !registry.IsIBLPreviewDirty(type) &&
				registry.GetIBLPreviewUpdateCount(type) == 2,
				"A clean IBL request is satisfied without another submitted draw");
		}

		registry.RequestShadowPreview();
		registry.EnsureShadowPreviewResources();
		const auto shadow = registry.ObserveShadowPreviewRequest();
		constexpr auto shadowIndex = RenderTextureIndex::Preview_Shadow_DirectionalShadowMap;
		// A frame with no shadow pass performs no acknowledgement.
		RGDeferredCommitList empty;
		empty.CommitAll(submitted);
		context.Check(registry.ObserveShadowPreviewRequest().m_Requested && registry.IsDirty(shadowIndex),
			"A submitted frame without a shadow preview pass preserves its request");
		registry.RequestShadowPreview();
		registry.MarkDirty(shadowIndex);
		registry.PublishShadowPreview(shadow.m_Generation, shadow.m_ContentRevision);
		context.Check(registry.ObserveShadowPreviewRequest().m_Requested && registry.IsDirty(shadowIndex),
			"A shadow draw preserves newer request and content generations");
		const auto requestOnly = registry.ObserveShadowPreviewRequest();
		registry.RequestShadowPreview();
		registry.PublishShadowPreview(requestOnly.m_Generation, requestOnly.m_ContentRevision);
		context.Check(registry.ObserveShadowPreviewRequest().m_Requested && !registry.IsDirty(shadowIndex),
			"A newer request survives even when the drawn content revision is still current");
		const auto contentOnly = registry.ObserveShadowPreviewRequest();
		registry.MarkDirty(shadowIndex);
		registry.PublishShadowPreview(contentOnly.m_Generation, contentOnly.m_ContentRevision);
		context.Check(!registry.ObserveShadowPreviewRequest().m_Requested && registry.IsDirty(shadowIndex),
			"A newer content revision survives even when the drawn request generation is still current");
		registry.RequestShadowPreview();
		const auto currentShadow = registry.ObserveShadowPreviewRequest();
		registry.PublishShadowPreview(currentShadow.m_Generation, currentShadow.m_ContentRevision);
		context.Check(!registry.ObserveShadowPreviewRequest().m_Requested && !registry.IsDirty(shadowIndex),
			"A matching shadow preview submission acknowledges and initializes its content");
		const RHIFencePoint fence{ RHIFenceHandle{ 1, 1 }, 42 };
		registry.EnsureShadowPreviewResources(256, &fence);
		registry.CommitTextureContent(shadowIndex, currentShadow.m_ContentRevision);
		context.Check(registry.IsDirty(shadowIndex) &&
			registry.GetTextureContentRevision(shadowIndex) != currentShadow.m_ContentRevision,
			"Reallocation changes the content revision before any old clear can settle");
	}

	void RunPreviewPassContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		device.m_TextureViewsSupported = true;
		device.m_PassConstantsEnabled = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({ .m_Device = &device,
			.m_TransientResourcePool = &pool, .m_SamplerRegistry = &samplers });
		registry.EnsureIblResources();
		class Shaders final : public RenderShaderProgramAccess
		{
		public:
			ShaderID LoadProgram(const ShaderProgramRef&) noexcept override { return ShaderID{ 1 }; }
			uint64_t GetGeneration(ShaderID) const noexcept override { return 1; }
		} shaders;
		class Bindings final : public RenderBindingLayoutAccess
		{
		public:
			RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept override { return { 1, 1 }; }
			RHIBindingLayoutDesc GetCommonBindingLayoutDesc() const noexcept override { return {}; }
			const ComposedLayout* GetCommonLayoutContract() const noexcept override
			{
				return &scene_bindings::GetCommonLayoutContract();
			}
		} bindings;
		BindingTestResolver pipelines;
		BindingTestPresentation presentation;
		const RenderServices services{ .m_PipelineResolver = &pipelines, .m_ShaderPrograms = &shaders,
			.m_Samplers = &samplers, .m_Resources = &registry, .m_Presentation = &presentation,
			.m_BindingLayout = &bindings };
		RenderPassIBLClear clear;
		RenderPassIBLPreview preview;
		RenderPassIBL ibl;
		RenderPassPostProcessPreview post;
		RHIUploadPool uploads(device);
		for (uint32_t phase = 0; phase < 4; ++phase)
		{
			RHIUploadWriter writer;
			auto batch = uploads.BeginBatch();
			batch->Attach(writer);
			RenderFrameBuildResult frame;
			frame.m_FrameSerial = phase + 1;
			frame.m_RenderViews.resize(1);
			frame.m_RenderViews[0].m_Width = 64;
			frame.m_RenderViews[0].m_Height = 64;
			frame.m_RenderViews[0].m_DisplayWidth = 64;
			frame.m_RenderViews[0].m_DisplayHeight = 64;
			frame.m_FrameConstants.m_Scene = writer.UploadConstants(SceneGPU{});
			const std::array<ViewGPU, 1> views{};
			frame.m_FrameConstants.m_Views = writer.UploadStructured(std::span(views));
			const auto frameContext = frame.MakeRenderFrameContext();
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Imports {};
			graph.AddPass<Imports>("PreviewTest.Import",
				[&registry](RenderGraph::RGBuilder& builder, Imports&)
				{
					auto& sources = builder.GetBlackboard().Create<RGIBLResources>(IBLResourcesName);
					auto& previews = builder.GetBlackboard().Create<RGIBLPreviewResources>(IBLPreviewResourcesName);
					const std::array targets{
						&sources.m_EnvironmentCubemap, &sources.m_IrradianceCubemap,
						&sources.m_PrefilteredSpecularCubemap, &sources.m_BrdfLut,
						&previews.m_EnvironmentCubemapPreview, &previews.m_IrradianceCubemapPreview,
						&previews.m_PrefilteredSpecularCubemapPreview };
					for (size_t i = 0; i < targets.size(); ++i)
					{
						const auto index = static_cast<RenderTextureIndex>(i);
						const bool initialized = i < 4 ? registry.HasInitializedActiveIBL() : !registry.IsDirty(index);
						*targets[i] = builder.ImportTexture(std::format("PreviewTest.Texture{}", i).c_str(),
							registry.GetTextureHandle(index), *registry.GetTextureDesc(index), CommonRHIResourceState(),
							initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
					}
				});
			registry.RequestIBLPreview(IBLPreviewType::Environment);
			clear.AddPass(graph, frameContext, services);
			preview.AddPass(graph, frameContext, services);
			if (phase < 3)
			{
				registry.SetPostProcessPreviewSelection({ PostProcessDebugTap::SceneColor, 0 });
				registry.RequestPostProcessPreview();
				const RGPostProcessColor source{ graph.GetBlackboard().Get<RGIBLPreviewResources>(
					IBLPreviewResourcesName).m_EnvironmentCubemapPreview, PostProcessColorState::SceneLinearRec709, 1.0f };
				post.AddPassForTap(graph, frameContext, services, source, PostProcessDebugTap::SceneColor);
				post.AddPassForTap(graph, frameContext, services, source, PostProcessDebugTap::SceneColor);
			}
			ibl.AddFinishPass(graph);
			const bool compiled = graph.Compile();
			context.Check(compiled, "Actual IBL clear and preview producers compile with inspector consumers");
			if (!compiled)
			{
				batch->Detach(writer);
				batch->Cancel();
				return;
			}
			if (phase == 1)
			{
				registry.SetIBLEnvironmentPreviewLayout(IBLPreviewLayout::Grid2x3);
				registry.SetIBLEnvironmentPreviewMip(1);
				registry.RequestIBLPreview(IBLPreviewType::Environment);
				registry.RequestPostProcessPreview();
				registry.SetPostProcessPreviewExposureEV(1.0f);
			}
			RecordingGraphicsCommandContext graphics;
			graphics.BeginRecordingScope(writer);
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits, { .m_GraphicsCommandContext = &graphics });
			const auto before = registry.GetIBLPreviewUpdateCount(IBLPreviewType::Environment);
			graph.Execute(execute);
			commits.Seal();
			context.Check(registry.GetIBLPreviewUpdateCount(IBLPreviewType::Environment) == before &&
				(phase > 1 || !registry.HasInitializedActiveIBL()),
				"Actual execute lambdas leave publication and IBL initialization unchanged while recording");
			const RGCommitContext submitted{ frame.m_FrameSerial, { RHIFenceHandle{ 1, 1 }, phase + 1 } };
			if (phase == 0)
			{
				commits.Discard();
				context.Check(!registry.HasInitializedActiveIBL() && registry.IsPostProcessPreviewRequested() &&
					!registry.HasPublishedPostProcessPreview() && registry.IsIBLPreviewDirty(IBLPreviewType::Environment),
					"Discarding actual recorded preview passes preserves demand, dirty state and uninitialized IBL");
			}
			else
			{
				commits.CommitAll(submitted);
				const auto state = registry.GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
				context.Check(registry.HasInitializedActiveIBL() && state.m_UpdateCount == (phase < 3 ? phase : 2u) &&
					state.m_Dirty == (phase == 1) && state.m_Requested == (phase == 1) &&
					state.m_PublishedMip == (phase == 1 ? 0u : 1u) &&
					registry.IsPostProcessPreviewRequested() == (phase == 1) &&
					registry.GetPostProcessPreviewUpdateCount() == (phase < 3 ? phase : 2u),
					"Actual passes publish built selections, preserve newer requests, retry once and leave clean IBL cached");
			}
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			context.Check(std::ranges::count(snapshot.m_Passes, "PostProcess.Preview", &RGSnapshotPassInfo::m_Name) ==
				(phase < 3 ? 1 : 0), "Graph-local preview deduplication keeps one producer per channel");
			graphics.EndRecordingScope();
			graph.Retire(submitted.m_Fence);
			batch->Detach(writer);
			batch->Cancel();
		}
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingPreviewsSelfTests(SelfTestContext& context) noexcept
	{
		RunIBLPreviewContractTests(context);
		RunPostProcessPreviewContractTests(context);
		RunShadowPreviewContractTests(context);
		RunPreviewSubmissionContractTests(context);
		RunPreviewPassContractTests(context);
	}
}
