#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassForwardPlusCull.h"

#include "Graphics/GPUStructures.h"
#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		enum class ForwardPlusRootParameter : uint32_t
		{
			PassConstants,
			ViewBuffer,
			LightBuffer,
			TileHeaders,
			TileIndices,
			TileDepthRanges,
		};

		struct ForwardPlusCullParameters
		{
			uint32_t m_DepthTextureIndex = 0;
			uint32_t m_ViewIndex = 0;
			uint32_t m_LightBaseIndex = 0;
			uint32_t m_LightCount = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_TileCountX = 0;
			uint32_t m_TileCountY = 0;
		};
		static_assert(IsPassRootConstantStruct<ForwardPlusCullParameters>);
		static_assert(sizeof(ForwardPlusCullParameters) == 32);

		struct PassData
		{
			RGTextureId m_Depth{};
			RGTextureViewId m_DepthSrv{};
			RGBufferId m_TileHeaders{};
			RGBufferId m_TileIndices{};
			RGBufferId m_TileDepthRanges{};
			ForwardPlusTileGrid m_TileGrid{};
			uint32_t m_ViewIndex = 0;
			uint32_t m_LightBaseIndex = 0;
			uint32_t m_LightCount = 0;
			bool m_DiagnosticsEnabled = false;
		};

		struct ReadbackPassData
		{
			RGBufferId m_TileHeaders{};
			RGBufferId m_TileIndices{};
			RGBufferId m_TileDepthRanges{};
			RGBufferId m_Readback{};
			RGBufferId m_GridReadback{};
			uint64_t m_HeaderSourceOffset = 0;
			uint64_t m_IndicesSourceOffset = 0;
			ForwardPlusTileGrid m_TileGrid{};
			uint32_t m_BufferIndex = 0;
			uint32_t m_TileX = 0;
			uint32_t m_TileY = 0;
			uint64_t m_FrameSerial = 0;
		};

		void AppendBindingSlot(RHIBindingLayoutDesc& desc, RHIBindingType type, uint32_t binding,
			uint32_t sizeInBytes, const char* debugName) noexcept
		{
			GGLAB_ASSERT(desc.m_SlotCount < desc.MaxSlots);
			desc.m_Slots[desc.m_SlotCount++] = {
				.m_Type = type,
				.m_Visibility = RHIShaderStage::Compute,
				.m_Binding = binding,
				.m_Space = 0,
				.m_Count = IsBindlessBindingType(type) ? 0u : 1u,
				.m_SizeInBytes = sizeInBytes,
				.m_DebugName = debugName,
			};
		}

		RHIBindingLayoutDesc BuildForwardPlusBindingLayout(bool diagnosticsEnabled) noexcept
		{
			RHIBindingLayoutDesc desc{};
			desc.m_DebugName = "ForwardPlusCull.BindingLayout";
			AppendBindingSlot(desc, RHIBindingType::PushConstants, 0,
				sizeof(ForwardPlusCullParameters), "ForwardPlusCullConstants");
			AppendBindingSlot(
				desc, RHIBindingType::ReadOnlyStorageBuffer, 0, 0, "ForwardPlusViews");
			AppendBindingSlot(
				desc, RHIBindingType::ReadOnlyStorageBuffer, 1, 0, "ForwardPlusLights");
			AppendBindingSlot(
				desc, RHIBindingType::ReadWriteStorageBuffer, 0, 0, "ForwardPlusTileHeaders");
			AppendBindingSlot(
				desc, RHIBindingType::ReadWriteStorageBuffer, 1, 0, "ForwardPlusTileIndices");
			if (diagnosticsEnabled)
			{
				AppendBindingSlot(desc, RHIBindingType::ReadWriteStorageBuffer, 2, 0,
					"ForwardPlusTileDepthRanges");
			}
			AppendBindingSlot(
				desc, RHIBindingType::BindlessResourceTable, 0, 0, "BindlessResources");
			return desc;
		}
	}

	void RenderPassForwardPlusCull::Prepare(const RenderServices& services) noexcept
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
		shaderDesc.m_SourcePath = L"Passes/PassForwardPlusCull.hlsl";
		shaderDesc.m_Stage = ShaderStage::Compute;
		shaderDesc.m_Entry = L"CSMain";
		m_PipelineRecipes[0].m_CSId = shaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Defines = {
			{
				.m_Name = L"GGLAB_FORWARD_PLUS_DIAGNOSTICS",
				.m_Value = L"1",
			},
		};
		m_PipelineRecipes[1].m_CSId = shaderManager->LoadShader(shaderDesc);

		auto* rhiContext = renderer->GetRHIContext();
		GGLAB_ASSERT_NOT_NULL(rhiContext);
		for (size_t variantIndex = 0; variantIndex < m_PipelineRecipes.size(); ++variantIndex)
		{
			auto& recipe = m_PipelineRecipes[variantIndex];
			recipe.m_BindingLayout = rhiContext->GetPipelineSystem().CreateBindingLayout(
				BuildForwardPlusBindingLayout(variantIndex != 0));
			if (!recipe.m_CSId.IsValid() || !recipe.m_BindingLayout.IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Forward+ failed to prepare compute shader variant {} or its binding layout.",
					variantIndex);
				GGLAB_UNREACHABLE("Forward+ compute shader variant is unavailable.");
			}
		}
		m_IsInitialized = true;
	}

	void RenderPassForwardPlusCull::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_IsInitialized, "Forward+ cull must be prepared before graph construction.");
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const RenderScene& renderScene = context.m_RenderScene;
		const uint32_t viewIndex =
			renderScene.m_ViewBaseIndex + static_cast<uint32_t>(utils::ToIndex(displayViewId));
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		if (m_DebugReadback)
		{
			auto* device = renderer->GetDevice();
			auto* rhiContext = renderer->GetRHIContext();
			auto* swapChain = renderer->GetSwapChain();
			GGLAB_ASSERT_NOT_NULL(device);
			GGLAB_ASSERT_NOT_NULL(rhiContext);
			GGLAB_ASSERT_NOT_NULL(swapChain);
			m_DebugReadback->Initialize(*device, rhiContext->GetFrameSlotCount());
			m_DebugReadback->ConsumeCompletedSlot(context.m_FrameSlotIndex);
			m_DebugReadback->PrepareGridBuffer(*device, context.m_FrameSlotIndex,
				MakeForwardPlusTileGrid(
					swapChain->GetBufferWidth(), swapChain->GetBufferHeight()));
		}

		rg.AddPass<PassData>(
			GetRenderGraphPassName(), RGPassEncoderType::Compute,
			[viewIndex, &renderScene, debugReadback = m_DebugReadback](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				const auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& framePlan =
					blackboard.Get<DepthCoverageFramePlan>(DepthCoverageFramePlanName);
				GGLAB_ASSERT_MSG(
					framePlan.UsesDepthPrepassEqual() && framePlan.m_HasDepthCoverageDraws,
					"Forward+ requires a complete validated depth prepass.");
				GGLAB_ASSERT_MSG(sceneDepth.m_Convention == DepthConvention::Reversed,
					"Forward+ only supports Reversed-Z display depth.");

				const RHITextureDesc& depthDesc = builder.GetTextureDesc(sceneDepth.m_Texture);
				data.m_TileGrid = MakeForwardPlusTileGrid(
					depthDesc.m_Extent.m_Width, depthDesc.m_Extent.m_Height);
				GGLAB_ASSERT_MSG(
					data.m_TileGrid.IsValid(), "Forward+ requires a non-empty display extent.");

				data.m_Depth = builder.Read(
					sceneDepth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_DepthSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					data.m_Depth, sceneDepth.m_SrvDesc);

				RHIBufferDesc headersDesc{};
				headersDesc.m_SizeInBytes = static_cast<uint64_t>(data.m_TileGrid.m_TileCount) *
					sizeof(ForwardPlusTileHeader);
				headersDesc.m_StrideInBytes = sizeof(ForwardPlusTileHeader);

				RHIBufferDesc indicesDesc{};
				indicesDesc.m_SizeInBytes = static_cast<uint64_t>(data.m_TileGrid.m_TileCount) *
					ForwardPlusTileLightCapacity * sizeof(uint32_t);
				indicesDesc.m_StrideInBytes = sizeof(uint32_t);

				auto& resources =
					blackboard.Get<RGForwardPlusResources>(ForwardPlusResourcesName);
				resources.m_DebugReadback = debugReadback;
				resources.m_TileGrid = data.m_TileGrid;
				resources.m_TileLightHeaders =
					builder.CreateBuffer("ForwardPlus.TileLightHeaders", headersDesc);
				resources.m_TileLightIndices =
					builder.CreateBuffer("ForwardPlus.TileLightIndices", indicesDesc);
				builder.WriteInPlace(resources.m_TileLightHeaders, RGBufferAccess::StorageWrite,
					RHIStage::ComputeShader);
				builder.WriteInPlace(resources.m_TileLightIndices, RGBufferAccess::StorageWrite,
					RHIStage::ComputeShader);
				data.m_DiagnosticsEnabled = debugReadback != nullptr;
				if (data.m_DiagnosticsEnabled)
				{
					RHIBufferDesc depthRangesDesc{};
					depthRangesDesc.m_SizeInBytes =
						static_cast<uint64_t>(data.m_TileGrid.m_TileCount) *
						sizeof(ForwardPlusTileDepthRange);
					depthRangesDesc.m_StrideInBytes = sizeof(ForwardPlusTileDepthRange);
					resources.m_TileDepthRanges = builder.CreateBuffer(
						"ForwardPlus.TileDepthRanges", depthRangesDesc);
					builder.WriteInPlace(resources.m_TileDepthRanges,
						RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_TileDepthRanges = resources.m_TileDepthRanges;
				}
				data.m_TileHeaders = resources.m_TileLightHeaders;
				data.m_TileIndices = resources.m_TileLightIndices;
				data.m_ViewIndex = viewIndex;
				data.m_LightBaseIndex = renderScene.m_LightBaseIndex;
				data.m_LightCount =
					std::min(renderScene.m_LightCount, ForwardPlusTileLightCapacity);
			},
			[this, renderer, &context](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetDirectComputeCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);

				const auto depthSrv = executeContext.GetViewDescriptor(data.m_DepthSrv);
				const auto headers = executeContext.GetBufferHandle(data.m_TileHeaders);
				const auto indices = executeContext.GetBufferHandle(data.m_TileIndices);
				GGLAB_ASSERT_MSG(depthSrv.IsValid() && headers.IsValid() && indices.IsValid(),
					"Forward+ graph resources must resolve before dispatch.");

				commandContext->SetPipeline(
					GetOrCreatePipeline(*renderer, data.m_DiagnosticsEnabled));
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(ForwardPlusRootParameter::ViewBuffer),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(ForwardPlusRootParameter::LightBuffer),
					renderer->GetLightStructuredBuffer()->GetBufferHandle(
						context.m_FrameSlotIndex));
				commandContext->SetReadWriteBuffer(
					static_cast<uint32_t>(ForwardPlusRootParameter::TileHeaders), headers);
				commandContext->SetReadWriteBuffer(
					static_cast<uint32_t>(ForwardPlusRootParameter::TileIndices), indices);
				if (data.m_DiagnosticsEnabled)
				{
					const RHIBufferHandle depthRanges =
						executeContext.GetBufferHandle(data.m_TileDepthRanges);
					GGLAB_ASSERT_MSG(depthRanges.IsValid(),
						"Forward+ diagnostics depth ranges must resolve before dispatch.");
					commandContext->SetReadWriteBuffer(
						static_cast<uint32_t>(ForwardPlusRootParameter::TileDepthRanges), depthRanges);
				}
				commandContext->SetPushConstants(
					static_cast<uint32_t>(ForwardPlusRootParameter::PassConstants),
					ForwardPlusCullParameters{
						.m_DepthTextureIndex = depthSrv.m_Index,
						.m_ViewIndex = data.m_ViewIndex,
						.m_LightBaseIndex = data.m_LightBaseIndex,
						.m_LightCount = data.m_LightCount,
						.m_Width = data.m_TileGrid.m_Width,
						.m_Height = data.m_TileGrid.m_Height,
						.m_TileCountX = data.m_TileGrid.m_TileCountX,
						.m_TileCountY = data.m_TileGrid.m_TileCountY,
					});
				commandContext->Dispatch(
					data.m_TileGrid.m_TileCountX, data.m_TileGrid.m_TileCountY, 1);
			});

		if (!m_DebugReadback)
		{
			return;
		}

		const auto debugReadback = m_DebugReadback;
		const uint32_t bufferIndex = context.m_FrameSlotIndex;
		const uint64_t frameSerial = context.m_FrameSerial;
		rg.AddPass<ReadbackPassData>(
			"Lighting.ForwardPlus.Readback", RGPassEncoderType::Copy,
			[debugReadback, bufferIndex, frameSerial](
				RenderGraph::RGBuilder& builder, ReadbackPassData& data)
			{
				builder.SideEffect();
				auto& resources =
					builder.GetBlackboard().Get<RGForwardPlusResources>(ForwardPlusResourcesName);
				GGLAB_ASSERT_MSG(resources.IsValid(), "Forward+ readback requires cull outputs.");
				GGLAB_ASSERT_MSG(resources.HasGridDiagnostics(),
					"Forward+ grid readback requires diagnostic depth ranges.");
				data.m_TileGrid = resources.m_TileGrid;
				data.m_TileX =
					std::min(debugReadback->GetSelectedTileX(), data.m_TileGrid.m_TileCountX - 1);
				data.m_TileY =
					std::min(debugReadback->GetSelectedTileY(), data.m_TileGrid.m_TileCountY - 1);
				const uint32_t tileIndex =
					data.m_TileY * data.m_TileGrid.m_TileCountX + data.m_TileX;
				data.m_HeaderSourceOffset =
					static_cast<uint64_t>(tileIndex) * sizeof(ForwardPlusTileHeader);
				data.m_IndicesSourceOffset =
					static_cast<uint64_t>(GetForwardPlusTileOffset(tileIndex)) * sizeof(uint32_t);
				data.m_TileHeaders = builder.Read(
					resources.m_TileLightHeaders, RGBufferAccess::CopySource, RHIStage::Copy);
				data.m_TileIndices = builder.Read(
					resources.m_TileLightIndices, RGBufferAccess::CopySource, RHIStage::Copy);
				data.m_TileDepthRanges = builder.Read(
					resources.m_TileDepthRanges, RGBufferAccess::CopySource, RHIStage::Copy);

				RHIBufferDesc readbackDesc{};
				readbackDesc.m_SizeInBytes = ForwardPlusDebugReadback::ReadbackSizeInBytes;
				readbackDesc.m_Usage = RHIBufferUsage::CopyDest;
				readbackDesc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
				readbackDesc.m_DebugName = "ForwardPlus.DebugReadback";
				data.m_Readback = builder.ImportBuffer("ForwardPlus.DebugReadback",
					debugReadback->GetBuffer(bufferIndex), readbackDesc, RGBufferAccess::CopyDest);
				builder.WriteInPlace(data.m_Readback, RGBufferAccess::CopyDest, RHIStage::Copy);

				RHIBufferDesc gridReadbackDesc{};
				gridReadbackDesc.m_SizeInBytes =
					ForwardPlusDebugReadback::GetGridReadbackSizeInBytes(
						data.m_TileGrid.m_TileCount);
				gridReadbackDesc.m_Usage = RHIBufferUsage::CopyDest;
				gridReadbackDesc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
				gridReadbackDesc.m_DebugName = "ForwardPlus.GridReadback";
				data.m_GridReadback = builder.ImportBuffer("ForwardPlus.GridReadback",
					debugReadback->GetGridBuffer(bufferIndex), gridReadbackDesc,
					RGBufferAccess::CopyDest);
				builder.WriteInPlace(
					data.m_GridReadback, RGBufferAccess::CopyDest, RHIStage::Copy);
				data.m_BufferIndex = bufferIndex;
				data.m_FrameSerial = frameSerial;
			},
			[debugReadback](RGExecuteContext& executeContext, ReadbackPassData& data)
			{
				auto* commandContext = executeContext.GetCopyCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const RHIBufferHandle headers = executeContext.GetBufferHandle(data.m_TileHeaders);
				const RHIBufferHandle indices = executeContext.GetBufferHandle(data.m_TileIndices);
				const RHIBufferHandle readback = executeContext.GetBufferHandle(data.m_Readback);
				const RHIBufferHandle depthRanges =
					executeContext.GetBufferHandle(data.m_TileDepthRanges);
				const RHIBufferHandle gridReadback =
					executeContext.GetBufferHandle(data.m_GridReadback);
				GGLAB_ASSERT_MSG(depthRanges.IsValid() && gridReadback.IsValid(),
					"Forward+ full-grid readback resources must resolve before copy.");
				commandContext->CopyBuffer(readback, ForwardPlusDebugReadback::HeaderReadbackOffset,
					headers, data.m_HeaderSourceOffset, sizeof(ForwardPlusTileHeader));
				commandContext->CopyBuffer(readback,
					ForwardPlusDebugReadback::IndicesReadbackOffset, indices,
					data.m_IndicesSourceOffset, sizeof(uint32_t) * ForwardPlusTileLightCapacity);
				commandContext->CopyBuffer(gridReadback, 0, headers, 0,
					ForwardPlusDebugReadback::GetGridHeadersSizeInBytes(
						data.m_TileGrid.m_TileCount));
				commandContext->CopyBuffer(gridReadback,
					ForwardPlusDebugReadback::GetGridDepthRangesOffset(
						data.m_TileGrid.m_TileCount),
					depthRanges, 0, static_cast<uint64_t>(data.m_TileGrid.m_TileCount) *
					sizeof(ForwardPlusTileDepthRange));
				debugReadback->MarkScheduled(data.m_BufferIndex, data.m_FrameSerial,
					data.m_TileGrid, data.m_TileX, data.m_TileY);
				debugReadback->MarkGridScheduled(
					data.m_BufferIndex, data.m_FrameSerial, data.m_TileGrid);
			});
	}

	RHIPipelineHandle RenderPassForwardPlusCull::GetOrCreatePipeline(
		const Renderer& renderer, bool diagnosticsEnabled) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		const size_t variantIndex = diagnosticsEnabled ? 1u : 0u;
		const RHIPipelineHandle pipeline =
			pipelineCache->Resolve(
				m_PipelineSlots[variantIndex], m_PipelineRecipes[variantIndex], GetInfo());
		if (!pipeline.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Forward+ failed to create its required compute pipeline.");
			GGLAB_UNREACHABLE("Forward+ compute pipeline is unavailable.");
		}
		return pipeline;
	}
}
