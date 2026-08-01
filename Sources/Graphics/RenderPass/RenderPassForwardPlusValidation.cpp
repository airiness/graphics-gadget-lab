#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassForwardPlusValidation.h"

#include "Graphics/Pipeline/ForwardPlus.h"
#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/ForwardPlusValidationGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		enum class TileRootParameter : uint32_t
		{
			PassConstants,
			OutputMetrics,
		};

		enum class FrameRootParameter : uint32_t
		{
			PassConstants,
			TileMetrics,
			OutputMetrics,
		};

		struct HdrDiffParameters
		{
			uint32_t m_SceneColorTextureIndex = 0;
			uint32_t m_LegacyReferenceTextureIndex = 0;
			uint32_t m_DepthTextureIndex = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_DepthConvention = 0;
			uint32_t m_TileCountX = 0;
			uint32_t m_TileCount = 0;
		};
		static_assert(IsPassRootConstantStruct<HdrDiffParameters>);
		static_assert(sizeof(HdrDiffParameters) == 32);

		struct TilePassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_LegacyReference{};
			RGTextureId m_Depth{};
			RGTextureViewId m_SceneColorSrv{};
			RGTextureViewId m_LegacyReferenceSrv{};
			RGTextureViewId m_DepthSrv{};
			RGBufferId m_TileMetrics{};
			ForwardPlusTileGrid m_TileGrid{};
			DepthConvention m_DepthConvention = DepthConvention::Standard;
		};

		struct FramePassData
		{
			RGBufferId m_TileMetrics{};
			RGBufferId m_FrameMetrics{};
			ForwardPlusTileGrid m_TileGrid{};
		};

		struct ReadbackPassData
		{
			RGBufferId m_FrameMetrics{};
			RGBufferId m_Readback{};
			uint32_t m_BufferIndex = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint64_t m_FrameSerial = 0;
		};

		void AppendBindingSlot(RHIBindingLayoutDesc& desc, RHIBindingType type,
			RHIShaderStage visibility, uint32_t binding, uint32_t sizeInBytes,
			const char* debugName) noexcept
		{
			GGLAB_ASSERT(desc.m_SlotCount < desc.MaxSlots);
			desc.m_Slots[desc.m_SlotCount++] = {
				.m_Type = type,
				.m_Visibility = visibility,
				.m_Binding = binding,
				.m_Space = 0,
				.m_Count = IsBindlessBindingType(type) ? 0u : 1u,
				.m_SizeInBytes = sizeInBytes,
				.m_DebugName = debugName,
			};
		}

		RHIBindingLayoutDesc BuildTileBindingLayout() noexcept
		{
			RHIBindingLayoutDesc desc{};
			desc.m_DebugName = "ForwardPlusHdrDiff.TileBindingLayout";
			AppendBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::Compute, 0,
				sizeof(HdrDiffParameters), "HdrDiffConstants");
			AppendBindingSlot(desc, RHIBindingType::ReadWriteStorageBuffer,
				RHIShaderStage::Compute, 0, 0, "TileMetrics");
			AppendBindingSlot(desc, RHIBindingType::BindlessSampledTextureTable,
				RHIShaderStage::Compute, 0, 0, "BindlessTextures");
			return desc;
		}

		RHIBindingLayoutDesc BuildFrameBindingLayout() noexcept
		{
			RHIBindingLayoutDesc desc{};
			desc.m_DebugName = "ForwardPlusHdrDiff.FrameBindingLayout";
			AppendBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::Compute, 0,
				sizeof(HdrDiffParameters), "HdrDiffConstants");
			AppendBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer,
				RHIShaderStage::Compute, 0, 0, "TileMetrics");
			AppendBindingSlot(desc, RHIBindingType::ReadWriteStorageBuffer,
				RHIShaderStage::Compute, 0, 0, "FrameMetrics");
			return desc;
		}
	}

	void RenderPassForwardPlusValidation::Prepare(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Passes/PassForwardPlusValidation.hlsl";
		shaderDesc.m_Stage = ShaderStage::Compute;
		shaderDesc.m_Entry = L"CSReduceTiles";
		shaderDesc.m_Defines = {
			{
				.m_Name = L"GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_TILES",
				.m_Value = L"1",
			},
		};
		m_TilePipelineRecipe.m_CSId = shaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Entry = L"CSReduceFrame";
		shaderDesc.m_Defines = {
			{
				.m_Name = L"GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_FRAME",
				.m_Value = L"1",
			},
		};
		m_FramePipelineRecipe.m_CSId = shaderManager->LoadShader(shaderDesc);

		auto* rhiContext = renderer->GetRHIContext();
		GGLAB_ASSERT_NOT_NULL(rhiContext);
		auto& pipelineSystem = rhiContext->GetPipelineSystem();
		m_TilePipelineRecipe.m_BindingLayout =
			pipelineSystem.CreateBindingLayout(BuildTileBindingLayout());
		m_FramePipelineRecipe.m_BindingLayout =
			pipelineSystem.CreateBindingLayout(BuildFrameBindingLayout());

		if (!m_TilePipelineRecipe.m_CSId.IsValid() ||
			!m_TilePipelineRecipe.m_BindingLayout.IsValid() ||
			!m_FramePipelineRecipe.m_CSId.IsValid() ||
			!m_FramePipelineRecipe.m_BindingLayout.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Forward+ HDR diff failed to prepare its shaders or binding layouts.");
			GGLAB_UNREACHABLE("Forward+ HDR diff pipeline is unavailable.");
		}
		m_IsInitialized = true;
	}

	void RenderPassForwardPlusValidation::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_IsInitialized, "Forward+ HDR diff must be prepared before graph construction.");
		GGLAB_ASSERT_NOT_NULL(m_DebugReadback.get());
		if (!m_DebugReadback)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<TilePassData>(
			"Lighting.ForwardPlus.HdrDiffTiles", RGPassEncoderType::Compute,
			[displayViewId](RenderGraph::RGBuilder& builder, TilePassData& data)
			{
				builder.SideEffect();
				auto& blackboard = builder.GetBlackboard();
				auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				auto& validation = blackboard.Get<RGForwardPlusValidationResources>(
					ForwardPlusValidationResourcesName);
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				GGLAB_ASSERT_MSG(validation.IsValid(),
					"Forward+ HDR diff requires the opaque legacy reference target.");

				const RHITextureDesc& sceneColorDesc = builder.GetTextureDesc(targets.m_SceneColor);
				data.m_TileGrid = MakeForwardPlusTileGrid(
					sceneColorDesc.m_Extent.m_Width, sceneColorDesc.m_Extent.m_Height);
				data.m_DepthConvention = sceneDepth.m_Convention;
				GGLAB_ASSERT_MSG(data.m_TileGrid.IsValid(),
					"Forward+ HDR diff requires a non-empty display extent.");

				data.m_SceneColor = builder.Read(
					targets.m_SceneColor, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_LegacyReference = builder.Read(validation.m_LegacyReferenceColor,
					RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_Depth = builder.Read(
					sceneDepth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_SceneColorSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_SceneColor);
				data.m_LegacyReferenceSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_LegacyReference);
				data.m_DepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					data.m_Depth, sceneDepth.m_SrvDesc);

				RHIBufferDesc tileMetricsDesc{};
				tileMetricsDesc.m_SizeInBytes =
					static_cast<uint64_t>(data.m_TileGrid.m_TileCount) * sizeof(uint32_t) * 4;
				tileMetricsDesc.m_StrideInBytes = sizeof(uint32_t) * 4;
				validation.m_TileMetrics =
					builder.CreateBuffer("ForwardPlus.HdrDiffTileMetrics", tileMetricsDesc);
				builder.WriteInPlace(validation.m_TileMetrics, RGBufferAccess::StorageWrite,
					RHIStage::ComputeShader);
				data.m_TileMetrics = validation.m_TileMetrics;
			},
			[this, renderer](RGExecuteContext& executeContext, TilePassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const auto sceneColorSrv = executeContext.GetViewDescriptor(data.m_SceneColorSrv);
				const auto legacyReferenceSrv =
					executeContext.GetViewDescriptor(data.m_LegacyReferenceSrv);
				const auto depthSrv = executeContext.GetViewDescriptor(data.m_DepthSrv);
				const RHIBufferHandle tileMetrics =
					executeContext.GetBufferHandle(data.m_TileMetrics);
				GGLAB_ASSERT_MSG(sceneColorSrv.IsValid() && legacyReferenceSrv.IsValid() &&
					depthSrv.IsValid() && tileMetrics.IsValid(),
					"Forward+ HDR diff tile resources must resolve before dispatch.");

				commandContext->SetPipeline(GetOrCreateTilePipeline(*renderer));
				commandContext->SetReadWriteBuffer(
					static_cast<uint32_t>(TileRootParameter::OutputMetrics), tileMetrics);
				commandContext->SetPushConstants(
					static_cast<uint32_t>(TileRootParameter::PassConstants),
					HdrDiffParameters{
						.m_SceneColorTextureIndex = sceneColorSrv.m_Index,
						.m_LegacyReferenceTextureIndex = legacyReferenceSrv.m_Index,
						.m_DepthTextureIndex = depthSrv.m_Index,
						.m_Width = data.m_TileGrid.m_Width,
						.m_Height = data.m_TileGrid.m_Height,
						.m_DepthConvention = static_cast<uint32_t>(data.m_DepthConvention),
						.m_TileCountX = data.m_TileGrid.m_TileCountX,
						.m_TileCount = data.m_TileGrid.m_TileCount,
					});
				commandContext->Dispatch(
					data.m_TileGrid.m_TileCountX, data.m_TileGrid.m_TileCountY, 1);
			});

		rg.AddPass<FramePassData>(
			"Lighting.ForwardPlus.HdrDiffFrame", RGPassEncoderType::Compute,
			[](RenderGraph::RGBuilder& builder, FramePassData& data)
			{
				builder.SideEffect();
				auto& validation = builder.GetBlackboard().Get<
					RGForwardPlusValidationResources>(ForwardPlusValidationResourcesName);
				data.m_TileMetrics = builder.Read(
					validation.m_TileMetrics, RGBufferAccess::StorageRead, RHIStage::ComputeShader);

				const RHITextureDesc& referenceDesc =
					builder.GetTextureDesc(validation.m_LegacyReferenceColor);
				data.m_TileGrid = MakeForwardPlusTileGrid(
					referenceDesc.m_Extent.m_Width, referenceDesc.m_Extent.m_Height);
				RHIBufferDesc frameMetricsDesc{};
				frameMetricsDesc.m_SizeInBytes = sizeof(uint32_t) * 4;
				frameMetricsDesc.m_StrideInBytes = sizeof(uint32_t) * 4;
				validation.m_FrameMetrics =
					builder.CreateBuffer("ForwardPlus.HdrDiffFrameMetrics", frameMetricsDesc);
				builder.WriteInPlace(validation.m_FrameMetrics, RGBufferAccess::StorageWrite,
					RHIStage::ComputeShader);
				data.m_FrameMetrics = validation.m_FrameMetrics;
			},
			[this, renderer](RGExecuteContext& executeContext, FramePassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const RHIBufferHandle tileMetrics =
					executeContext.GetBufferHandle(data.m_TileMetrics);
				const RHIBufferHandle frameMetrics =
					executeContext.GetBufferHandle(data.m_FrameMetrics);
				GGLAB_ASSERT_MSG(tileMetrics.IsValid() && frameMetrics.IsValid(),
					"Forward+ HDR diff frame resources must resolve before dispatch.");

				commandContext->SetPipeline(GetOrCreateFramePipeline(*renderer));
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(FrameRootParameter::TileMetrics), tileMetrics);
				commandContext->SetReadWriteBuffer(
					static_cast<uint32_t>(FrameRootParameter::OutputMetrics), frameMetrics);
				commandContext->SetPushConstants(
					static_cast<uint32_t>(FrameRootParameter::PassConstants),
					HdrDiffParameters{
						.m_Width = data.m_TileGrid.m_Width,
						.m_Height = data.m_TileGrid.m_Height,
						.m_TileCountX = data.m_TileGrid.m_TileCountX,
						.m_TileCount = data.m_TileGrid.m_TileCount,
					});
				commandContext->Dispatch(1, 1, 1);
			});

		const auto debugReadback = m_DebugReadback;
		const uint32_t bufferIndex = context.m_BackBufferIndex;
		const uint64_t frameSerial = context.m_FrameSerial;
		rg.AddPass<ReadbackPassData>(
			"Lighting.ForwardPlus.HdrDiffReadback", RGPassEncoderType::Copy,
			[debugReadback, bufferIndex, frameSerial](
				RenderGraph::RGBuilder& builder, ReadbackPassData& data)
			{
				builder.SideEffect();
				auto& validation = builder.GetBlackboard().Get<
					RGForwardPlusValidationResources>(ForwardPlusValidationResourcesName);
				data.m_FrameMetrics = builder.Read(
					validation.m_FrameMetrics, RGBufferAccess::CopySource, RHIStage::Copy);
				const RHITextureDesc& referenceDesc =
					builder.GetTextureDesc(validation.m_LegacyReferenceColor);
				data.m_Width = referenceDesc.m_Extent.m_Width;
				data.m_Height = referenceDesc.m_Extent.m_Height;

				RHIBufferDesc readbackDesc{};
				readbackDesc.m_SizeInBytes = ForwardPlusDebugReadback::HdrDiffReadbackSizeInBytes;
				readbackDesc.m_Usage = RHIBufferUsage::CopyDest;
				readbackDesc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
				readbackDesc.m_DebugName = "ForwardPlus.HdrDiffReadback";
				data.m_Readback = builder.ImportBuffer("ForwardPlus.HdrDiffReadback",
					debugReadback->GetHdrDiffBuffer(bufferIndex), readbackDesc,
					RGBufferAccess::CopyDest);
				builder.WriteInPlace(data.m_Readback, RGBufferAccess::CopyDest, RHIStage::Copy);
				data.m_BufferIndex = bufferIndex;
				data.m_FrameSerial = frameSerial;
			},
			[debugReadback](RGExecuteContext& executeContext, ReadbackPassData& data)
			{
				auto* commandContext = executeContext.GetCopyCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const RHIBufferHandle frameMetrics =
					executeContext.GetBufferHandle(data.m_FrameMetrics);
				const RHIBufferHandle readback = executeContext.GetBufferHandle(data.m_Readback);
				commandContext->CopyBuffer(readback, 0, frameMetrics, 0,
					ForwardPlusDebugReadback::HdrDiffReadbackSizeInBytes);
				debugReadback->MarkHdrDiffScheduled(data.m_BufferIndex, data.m_FrameSerial,
					data.m_Width, data.m_Height);
			});
	}

	RHIPipelineHandle RenderPassForwardPlusValidation::GetOrCreateTilePipeline(
		const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_TilePipelineSlot, m_TilePipelineRecipe, GetInfo());
	}

	RHIPipelineHandle RenderPassForwardPlusValidation::GetOrCreateFramePipeline(
		const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_FramePipelineSlot, m_FramePipelineRecipe, GetInfo());
	}
}
