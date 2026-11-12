#include "AssetTestServices.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/DirectionalShadowFramePlan.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/TypedConstantBinding.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12PipelineDescUtils.h"
#include "Graphics/RHI/RHIUploadPool.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderPass/AtmosphereBindingLayout.h"
#include "Graphics/RenderPass/ForwardBindingLayout.h"
#include "Graphics/RenderPass/ForwardShadingCommon.h"
#include "Graphics/RenderSceneBuilder.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Utility/DXGIFormatUtils.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace gglab::rendering_test
{
	template <typename T>
	concept HasPublicPresent = requires(T & value) { value.Present(); };

	template <typename T>
	concept HasPublicFrameCompletionWait = requires(T & value) { value.WaitFrameCompletion(); };

	template <typename T>
	concept HasPublicCurrentBackBufferIndex = requires(const T & value) { value.GetCurrentBackBufferIndex(); };

	void RunPassParameterContractTests(SelfTestContext& context) noexcept
	{
		const auto layout = Renderer::BuildCommonRHIBindingLayoutDesc();
		const auto passIndex = static_cast<uint32_t>(CommonRSRootParamIndex::PassCB);
		const auto inlineIndex = static_cast<uint32_t>(CommonRSRootParamIndex::InlineConstants);
		context.Check(layout.m_Slots[passIndex].m_Type == RHIBindingType::ConstantBuffer &&
			layout.m_Slots[passIndex].m_Binding == 2 &&
			layout.m_Slots[inlineIndex].m_Type == RHIBindingType::InlineConstants &&
			layout.m_Slots[inlineIndex].m_Binding == 4,
			"Common binding layout separates b2 pass buffers from b4 inline constants");
		DX12BindingLayoutBuildResult lowered{};
		BuildDX12RootSignatureDesc(layout, lowered);
		context.Check(lowered.m_RootParameters[passIndex].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV &&
			lowered.m_RootParameters[inlineIndex].ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
			lowered.m_RootParameters[inlineIndex].Constants.Num32BitValues == MaxInlineConstantDWORDs,
			"DX12 lowers pass buffers to root CBVs while retaining immediate constants");
		const auto commonPlan = BuildDX12RootParameterPlan(
			{ lowered.m_Desc.Desc_1_1.pParameters, lowered.m_Desc.Desc_1_1.NumParameters });
		context.Check(commonPlan.m_BufferBindingTypes[passIndex] == RHIBindingType::ConstantBuffer &&
			commonPlan.m_BufferBindingTypes[static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB)] ==
				RHIBindingType::ReadOnlyStorageBuffer &&
			commonPlan.m_InlineConstantSizesInBytes[inlineIndex] == MaxInlineConstantDWORDs * sizeof(uint32_t) &&
			commonPlan.m_InlineConstantSizesInBytes[passIndex] == 0,
			"The DX12 native parameter plan keeps common CBV, SRV and inline capacities distinct");
		RHIBindingLayoutDesc compactLayout{};
		compactLayout.m_SlotCount = 8;
		compactLayout.m_Slots[0] = { .m_Type = RHIBindingType::BindlessResourceTable, .m_Count = 0 };
		compactLayout.m_Slots[2] = { .m_Type = RHIBindingType::InlineConstants, .m_SizeInBytes = 16 };
		compactLayout.m_Slots[3] = { .m_Type = RHIBindingType::ConstantBuffer };
		compactLayout.m_Slots[4] = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer };
		compactLayout.m_Slots[5] = { .m_Type = RHIBindingType::ReadWriteStorageBuffer };
		compactLayout.m_Slots[6] = { .m_Type = RHIBindingType::ConstantBuffer, .m_Count = 2 };
		compactLayout.m_Slots[7] = { .m_Type = RHIBindingType::BindlessSamplerTable, .m_Count = 0 };
		const auto compactPlan = [&compactLayout]()
			{
				DX12BindingLayoutBuildResult native{};
				BuildDX12RootSignatureDesc(compactLayout, native);
				return BuildDX12RootParameterPlan(
					{ native.m_Desc.Desc_1_1.pParameters, native.m_Desc.Desc_1_1.NumParameters });
			}();
		context.Check(compactPlan.m_InlineConstantSizesInBytes[0] == 16 &&
			compactPlan.m_BufferBindingTypes[1] == RHIBindingType::ConstantBuffer &&
			compactPlan.m_BufferBindingTypes[2] == RHIBindingType::ReadOnlyStorageBuffer &&
			compactPlan.m_BufferBindingTypes[3] == RHIBindingType::ReadWriteStorageBuffer,
			"The cached native plan skips unknown and bindless slots and owns values beyond descriptor lifetime");
		context.Check(compactPlan.m_BufferBindingTypes[4] == RHIBindingType::Unknown &&
			compactPlan.m_InlineConstantSizesInBytes[4] == 0 &&
			!IsValidInlineConstantWrite(compactPlan.m_InlineConstantSizesInBytes[4], 1, 0) &&
			compactPlan.m_BufferBindingTypes.back() == RHIBindingType::Unknown &&
			compactPlan.m_InlineConstantSizesInBytes.back() == 0,
			"Descriptor tables and unused native parameters cannot be bound as root buffers or inline words");
		compactLayout.m_Slots[2].m_SizeInBytes = 8;
		DX12BindingLayoutBuildResult alternateNative{};
		BuildDX12RootSignatureDesc(compactLayout, alternateNative);
		const auto alternatePlan = BuildDX12RootParameterPlan(
			{ alternateNative.m_Desc.Desc_1_1.pParameters, alternateNative.m_Desc.Desc_1_1.NumParameters });
		context.Check(compactPlan.m_InlineConstantSizesInBytes[0] == 16 &&
			alternatePlan.m_InlineConstantSizesInBytes[0] == 8 &&
			IsValidInlineConstantWrite(compactPlan.m_InlineConstantSizesInBytes[0], 2, 2) &&
			!IsValidInlineConstantWrite(alternatePlan.m_InlineConstantSizesInBytes[0], 2, 2),
			"Switching native plans changes inline capacity without mutating a cached earlier layout");
		context.Check(IsValidInlineConstantWrite(32, 8, 0) && IsValidInlineConstantWrite(32, 2, 6) &&
			!IsValidInlineConstantWrite(32, 2, 7) && !IsValidInlineConstantWrite(32, 1, UINT32_MAX) &&
			!IsValidInlineConstantWrite(0, 1, 0) && !IsValidInlineConstantWrite(32, 0, 0),
			"Inline writes reject non-inline bindings, empty payloads and overflowing partial writes");

		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_UseControlledFenceCompletion = true;
		device.m_CompletedFenceValue = 0;
		RHIUploadPool uploads(device);
		RHIUploadWriter writer;
		auto batch = uploads.BeginBatch();
		TransientResourcePool graphResources(&device);
		RecordingGraphicsCommandContext graphics;
		RecordingComputeCommandContext compute;
		// Both encoders share the frame writer, as in the backend submission owners.
		const auto beginRecording = [&](RHIUploadBatch& uploadBatch) noexcept
			{
				uploadBatch.Attach(writer);
				graphics.BeginRecordingScope(writer);
				compute.BeginRecordingScope(writer);
			};
		const auto endRecording = [&](RHIUploadBatch& uploadBatch) noexcept
			{
				graphics.EndRecordingScope();
				compute.EndRecordingScope();
				uploadBatch.Detach(writer);
			};
		beginRecording(*batch);
		struct InlinePayload
		{
			float m_Value;
			uint32_t m_Flags;
		};
		graphics.SetInlineConstants(inlineIndex, InlinePayload{ 1.5f, 0xabcdef01u }, 2);
		std::array<uint32_t, 2> inlineWords{ 11, 19 };
		compute.SetInlineConstants(inlineIndex, std::span(inlineWords), 3);
		inlineWords.fill(0);
		context.Check(graphics.m_LastInlineParameter == inlineIndex && graphics.m_LastInlineOffset == 2 &&
			graphics.m_LastInlineWords == std::vector<uint32_t>({ std::bit_cast<uint32_t>(1.5f), 0xabcdef01u }) &&
			compute.m_LastInlineOffset == 3 && compute.m_LastInlineWords == std::vector<uint32_t>({ 11, 19 }),
			"Public inline entry points preserve typed payload bits, span elements and word offsets");
		RGDeferredCommitList executionCommits;
		RGExecuteContext execution(executionCommits, { &graphics, &compute, nullptr });
		std::array<uint32_t, 24> payload{};
		payload.fill(0x12345678u);
		bool uploaded = true;
		RenderGraph graph({ &device, &graphResources });
		graph.AddTrivialSideEffectPass("Constants.Graphics", [&](RGExecuteContext&)
			{
				uploaded &= graphics.SetConstantBufferData(passIndex, payload);
				payload.fill(0);
				const std::array<uint32_t, 2> words{ 17, 3 };
				graphics.SetInlineConstants(inlineIndex, std::span(words), 1);
			});
		graph.AddTrivialSideEffectPass("Constants.Compute", RGPassEncoderType::Compute,
			[&](RGExecuteContext&)
			{
				payload.fill(0xabcdef01u);
				uploaded &= compute.SetConstantBufferData(passIndex, payload);
				const std::array<uint32_t, 2> words{ 23, 5 };
				compute.SetInlineConstants(inlineIndex, std::span(words), 2);
				std::array<uint32_t, MaxPassConstantBufferBytes / sizeof(uint32_t)> large{};
				for (uint32_t index = 0; index < 5; ++index)
				{
					large.back() = index + 1;
					uploaded &= compute.SetConstantBufferData(passIndex, large);
				}
			});
		context.Check(graph.Compile(), "Constant upload graph compiles");
		graph.Execute(execution);
		executionCommits.Discard();
		context.Check(uploaded && graphics.m_ConstantBindings.size() == 1 &&
			compute.m_ConstantBindings.size() == 6,
			"Graphics and compute upload typed parameters larger than root constants");
		const auto first = graphics.m_ConstantBindings.front();
		const auto second = compute.m_ConstantBindings.front();
		uint32_t firstWord = 0;
		uint32_t secondWord = 0;
		std::memcpy(&firstWord,
			device.m_PassConstantStorage[first.m_Buffer.Index() - 1].data() + first.m_Offset,
			sizeof(firstWord));
		std::memcpy(&secondWord,
			device.m_PassConstantStorage[second.m_Buffer.Index() - 1].data() + second.m_Offset,
			sizeof(secondWord));
		context.Check(first.m_Buffer == second.m_Buffer && first.m_Offset != second.m_Offset &&
			first.m_Offset % 256 == 0 && second.m_Offset % 256 == 0 &&
			firstWord == 0x12345678u && secondWord == 0xabcdef01u,
			"Aligned slices retain immutable CPU snapshots");
		context.Check(device.m_PassConstantPublishCount == 0,
			"Executing a graph does not publish or seal submission-owned uploads");
		RenderGraph secondGraph({ &device, &graphResources });
		secondGraph.AddTrivialSideEffectPass("Constants.SecondGraph", [&](RGExecuteContext&)
			{
				uploaded &= graphics.SetConstantBufferData(passIndex, payload);
			});
		context.Check(secondGraph.Compile(), "A second graph shares the recording batch");
		executionCommits = RGDeferredCommitList{};
		secondGraph.Execute(execution);
		executionCommits.Discard();
		const auto reusable = graphics.UploadConstants(std::span(payload));
		context.Check(uploaded && reusable.m_SizeInBytes == sizeof(payload) &&
			graphics.SetConstantBuffer(passIndex, reusable) && compute.SetConstantBuffer(passIndex, reusable),
			"Non-graph uploads can be bound repeatedly by graphics and compute in the same batch");
		context.Check(compute.SetConstantBufferData(passIndex, std::span(payload)),
			"Span parameter overloads upload their elements rather than the span object");
		context.Check(batch->GetPageCount() == 2 && batch->Publish() &&
			device.m_PassConstantPublishCount == 2 &&
			graphics.m_LastInlineWords == std::vector<uint32_t>({ 17, 3 }) && graphics.m_LastInlineOffset == 1 &&
			compute.m_LastInlineWords == std::vector<uint32_t>({ 23, 5 }) && compute.m_LastInlineOffset == 2,
			"Submission publishes all pages once while inline partial updates retain word offsets");
		context.Check(device.m_PassConstantMapCount == 2 && device.m_PassConstantUnmapCount == 0,
			"Pages are mapped once at creation and publication flushes without ending the mapping");
		const RHIFencePoint submittedFence{ RHIFenceHandle{ 1, 1 }, 5 };
		const auto presentFailed = RHIFrameEndResult::Fatal(submittedFence);
		context.Check(batch->Submitted(presentFailed.GetSubmittedFence()),
			"Submitted uploads remain fence-gated even when presentation fails");
		endRecording(*batch);
		graph.Retire(submittedFence);
		secondGraph.Retire(submittedFence);
		uploads.ReclaimCompleted();
		context.Check(uploads.GetPendingPageCount() == 2,
			"In-flight upload pages are retained independently of graph lifetime");

		auto cancelled = uploads.BeginBatch();
		beginRecording(*cancelled);
		const auto later = graphics.UploadConstants(payload);
		context.Check(later.IsValid() && later.m_Buffer != first.m_Buffer && uploads.GetPageCount() == 3,
			"A later submission cannot overwrite an in-flight page");
		context.Check(!graphics.SetConstantBuffer(passIndex, reusable) && graphics.HasRecordingError() &&
			cancelled->HasFailed() && !cancelled->Publish() && !cancelled->Submitted(submittedFence),
			"Cross-batch binding fails the recording transaction and prevents normal submission");
		endRecording(*cancelled);
		cancelled->Cancel();
		auto reuse = uploads.BeginBatch();
		beginRecording(*reuse);
		const auto afterCancel = graphics.UploadConstants(payload);
		context.Check(afterCancel.m_Buffer == later.m_Buffer &&
			afterCancel.m_UploadBatchId != later.m_UploadBatchId,
			"Cancellation immediately recovers discarded storage with a new batch identity");
		context.Check(device.m_PassConstantMapCount == 3 && device.m_PassConstantUnmapCount == 0,
			"Cancellation keeps pages mapped and a reused page is not mapped again");
		endRecording(*reuse);
		reuse->Cancel();
		device.m_CompletedFenceValue = 5;
		uploads.ReclaimCompleted();
		context.Check(uploads.GetPendingPageCount() == 0,
			"Submitted pages become reusable only after GPU completion");

		const uint32_t unmapsBeforeLimitedPool = device.m_PassConstantUnmapCount;
		{
			RHIUploadPool limited(device, { .m_PageSizeInBytes = 256, .m_MaxPagesPerBatch = 1 });
			auto exhausted = limited.BeginBatch();
			beginRecording(*exhausted);
			context.Check(graphics.SetConstantBufferData(passIndex, payload) &&
				!compute.SetConstantBufferData(passIndex, payload) && graphics.HasRecordingError() &&
				compute.HasRecordingError() && !exhausted->Publish(),
				"Page exhaustion poisons every encoder sharing the submission batch");
			endRecording(*exhausted);
			exhausted->Cancel();
			context.Check(device.m_PassConstantUnmapCount == unmapsBeforeLimitedPool,
				"A failed publication and its cancellation leave the page mapped");
		}
		context.Check(device.m_PassConstantUnmapCount == unmapsBeforeLimitedPool + 1,
			"Pool destruction ends each page mapping exactly once");

		for (uint32_t failure = 0; failure < 2; ++failure)
		{
			RecordingDevice failingDevice;
			failingDevice.m_PassConstantsEnabled = true;
			failingDevice.m_ConstantCreateFails = failure == 0;
			failingDevice.m_ConstantMapFails = failure == 1;
			RHIUploadPool failingPool(failingDevice);
			auto failed = failingPool.BeginBatch();
			beginRecording(*failed);
			context.Check(!graphics.SetConstantBufferData(passIndex, payload) && failed->HasFailed(),
				"Buffer creation and mapping failures fail the recording transaction");
			endRecording(*failed);
			failed->Cancel();
		}
		auto sealed = uploads.BeginBatch();
		context.Check(sealed->Publish() && !sealed->Upload(std::as_bytes(std::span(payload))).IsValid(),
			"Publication freezes upload allocation before submission");
		sealed->Cancel();
		auto publicationFailure = uploads.BeginBatch();
		beginRecording(*publicationFailure);
		device.m_ConstantPublishFails = true;
		context.Check(graphics.UploadConstants(payload).IsValid() &&
			!publicationFailure->Publish() && graphics.HasRecordingError() &&
			compute.HasRecordingError() &&
			!publicationFailure->Submitted(submittedFence),
			"Host write publication failures fail every encoder and prevent normal submission");
		device.m_ConstantPublishFails = false;
		endRecording(*publicationFailure);
		publicationFailure->Cancel();
		auto oversized = uploads.BeginBatch();
		std::vector<std::byte> tooLarge(device.GetUploadLimits().m_MaxConstantRangeInBytes + 4);
		context.Check(!oversized->Upload(tooLarge).IsValid() && oversized->HasFailed(),
			"Constant uploads reject ranges beyond device capabilities");
		oversized->Cancel();
		RecordingGraphicsCommandContext unscoped;
		context.Check(!unscoped.SetConstantBufferData(passIndex, payload) && unscoped.HasRecordingError(),
			"Data uploads require an explicit recording scope outside the RenderGraph too");
	}

	void RunFrameConstantUploadTests(SelfTestContext& context) noexcept
	{
		asset_test::AssetManagerHarness assets("scene-frame-constants");
		context.Check(assets.IsValid(), "Scene constant tests compose headless asset services");
		if (!assets.IsValid())
		{
			return;
		}
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_CreateValidDescriptors = true;
		TransientResourcePool textures(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry resources({ .m_Device = &device,
			.m_TransientResourcePool = &textures, .m_SamplerRegistry = &samplers });
		resources.EnsureIblResources({ .m_EnvironmentCubemapSize = 4,
			.m_IrradianceCubemapSize = 4, .m_PrefilteredSpecularCubemapSize = 4 });
		EnvironmentLightingSystem environment({ .m_RenderResourceRegistry = &resources });
		environment.SetIntensity(1.5f);
		environment.SetRotationRadians(0.25f);
		environment.ResolveWorldLighting(std::nullopt, std::nullopt, 0);
		TransferManager transfers(std::make_unique<RecordingTransferContext>());
		World world;
		PersistentStructuredBuffer<ObjectGPU> objects({ .m_Device = &device, .m_ElementCapacity = 4 });
		PersistentStructuredBuffer<MaterialGPU> materials({ .m_Device = &device, .m_ElementCapacity = 4 });
		PersistentStructuredBuffer<LightGPU> lights({ .m_Device = &device, .m_ElementCapacity = 4 });
		PersistentStructuredBufferTable<uint64_t, ObjectGPU> objectTable(4, 1);
		PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU> materialTable(4, 1);
		PersistentStructuredBufferTable<uint64_t, LightGPU> lightTable(4, 1);
		class FrameBuffers final : public RenderFrameBufferAccess
		{
		public:
			FrameBuffers(PersistentStructuredBuffer<ObjectGPU>& objects,
				PersistentStructuredBuffer<MaterialGPU>& materials,
				PersistentStructuredBuffer<LightGPU>& lights) noexcept :
				m_Objects(objects), m_Materials(materials), m_Lights(lights)
			{
			}
			const PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() const noexcept override { return &m_Objects; }
			PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() noexcept override { return &m_Objects; }
			const PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() const noexcept override { return &m_Materials; }
			PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() noexcept override { return &m_Materials; }
			const PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer() const noexcept override { return &m_Lights; }
			PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer() noexcept override { return &m_Lights; }

		private:
			PersistentStructuredBuffer<ObjectGPU>& m_Objects;
			PersistentStructuredBuffer<MaterialGPU>& m_Materials;
			PersistentStructuredBuffer<LightGPU>& m_Lights;
		} buffers(objects, materials, lights);
		const RenderServices services{ .m_FrameBuffers = &buffers };
		std::array<RenderView, 1> cameraViews{};
		cameraViews[0].m_ViewId = RenderViewID::Main;
		cameraViews[0].m_IsValid = true;
		cameraViews[0].m_CameraPosition = { 1.0f, 2.0f, 3.0f };
		cameraViews[0].m_Near = 0.1f;
		RenderView displayView = cameraViews[0];
		displayView.m_CameraPosition = { 7.0f, 8.0f, 9.0f };
		DirectionalShadowFramePlan shadows;
		shadows.m_Cascades.resize(2);
		shadows.m_Cascades[0].m_SplitNear = 0.1f;
		shadows.m_Cascades[0].m_View.m_CameraPosition = { 4.0f, 5.0f, 6.0f };
		shadows.m_Cascades[0].m_SplitFar = 12.0f;
		shadows.m_Cascades[1].m_SplitFar = 40.0f;
		shadows.m_DistanceFadeStart = 35.0f;
		shadows.m_DistanceFadeInvRange = 0.2f;
		RenderSceneBuilder builder;
		RHIUploadWriter writer;
		for (uint32_t scenario = 0; scenario < 5; ++scenario)
		{
			RHIUploadPool pool(device, { .m_PageSizeInBytes = scenario >= 3 ? 256u : 4096u,
				.m_MaxUploadSizeInBytes = scenario == 2 ? static_cast<uint32_t>(sizeof(SceneGPU)) - 4u : 64u * 1024u,
				.m_MaxBytesPerBatch = scenario >= 3 ? (scenario - 2u) * 256u : 16u * 1024u });
			auto batch = pool.BeginBatch();
			batch->Attach(writer);
			const auto uploads = writer.GetFrameUploads();
			if (scenario == 1)
			{
				context.Check(uploads.UploadConstants(uint32_t{ 0x12345678 }).IsValid(),
					"Scene uploads can follow an earlier composition upload in the shared batch");
			}
			auto built = builder.Build({ .m_World = world, .m_AssetManager = assets.GetAssets(),
				.m_SamplerRegistry = samplers, .m_TransferManager = transfers,
				.m_RenderResourceRegistry = resources, .m_EnvironmentLightingSystem = environment,
				.m_RenderViews = cameraViews, .m_DirectionalShadowFramePlan = shadows,
				.m_Uploads = uploads, .m_ObjectsSB = objects, .m_MaterialsSB = materials,
				.m_LightsSB = lights, .m_ObjectTable = objectTable, .m_MaterialTable = materialTable,
				.m_LightTable = lightTable, .m_PostTemporalView = scenario == 1 ? &displayView : nullptr });
			if (scenario < 2)
			{
				const auto& scene = built.m_Constants.m_Scene;
				const auto& shadow = built.m_Constants.m_DirectionalShadow;
				const auto& views = built.m_Constants.m_Views;
				const uint32_t viewCount = scenario == 1 ? 4u : 3u;
				context.Check(built.m_Status == RenderSceneBuildStatus::Ready &&
					writer.Owns(scene) && writer.Owns(shadow) && writer.Owns(views) &&
					scene.m_SizeInBytes == sizeof(SceneGPU) && shadow.m_SizeInBytes == sizeof(DirectionalShadowGPU) &&
					views.m_OffsetInBytes != 0 && views.m_StrideInBytes == sizeof(ViewGPU) &&
					views.m_SizeInBytes == viewCount * sizeof(ViewGPU),
					"One scene build produces three frame bindings, including a nonzero-offset ViewGPU slice");
				SceneGPU sceneData{};
				std::memcpy(&sceneData, device.m_PassConstantStorage[scene.m_Buffer.Index() - 1].data() +
					scene.m_OffsetInBytes, sizeof(sceneData));
				DirectionalShadowGPU shadowData{};
				std::memcpy(&shadowData, device.m_PassConstantStorage[shadow.m_Buffer.Index() - 1].data() +
					shadow.m_OffsetInBytes, sizeof(shadowData));
				const auto expectedShadow = BuildDirectionalShadowGPU(shadows, built.m_ShadowViewBaseOffset);
				context.Check(sceneData.ObjectCount == 0 && sceneData.MaterialCount == 0 && sceneData.ViewCount == viewCount &&
					sceneData.ViewBaseIndex == 0 && built.m_RenderScene.m_ViewBaseIndex == 0 &&
					sceneData.LightCount == 4 && sceneData.IBLResource.EnvironmentIntensity == 1.5f &&
					sceneData.IBLResource.EnvironmentRotationRadians == 0.25f &&
					std::memcmp(&shadowData, &expectedShadow, sizeof(shadowData)) == 0,
					"Scene values preserve lighting and exact cascade data while using slice-relative view indices");
				std::vector<ViewGPU> viewData(viewCount);
				std::memcpy(viewData.data(), device.m_PassConstantStorage[views.m_Buffer.Index() - 1].data() +
					views.m_OffsetInBytes, views.m_SizeInBytes);
				context.Check(viewData[0].CameraPos.m_X == 1.0f && viewData[0].Near == 0.1f &&
					viewData[1].CameraPos.m_X == 4.0f && built.m_ShadowViewBaseOffset == 1 &&
					(scenario != 1 || (viewData[3].CameraPos.m_X == 7.0f && built.m_PostTemporalViewOffset == 3)),
					"Camera, cascade and post-temporal views retain their ordering and relative addresses within the slice");
				RenderFrameBuilder::BuildResult privateFrame;
				privateFrame.m_FrameConstants = built.m_Constants;
				privateFrame.m_Uploads = uploads;
				const auto privateContext = privateFrame.MakeRenderFrameContext();
				RenderFrameBuildResult frame;
				frame.m_FrameConstants = privateContext.m_FrameConstants;
				frame.m_Uploads = privateContext.m_Uploads;
				frame.m_RenderScene = built.m_RenderScene;
				frame.m_RenderSceneStatus = built.m_Status;
				RenderFrameBuildResult moved = std::move(frame);
				const auto frameContext = moved.MakeRenderFrameContext();
				context.Check(frameContext.m_Uploads.IsCurrent() &&
					writer.Owns(frameContext.m_FrameConstants.m_Scene) &&
					writer.Owns(frameContext.m_FrameConstants.m_DirectionalShadow) &&
					writer.Owns(frameContext.m_FrameConstants.m_Views),
					"Private and public frame contexts carry complete bindings and preserve them across a move");
				{
					RenderFrameBuildResult rejectedFrame;
					rejectedFrame.m_FrameConstants = built.m_Constants;
				}
				context.Check(writer.Owns(scene) && writer.Owns(shadow) && writer.Owns(views) &&
					pool.GetStatistics().m_Leased.m_PageCount != 0,
					"Rejecting a CPU frame cannot retire its RHI-owned upload storage before cancellation");
				RecordingGraphicsCommandContext graphics;
				RecordingComputeCommandContext compute;
				graphics.BeginRecordingScope(writer);
				compute.BeginRecordingScope(writer);
				class LayoutPipelineSystem final : public RHIPipelineSystem
				{
				public:
					RHIBindingLayoutHandle CreateBindingLayout(const RHIBindingLayoutDesc&) noexcept override { return { 1, 1 }; }
					RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineCreateInfo&) noexcept override { return {}; }
					RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineCreateInfo&) noexcept override { return {}; }
					bool IsAlive(RHIBindingLayoutHandle handle) const noexcept override { return handle.IsValid(); }
					bool IsAlive(RHIPipelineHandle handle) const noexcept override { return handle.IsValid(); }
					uint64_t GetRevision() const noexcept override { return 1; }
					void Clear() noexcept override {}
				} pipelines;
				const auto forwardLayout = ForwardBindingLayout::Create(
					pipelines, scene_bindings::GetCommonLayoutContract(), false);
				const auto& typedScene = frameContext.m_FrameConstants.m_SceneSnapshot;
				const auto& typedShadow = frameContext.m_FrameConstants.m_DirectionalShadowSnapshot;
				const auto matchesRaw = [](const auto& typed, const RHIConstantBufferBinding& raw)
					{
						const auto& binding = typed.GetBinding();
						return typed.IsValid() && binding.m_Buffer == raw.m_Buffer &&
							binding.m_OffsetInBytes == raw.m_OffsetInBytes &&
							binding.m_SizeInBytes == raw.m_SizeInBytes &&
							binding.m_UploadBatchId == raw.m_UploadBatchId;
					};
				context.Check(matchesRaw(typedScene, scene) && matchesRaw(typedShadow, shadow),
					"Producer-created typed snapshots preserve the exact raw binding across frame moves");
				const auto neutralSnapshot = TypedConstantSnapshot<AtmosphereGPU>::Upload(
					uploads, MakeNeutralAtmosphere());
				const auto forwardContract = scene_bindings::ComposeForwardLayout(
					scene_bindings::GetCommonLayoutContract(), false);
				const auto forwardParameter = [&forwardContract](LayoutBindingKey key) noexcept
					{
						return forwardContract.IsSuccess() ?
							forwardContract.m_Layout->FindParameterIndex(key) :
							std::nullopt;
					};
				const auto shadowParameter = forwardParameter(scene_bindings::DirectionalShadow);
				const auto atmosphereParameter = forwardParameter(scene_bindings::AtmosphereConstants);
				for (uint32_t pass = 0; pass < 3; ++pass)
				{
					const auto& queue = moved.m_RenderQueues[0];
					auto constants = forwardLayout.BeginRecording(graphics);
					context.Check(constants.SetPipeline(ResolveBindingTestPipeline(forwardLayout)) &&
						forward_shading::BindSceneResources(graphics, frameContext, services,
							{ .m_AtmosphereSnapshot = neutralSnapshot, .m_RasterDomain = &queue.m_CoverageRasterDomain },
							queue, forwardLayout, constants),
						"Forward consumes typed Scene, Shadow and neutral Atmosphere snapshots");
				}
				const auto& neutralBinding = neutralSnapshot.GetBinding();
				bool bindingsMatch = graphics.m_ConstantBindings.size() == 9 &&
					shadowParameter.has_value() && atmosphereParameter.has_value();
				for (size_t pass = 0; bindingsMatch && pass < 3; ++pass)
				{
					const auto& sceneBinding = graphics.m_ConstantBindings[pass * 3];
					const auto& shadowBinding = graphics.m_ConstantBindings[pass * 3 + 1];
					const auto& atmosphereBinding = graphics.m_ConstantBindings[pass * 3 + 2];
					bindingsMatch &= sceneBinding.m_ParameterIndex == static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB) &&
						sceneBinding.m_Buffer == scene.m_Buffer && sceneBinding.m_Offset == scene.m_OffsetInBytes &&
						shadowBinding.m_ParameterIndex == *shadowParameter &&
						shadowBinding.m_Buffer == shadow.m_Buffer && shadowBinding.m_Offset == shadow.m_OffsetInBytes &&
						atmosphereBinding.m_ParameterIndex == *atmosphereParameter &&
						atmosphereBinding.m_Buffer == neutralBinding.m_Buffer &&
						atmosphereBinding.m_Offset == neutralBinding.m_OffsetInBytes;
				}
				const auto matchesViews = [&views](const auto& binding) noexcept
					{
						return binding.m_Buffer == views.m_Buffer &&
							binding.m_OffsetInBytes == views.m_OffsetInBytes &&
							binding.m_SizeInBytes == views.m_SizeInBytes &&
							binding.m_StrideInBytes == views.m_StrideInBytes &&
							binding.m_UploadBatchId == views.m_UploadBatchId;
					};
				context.Check(bindingsMatch &&
					graphics.m_ReadOnlyBindings.size() == 3 &&
					std::ranges::all_of(graphics.m_ReadOnlyBindings, matchesViews) &&
					compute.SetConstantBuffer(0, frameContext.m_FrameConstants.m_Scene) &&
					compute.SetReadOnlyBuffer(1, views) && matchesViews(compute.m_ReadOnlyBindings.back()) &&
					!batch->HasFailed(),
					"Several shading passes and direct compute reuse all frame bindings without another upload");
				const auto resolved = ResolvedRenderLayout::Create(
					pipelines, scene_bindings::GetCommonLayoutContract());
				const auto sceneReference = ResolvedBindingRef<SceneGPU>::Resolve(
					resolved, scene_bindings::SceneConstants);
				context.Check(BindConstantSnapshot(compute, resolved, sceneReference, typedScene),
					"Typed compute binding retains the producer's original Scene snapshot");
				graphics.EndRecordingScope();
				compute.EndRecordingScope();
				context.Check(batch->Publish(), "Prepared scene constants publish with their frame batch");

				RHIUploadPool rejectionPool(device, { .m_PageSizeInBytes = 4096u });
				RHIUploadWriter rejectionWriter;
				auto rejectedBatch = rejectionPool.BeginBatch();
				rejectedBatch->Attach(rejectionWriter);
				const auto currentScene = TypedConstantSnapshot<SceneGPU>::Upload(
					rejectionWriter.GetFrameUploads(), SceneGPU{});
				graphics.BeginRecordingScope(rejectionWriter);
				const auto otherLayout = ResolvedRenderLayout::Create(
					pipelines, scene_bindings::GetCommonLayoutContract());
				context.Check(!BindConstantSnapshot(graphics, otherLayout, sceneReference, currentScene) &&
					graphics.HasRecordingError() && rejectedBatch->HasFailed(),
					"A wrong logical reference poisons recording and the shared batch in every configuration");
				graphics.EndRecordingScope();
				rejectedBatch->Detach(rejectionWriter);
				rejectedBatch->Cancel();
				auto nextBatch = rejectionPool.BeginBatch();
				nextBatch->Attach(rejectionWriter);
				graphics.BeginRecordingScope(rejectionWriter);
				context.Check(!BindConstantSnapshot(graphics, resolved, sceneReference, currentScene) &&
					graphics.HasRecordingError() && nextBatch->HasFailed(),
					"A correctly typed snapshot from a cancelled batch still fails encoder ownership validation");
				graphics.EndRecordingScope();
				nextBatch->Detach(rejectionWriter);
				nextBatch->Cancel();
				auto missingBatch = rejectionPool.BeginBatch();
				missingBatch->Attach(rejectionWriter);
				graphics.BeginRecordingScope(rejectionWriter);
				context.Check(!BindConstantSnapshot(graphics, resolved, sceneReference, TypedConstantSnapshot<SceneGPU>{}) &&
					graphics.HasRecordingError() && missingBatch->HasFailed(),
					"An unavailable typed snapshot rejects recording through the shared batch failure path");
				graphics.EndRecordingScope();
				missingBatch->Detach(rejectionWriter);
				missingBatch->Cancel();
			}
			else
			{
				context.Check(built.m_Status == RenderSceneBuildStatus::GpuUploadFailed && batch->HasFailed() &&
					built.m_Constants.m_Scene.IsValid() == (scenario >= 3) &&
					built.m_Constants.m_DirectionalShadow.IsValid() == (scenario == 4) &&
					!built.m_Constants.m_Views.IsValid() && !batch->Publish(),
					"A failed Scene, Shadow or View upload disables scene rendering and prevents publication");
			}
			batch->Detach(writer);
			batch->Cancel();
			const auto statistics = pool.GetStatistics();
			const uint64_t expectedUploads = scenario < 2 ?
				4u + (scenario == 1 ? 1u : 0u) :
				scenario - 2u;
			context.Check(statistics.m_LastBatch.m_UploadCount == expectedUploads &&
				statistics.m_CancelledBatchCount == 1 && !uploads.IsCurrent(),
				"Scene/Shadow/Views and the shared neutral snapshot cancel without a ring lease or duplicate upload");
		}
	}

	void RunRHIFrameAndGraphicsScopeContractTests(SelfTestContext& context) noexcept
	{
		static_assert(!HasPublicPresent<RHISwapChain>);
		static_assert(!HasPublicFrameCompletionWait<RHISwapChain>);
		static_assert(!HasPublicCurrentBackBufferIndex<RHISwapChain>);

		const RHIFrameBeginResult unavailable = RHIFrameBeginResult::Unavailable();
		const RHIFrameBeginResult fatalBegin = RHIFrameBeginResult::Fatal();
		context.Check(unavailable.IsUnavailable() && !unavailable.IsReady() &&
			unavailable.GetFrame() == nullptr && fatalBegin.IsFatal() &&
			fatalBegin.GetFrame() == nullptr,
			"RHI BeginFrame distinguishes temporary unavailability from fatal failure without a frame");

		const RHIFencePoint submittedFence{ RHIFenceHandle{ 7, 1 }, 19 };
		const RHIFrameEndResult completed = RHIFrameEndResult::Completed(submittedFence);
		const RHIFrameEndResult fatalAfterSubmit = RHIFrameEndResult::Fatal(submittedFence);
		const RHIFrameEndResult fatalBeforeSubmit = RHIFrameEndResult::Fatal();
		context.Check(completed.IsCompleted() && completed.HasSubmission() &&
			completed.GetSubmittedFence() == submittedFence &&
			fatalAfterSubmit.IsFatal() && fatalAfterSubmit.HasSubmission() &&
			fatalAfterSubmit.GetSubmittedFence() == submittedFence &&
			fatalBeforeSubmit.IsFatal() && !fatalBeforeSubmit.HasSubmission(),
			"RHI EndFrame separates transaction completion from submitted-fence lifetime semantics");

		constexpr uint32_t FrameSlotCount = 2;
		constexpr uint32_t SwapChainImageCount = 3;
		RenderFrameBuildResult syntheticFrame{};
		syntheticFrame.m_FrameSlotIndex = 1;
		syntheticFrame.m_BackBufferIndex = 2;
		syntheticFrame.m_FrameSerial = 17;
		const RenderFrameContext renderContext = syntheticFrame.MakeRenderFrameContext();
		std::array<uint32_t, FrameSlotCount> frameSlotStorage{};
		std::array<uint32_t, SwapChainImageCount> swapChainImageStorage{};
		frameSlotStorage[renderContext.m_FrameSlotIndex] = 11;
		swapChainImageStorage[renderContext.m_BackBufferIndex] = 29;
		context.Check(renderContext.m_FrameSlotIndex < FrameSlotCount &&
			renderContext.m_BackBufferIndex < SwapChainImageCount &&
			renderContext.m_FrameSlotIndex != renderContext.m_BackBufferIndex &&
			frameSlotStorage[1] == 11 && swapChainImageStorage[2] == 29,
			"Frame context preserves independent frame-slot and swapchain-image indices");

		const auto& bgraUnorm = GetRHIFormatInfo(RHIFormat::B8G8R8A8Unorm);
		const auto& bgraSrgb = GetRHIFormatInfo(RHIFormat::B8G8R8A8UnormSrgb);
		context.Check(bgraUnorm.m_Family == RHIFormatFamily::B8G8R8A8 &&
			bgraSrgb.m_Family == RHIFormatFamily::B8G8R8A8 &&
			AreRHIFormatsInSameFamily(bgraUnorm.m_Format, bgraSrgb.m_Format) &&
			!AreRHIFormatsInSameFamily(RHIFormat::B8G8R8A8Unorm, RHIFormat::R8G8B8A8Unorm) &&
			bgraUnorm.m_BytesPerBlock == 4,
			"Pure RHI format metadata keeps BGRA8 in its own color-view family");

		bool locationsAreExplicit = true;
		for (uint32_t layoutIndex = 0; layoutIndex < static_cast<uint32_t>(InputLayoutID::Count); ++layoutIndex)
		{
			const auto vertexLayout = BuildRHIVertexInputLayoutDesc(static_cast<InputLayoutID>(layoutIndex));
			locationsAreExplicit &= vertexLayout.m_AttributeCount > 0;
			for (uint32_t attributeIndex = 0; attributeIndex < vertexLayout.m_AttributeCount; ++attributeIndex)
			{
				locationsAreExplicit &=
					vertexLayout.m_Attributes[attributeIndex].m_Location == attributeIndex;
			}
		}
		context.Check(locationsAreExplicit,
			"Built-in RHI vertex layouts assign deterministic explicit attribute locations");
		const auto shadowInput = BuildRHIVertexInputLayoutDesc(InputLayoutID::MeshPositionUVs, ShaderBinaryFormat::SpirV);
		context.Check(shadowInput.m_AttributeCount == 3 && shadowInput.m_VertexBufferCount == 1 &&
			shadowInput.m_VertexBuffers[0].m_StrideInBytes == 56 &&
			shadowInput.m_Attributes[0].m_AlignedByteOffset == 0 &&
			shadowInput.m_Attributes[1].m_AlignedByteOffset == 24 &&
			shadowInput.m_Attributes[2].m_AlignedByteOffset == 32 &&
			shadowInput.m_Attributes[0].m_Location == 0 &&
			shadowInput.m_Attributes[1].m_Location == 2 &&
			shadowInput.m_Attributes[2].m_Location == 3 &&
			shadowInput.m_Attributes[1].m_SemanticIndex == 0 &&
			shadowInput.m_Attributes[2].m_SemanticIndex == 1,
			"Shadow mesh input omits unused normal/tangent without repacking vertices or UV semantics");
		GraphicsPhysicalPipelineKey shadowRecipe;
		shadowRecipe.m_InputLayoutId = InputLayoutID::MeshPositionUVs;
		const auto dxilShadow = BuildRHIGraphicsPipelineDesc(shadowRecipe, ShaderBinaryFormat::Dxil);
		const auto spirvShadow = BuildRHIGraphicsPipelineDesc(shadowRecipe, ShaderBinaryFormat::SpirV);
		context.Check(dxilShadow.m_VertexInput.m_AttributeCount == 5 &&
			spirvShadow.m_VertexInput.m_AttributeCount == 3 &&
			dxilShadow.m_VertexInput.m_VertexBuffers[0].m_StrideInBytes == 56,
			"Shadow pipeline lowering preserves the full DXIL signature and prunes only SPIR-V inputs");

		RHIRenderingSignature renderingSignature{};
		renderingSignature.m_ColorFormats[0] = RHIFormat::R8G8B8A8Unorm;
		renderingSignature.m_ColorAttachmentCount = 1;
		renderingSignature.m_DepthFormat = RHIFormat::D32Float;
		renderingSignature.m_SampleCount = 4;
		RHIGraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
		pipelineDesc.m_RenderTargetCount = 1;
		pipelineDesc.m_DepthStencilFormat = RHIFormat::D32Float;
		pipelineDesc.m_SampleCount = 4;
		const bool matchingSignature = IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
		pipelineDesc.m_RenderTargetFormats[0] = RHIFormat::B8G8R8A8Unorm;
		const bool rejectsColorMismatch = !IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
		pipelineDesc.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
		pipelineDesc.m_DepthStencilFormat = RHIFormat::Unknown;
		const bool rejectsDepthMismatch = !IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
		pipelineDesc.m_DepthStencilFormat = RHIFormat::D32Float;
		pipelineDesc.m_SampleCount = 1;
		const bool rejectsSampleMismatch = !IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
		context.Check(matchingSignature && rejectsColorMismatch && rejectsDepthMismatch &&
			rejectsSampleMismatch,
			"Graphics pipeline compatibility covers active color, depth, and sample signature");
	}

	void RunDX12GraphicsContractLoweringTests(SelfTestContext& context) noexcept
	{
		const bool bgraRoundTrips =
			ToDXGIFormat(RHIFormat::B8G8R8A8Unorm) == DXGI_FORMAT_B8G8R8A8_UNORM &&
			ToDXGIFormat(RHIFormat::B8G8R8A8UnormSrgb) == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
			ToRHIFormat(DXGI_FORMAT_B8G8R8A8_UNORM) == RHIFormat::B8G8R8A8Unorm &&
			ToRHIFormat(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) == RHIFormat::B8G8R8A8UnormSrgb;
		context.Check(bgraRoundTrips,
			"DX12 preserves actual BGRA8 swapchain formats through RHI conversion");

		GraphicsPhysicalPipelineKey recipe{};
		recipe.m_InputLayoutId = InputLayoutID::P3N3T2;
		const RHIGraphicsPipelineDesc baseDesc = BuildRHIGraphicsPipelineDesc(recipe);
		RHIGraphicsPipelineDesc changedDesc = baseDesc;
		++changedDesc.m_VertexInput.m_Attributes[1].m_Location;
		const DX12GraphicsPipelineShaderInputs shaders{};
		const auto baseKey = MakeDX12RHIGraphicsPSOKey(baseDesc, RootSignatureID{ 3 }, shaders);
		const auto changedKey = MakeDX12RHIGraphicsPSOKey(changedDesc, RootSignatureID{ 3 }, shaders);
		context.Check(baseKey != changedKey,
			"DX12 normalized graphics keys include explicit RHI vertex locations");

		const RHIBufferBarrier copyBarrier{
			.m_Before = {
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::CopySource,
			},
			.m_After = {
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			},
		};
		auto* nativeResource = reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 });
		const D3D12_BUFFER_BARRIER nativeBarrier = BuildD3D12BufferBarrier(copyBarrier, nativeResource);
		context.Check(nativeBarrier.SyncBefore == D3D12_BARRIER_SYNC_COPY &&
			nativeBarrier.SyncAfter == D3D12_BARRIER_SYNC_COPY &&
			nativeBarrier.AccessBefore == D3D12_BARRIER_ACCESS_COPY_SOURCE &&
			nativeBarrier.AccessAfter == D3D12_BARRIER_ACCESS_COPY_DEST &&
			nativeBarrier.pResource == nativeResource,
			"DX12 lowers buffer transitions to the exact Enhanced Barrier contract");
	}

	void RunScopedConstantRecordingTests(SelfTestContext& context) noexcept
	{
		constexpr std::array scenarioNames
		{
			"complete inputs",
			"missing all inputs",
			"missing Shadow",
			"external pipeline switch",
			"scoped switch without rebinding",
			"scoped switch with complete rebinding",
			"foreign logical reference",
			"cancelled batch snapshot",
			"unavailable snapshot",
			"expired recording scope",
			"overlapping contract scope",
			"same-pipeline reselection",
			"invalid pipeline selection",
			"numeric writes without typed readiness",
			"numeric overwrite after typed readiness",
			"repeatable ready state",
			"foreign logical pipeline",
			"wrong pipeline bind point",
			"failed pipeline resolution",
			"expired logical pipeline",
			"missing Atmosphere",
		};
		BindingTestPipelineSystem pipelines;
		const auto composed = scene_bindings::ComposeForwardLayout(scene_bindings::GetCommonLayoutContract(), false);
		const auto layout = ResolvedRenderLayout::Create(pipelines, *composed.m_Layout);
		const auto other = ResolvedRenderLayout::Create(pipelines, *composed.m_Layout);
		const auto sceneRef = ResolvedBindingRef<SceneGPU>::Resolve(layout, scene_bindings::SceneConstants);
		const auto shadowRef = ResolvedBindingRef<DirectionalShadowGPU>::Resolve(layout, scene_bindings::DirectionalShadow);
		const auto otherScene = ResolvedBindingRef<SceneGPU>::Resolve(other, scene_bindings::SceneConstants);
		const auto atmosphereRef = ResolvedBindingRef<AtmosphereGPU>::Resolve(layout, scene_bindings::AtmosphereConstants);
		context.Check(layout.GetNativeLayout() == other.GetNativeLayout(),
			"Recording rejection fixtures share one native layout while retaining independent logical contracts");
		static_assert(sizeof(SceneGPU) == 96 && sizeof(AtmosphereGPU) == 128);
		static_assert(!std::is_invocable_v<decltype(&BindConstantSnapshot<RHIGraphicsCommandContext, SceneGPU>),
			RHIGraphicsCommandContext&, const ResolvedRenderLayout&, const ResolvedBindingRef<SceneGPU>&,
			const TypedConstantSnapshot<AtmosphereGPU>&>);

		const auto run = [&]<typename Command>()
			{
				RecordingDevice device;
				device.m_PassConstantsEnabled = true;
				RHIUploadPool pool(device, { .m_PageSizeInBytes = 4096u });
				RHIUploadWriter writer;
				auto previousBatch = pool.BeginBatch();
				previousBatch->Attach(writer);
				const auto staleScene = TypedConstantSnapshot<SceneGPU>::Upload(writer.GetFrameUploads(), SceneGPU{});
				previousBatch->Detach(writer);
				previousBatch->Cancel();
				for (uint32_t scenario = 0; scenario < scenarioNames.size(); ++scenario)
				{
					auto batch = pool.BeginBatch();
					batch->Attach(writer);
					const auto scene = TypedConstantSnapshot<SceneGPU>::Upload(writer.GetFrameUploads(), SceneGPU{});
					const auto shadow = TypedConstantSnapshot<DirectionalShadowGPU>::Upload(
						writer.GetFrameUploads(), DirectionalShadowGPU{});
					const auto atmosphere = TypedConstantSnapshot<AtmosphereGPU>::Upload(
						writer.GetFrameUploads(), MakeNeutralAtmosphere());
					Command command;
					command.BeginRecordingScope(writer);
					{
						ScopedConstantBindings<Command> bindings(command, layout, sceneRef, shadowRef, atmosphereRef);
						const bool selected = bindings.SetPipeline(ResolveBindingTestPipeline<Command>(layout));
						bool bound = selected;
						if (scenario != 1 && scenario != 13)
						{
							bound &= bindings.Bind(sceneRef, scenario == 7 ? staleScene :
								scenario == 8 ? TypedConstantSnapshot<SceneGPU>{} : scene);
						}
						if (scenario != 1 && scenario != 2 && scenario != 13)
						{
							bound &= bindings.Bind(shadowRef, shadow);
						}
						if (scenario != 1 && scenario != 13 && scenario != 20)
						{
							bound &= bindings.Bind(atmosphereRef, atmosphere);
						}
						if (scenario == 3 || scenario == 11)
						{
							command.SetPipeline(RHIPipelineHandle{ scenario == 3 ? 2u : 1u, 1 });
						}
						if (scenario == 4 || scenario == 5)
						{
							bound &= bindings.SetPipeline(ResolveBindingTestPipeline<Command>(layout, 2));
							if (scenario == 5)
							{
								bound &= bindings.Bind(sceneRef, scene) && bindings.Bind(shadowRef, shadow) &&
									bindings.Bind(atmosphereRef, atmosphere);
							}
						}
						if (scenario == 6)
						{
							bound &= bindings.Bind(otherScene, scene);
						}
						if (scenario == 9)
						{
							command.EndRecordingScope();
							command.BeginRecordingScope(writer);
							bound &= bindings.Bind(sceneRef, scene);
						}
						if (scenario == 10)
						{
							ScopedConstantBindings<Command> replacement(command, other, otherScene);
							bound &= replacement.SetPipeline(ResolveBindingTestPipeline<Command>(other));
						}
						if (scenario == 12)
						{
							bound &= bindings.SetPipeline({});
						}
						if (scenario == 13 || scenario == 14)
						{
							// Numeric writes neither satisfy nor preserve a typed capability.
							bound &= command.SetConstantBuffer(*sceneRef.GetParameterIndex(layout), scene.GetBinding());
							bound &= command.SetConstantBuffer(*shadowRef.GetParameterIndex(layout), shadow.GetBinding());
						}
						if (scenario == 16)
						{
							bound &= bindings.SetPipeline(ResolveBindingTestPipeline<Command>(other));
						}
						if (scenario == 17)
						{
							using OtherContext = std::conditional_t<std::derived_from<Command, RHIGraphicsCommandContext>,
								RHIComputeCommandContext, RHIGraphicsCommandContext>;
							bound &= bindings.SetPipeline(ResolveBindingTestPipeline<OtherContext>(layout));
						}
						if (scenario == 18)
						{
							BindingTestResolver failedResolver;
							failedResolver.m_Pipeline = {};
							GraphicsPipelineSlot slot;
							bound &= bindings.SetPipeline(layout.Resolve(failedResolver, slot,
								GraphicsPhysicalPipelineKey{ .m_BindingLayout = layout.GetNativeLayout() }, {}));
						}
						if (scenario == 19)
						{
							const auto expired = [&pipelines, &composed]
								{
									const auto temporary = ResolvedRenderLayout::Create(pipelines, *composed.m_Layout);
									return ResolveBindingTestPipeline<Command>(temporary);
								}();
							bound &= bindings.SetPipeline(expired);
						}
						if constexpr (std::derived_from<Command, RHIGraphicsCommandContext>)
						{
							if (scenario % 2 == 0)
							{
								command.DrawIndexed(3, 1, 0, 0, 0);
							}
							else
							{
								command.Draw(3, 1, 0, 0);
							}
						}
						else
						{
							command.Dispatch(1, 1, 1);
						}
						if (scenario == 15)
						{
							if constexpr (std::derived_from<Command, RHIGraphicsCommandContext>)
							{
								command.Draw(3, 1, 0, 0);
							}
							else
							{
								command.Dispatch(1, 1, 1);
							}
						}
						const bool expected = scenario == 0 || scenario == 5 || scenario == 15;
						const uint32_t expectedCommandCount = scenario == 15 ? 2u : 1u;
						context.Check(expected ? bound && command.m_CommandCount == expectedCommandCount && !batch->HasFailed() :
							command.m_CommandCount == 0 && command.HasRecordingError() && batch->HasFailed(),
							std::format("Scoped {} command: {} enforces required inputs at execution in every configuration",
								std::derived_from<Command, RHIGraphicsCommandContext> ? "graphics" : "compute", scenarioNames[scenario]));
					}
					command.EndRecordingScope();
					batch->Detach(writer);
					batch->Cancel();
					context.Check(pool.GetStatistics().m_LastBatch.m_UploadCount == 3,
						"Scoped pipeline rebinding reuses all three original uploads without duplicating GPU storage");
				}
			};
		run.template operator()<RecordingGraphicsCommandContext>();
		run.template operator()<RecordingComputeCommandContext>();

		const auto atmosphereLayout = AtmosphereBindingLayout::Create(pipelines, scene_bindings::GetCommonLayoutContract());
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		RHIUploadPool pool(device);
		RHIUploadWriter writer;
		auto batch = pool.BeginBatch();
		batch->Attach(writer);
		const auto snapshot = TypedConstantSnapshot<AtmosphereGPU>::Upload(writer.GetFrameUploads(), AtmosphereGPU{});
		RecordingComputeCommandContext command;
		command.BeginRecordingScope(writer);
		{
			auto bindings = atmosphereLayout.BeginRecording(command);
			context.Check(atmosphereLayout.IsValid() && bindings.SetPipeline(ResolveBindingTestPipeline<RHIComputeCommandContext>(atmosphereLayout)) &&
				atmosphereLayout.Bind(bindings, snapshot), "The Atmosphere adapter resolves and binds its own typed b5 meaning");
			command.Dispatch(1, 1, 1);
			context.Check(command.m_CommandCount == 1 && command.m_ConstantBindings.size() == 1 &&
				command.m_ConstantBindings.front().m_ParameterIndex ==
					scene_bindings::GetCommonLayoutContract().GetParameterCount() &&
				!batch->HasFailed(),
				"Atmosphere recording uses fixed b5 after the common parameters");
		}
		{
			const auto scene = TypedConstantSnapshot<SceneGPU>::Upload(writer.GetFrameUploads(), SceneGPU{});
			auto bindings = atmosphereLayout.BeginRecording(command);
			context.Check(bindings.SetPipeline(ResolveBindingTestPipeline<RHIComputeCommandContext>(atmosphereLayout)) && !bindings.Bind(sceneRef, scene),
				"A Scene reference cannot satisfy the independent Atmosphere scope");
			command.Dispatch(1, 1, 1);
			context.Check(command.m_CommandCount == 1 && command.HasRecordingError() && batch->HasFailed(),
				"A logical Scene-to-Atmosphere switch rejects stale typed state before dispatch");
		}
		command.EndRecordingScope();
		batch->Detach(writer);
		batch->Cancel();
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingBindingsSelfTests(SelfTestContext& context) noexcept
	{
		RunPassParameterContractTests(context);
		RunFrameConstantUploadTests(context);
		RunScopedConstantRecordingTests(context);
		RunRHIFrameAndGraphicsScopeContractTests(context);
		RunDX12GraphicsContractLoweringTests(context);
	}
}
