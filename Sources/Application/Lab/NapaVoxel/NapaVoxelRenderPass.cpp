#include "Application/Lab/NapaVoxel/NapaVoxelRenderPass.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderExtension.h"

#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct NapaVoxelPassParameters
		{
			uint32_t m_ViewIndex = 0;
			uint32_t m_Material = 0;
			std::array<uint32_t, 2> m_Padding0{};
			Vector3 m_ChunkTranslation{};
			float m_Padding1 = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<NapaVoxelPassParameters>);
		static_assert(sizeof(NapaVoxelPassParameters) == 32);

		struct NapaVoxelPassChunk
		{
			RGBufferId m_VertexBuffer{};
			RGBufferId m_IndexBuffer{};
			RHIBufferDesc m_VertexBufferDesc{};
			RHIBufferDesc m_IndexBufferDesc{};
			Vector3 m_Translation{};
			std::vector<NapaVoxelGpuSectionDraw> m_Sections;
		};

		struct PassData
		{
			RGTextureId m_SceneColor{};
			RGTextureId m_Depth{};
			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
			DepthCoverageRasterDomain m_RasterDomain{};
			std::vector<NapaVoxelPassChunk> m_Chunks;
		};
	}

	NapaVoxelRenderPass::NapaVoxelRenderPass() noexcept :
		RenderPassBase({
			.m_TypeName = "Geometry.NapaVoxel",
			.m_DisplayName = "Napa Voxel",
			.m_CategoryName = "Geometry",
			.m_Description = "Draws published Napa voxel chunks into HDR scene color and main depth.",
			.m_Category = RenderPassCategory::Geometry,
			.m_Type = RenderPassType::Graphics,
			})
	{
	}

	void NapaVoxelRenderPass::AddPass(RenderGraph& renderGraph,
		const RenderFrameContext& frameContext, const RenderServices& services)
	{
		const std::shared_ptr<const NapaVoxelGpuMeshSet> frameView = m_FrameView;
		const NapaVoxelSurfaceMode surfaceMode = m_SurfaceMode;
		if (!frameView || frameView->GetVisibleWorldRevision() == 0 ||
			frameView->GetChunks().empty() || !frameContext.IsRenderSceneReady())
		{
			return;
		}

		EnsureInitialized(services);
		if (!m_IsInitialized)
		{
			return;
		}

		const RenderViewID displayViewId = frameContext.GetDisplayViewId();
		const auto* frameContextPtr = &frameContext;
		const auto* servicesPtr = &services;
		renderGraph.AddPass<PassData>(GetRenderGraphPassName(),
			[frameView, frameContextPtr, displayViewId](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& blackboard = builder.GetBlackboard();
				auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				auto& sceneDepth =
					blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& rasterDomain =
					frameContextPtr->GetRenderQueue(displayViewId).m_CoverageRasterDomain;
				GGLAB_ASSERT_MSG(rasterDomain.IsValid() &&
					rasterDomain.m_DepthConvention == DepthConvention::Reversed &&
					sceneDepth.m_Convention == DepthConvention::Reversed,
					"Napa voxel rendering requires the display Reversed-Z raster domain.");
				GGLAB_ASSERT_MSG(AreDepthCoverageTargetExtentsCompatible(rasterDomain,
					builder.GetTextureDesc(targets.m_SceneColor),
					builder.GetTextureDesc(sceneDepth.m_Texture)),
					"Napa voxel color and depth targets must match the display raster domain.");

				builder.ReadWriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				builder.ReadWriteInPlace(
					sceneDepth.m_Texture, RGTextureAccess::DepthStencilWrite);
				data.m_SceneColor = targets.m_SceneColor;
				data.m_Depth = sceneDepth.m_Texture;
				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
					data.m_Depth, sceneDepth.m_DsvDesc);
				data.m_RasterDomain = rasterDomain;

				data.m_Chunks.reserve(frameView->GetChunks().size());
				for (size_t chunkIndex = 0; chunkIndex < frameView->GetChunks().size(); ++chunkIndex)
				{
					const auto& sourceOwner = frameView->GetChunks()[chunkIndex];
					if (!sourceOwner)
					{
						continue;
					}
					const NapaVoxelGpuChunkMesh& source = *sourceOwner;
					const std::string vertexName =
						std::format("NapaVoxel.Vertex.{}", chunkIndex);
					const std::string indexName =
						std::format("NapaVoxel.Index.{}", chunkIndex);
					NapaVoxelPassChunk chunk{};
					chunk.m_VertexBufferDesc = source.m_VertexBufferDesc;
					chunk.m_IndexBufferDesc = source.m_IndexBufferDesc;
					chunk.m_Translation = source.m_Translation;
					chunk.m_Sections = source.m_Sections;
					chunk.m_VertexBuffer = builder.ImportBuffer(vertexName.c_str(),
						source.m_VertexBuffer.Get(), source.m_VertexBufferDesc,
						RGBufferAccess::None, RGContentValidity::Defined);
					chunk.m_IndexBuffer = builder.ImportBuffer(indexName.c_str(),
						source.m_IndexBuffer.Get(), source.m_IndexBufferDesc,
						RGBufferAccess::None, RGContentValidity::Defined);
					chunk.m_VertexBuffer = builder.Read(
						chunk.m_VertexBuffer, RGBufferAccess::Vertex, RHIStage::VertexShader);
					chunk.m_IndexBuffer = builder.Read(
						chunk.m_IndexBuffer, RGBufferAccess::Index, RHIStage::IndexInput);
					builder.Export(chunk.m_VertexBuffer, RGBufferAccess::None);
					builder.Export(chunk.m_IndexBuffer, RGBufferAccess::None);
					data.m_Chunks.push_back(std::move(chunk));
				}
			},
			[this, frameContextPtr, servicesPtr, displayViewId, surfaceMode](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(graphicsContext);
				GGLAB_ASSERT_NOT_NULL(renderer);

				const RHITextureViewHandle rtv = executeContext.GetViewHandle(data.m_Rtv);
				const RHITextureViewHandle dsv = executeContext.GetViewHandle(data.m_Dsv);
				const RHIRenderingAttachment colorAttachment{ .m_View = rtv };
				graphicsContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1),
					.m_DepthAttachment = RHIRenderingAttachment{ .m_View = dsv },
				});
				const size_t pipelineIndex = static_cast<size_t>(surfaceMode);
				graphicsContext->SetPipeline(renderer->GetPipelineCache()->Resolve(
					m_PipelineSlots[pipelineIndex], m_PipelineKeys[pipelineIndex], GetInfo()));
				graphicsContext->SetViewport(data.m_RasterDomain.m_Viewport);
				graphicsContext->SetScissorRect(data.m_RasterDomain.m_Scissor);
				graphicsContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				graphicsContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					renderer->GetSceneConstantBuffer()->GetBufferHandle(),
					frameContextPtr->m_RenderScene.m_SceneConstantBufferOffset);
				graphicsContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());

				for (const NapaVoxelPassChunk& chunk : data.m_Chunks)
				{
					const RHIVertexBufferBinding vertexBinding{
						.m_Buffer = executeContext.GetBufferHandle(chunk.m_VertexBuffer),
						.m_Offset = 0,
						.m_Stride = chunk.m_VertexBufferDesc.m_StrideInBytes,
						.m_SizeInBytes =
							static_cast<uint32_t>(chunk.m_VertexBufferDesc.m_SizeInBytes),
					};
					const RHIIndexBufferBinding indexBinding{
						.m_Buffer = executeContext.GetBufferHandle(chunk.m_IndexBuffer),
						.m_Offset = 0,
						.m_SizeInBytes =
							static_cast<uint32_t>(chunk.m_IndexBufferDesc.m_SizeInBytes),
						.m_Format = RHIFormat::R32Uint,
					};
					graphicsContext->SetVertexBuffers(
						0, std::span<const RHIVertexBufferBinding>(&vertexBinding, 1));
					graphicsContext->SetIndexBuffer(indexBinding);

					for (const NapaVoxelGpuSectionDraw& section : chunk.m_Sections)
					{
						const NapaVoxelPassParameters parameters{
							.m_ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
							.m_Material = static_cast<uint32_t>(section.m_Material),
							.m_ChunkTranslation = chunk.m_Translation,
						};
						graphicsContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
						graphicsContext->DrawIndexed(
							section.m_IndexCount, 1, section.m_FirstIndex);
					}
				}
			});
	}

	void NapaVoxelRenderPass::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}
		GGLAB_ASSERT_NOT_NULL(services.m_Renderer);
		GGLAB_ASSERT_NOT_NULL(services.m_ShaderManager);

		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Passes/PassNapaVoxel.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		shaderDesc.m_Entry = L"VSMain";
		const ShaderID vertexShader = services.m_ShaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Stage = ShaderStage::Pixel;
		shaderDesc.m_Entry = L"PSMain";
		const ShaderID pixelShader = services.m_ShaderManager->LoadShader(shaderDesc);
		if (!vertexShader.IsValid() || !pixelShader.IsValid())
		{
			return;
		}

		for (size_t index = 0; index < m_PipelineKeys.size(); ++index)
		{
			auto& key = m_PipelineKeys[index];
			key.m_VSId = vertexShader;
			key.m_PSId = pixelShader;
			key.m_BindingLayout = services.m_Renderer->GetCommonBindingLayout();
			key.m_InputLayoutId = InputLayoutID::P3N3;
			key.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			key.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			key.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
			key.m_Formats.m_RenderTargetCount = 1;
			key.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			key.m_RasterizerPreset = index == static_cast<size_t>(NapaVoxelSurfaceMode::Wireframe)
				? RasterizerPreset::Wireframe
				: RasterizerPreset::Default;
			key.m_DepthPreset = DepthPreset::ReversedZWrite;
			key.m_BlendPreset = BlendPreset::Default;
		}
		m_IsInitialized = true;
	}
}
