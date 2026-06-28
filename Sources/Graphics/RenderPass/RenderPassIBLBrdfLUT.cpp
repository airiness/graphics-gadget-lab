#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLBrdfLUT.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct PassData
		{
			RGTextureId m_BrdfLut{};
			RGTextureViewId m_Rtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};
	}

	void RenderPassIBLBrdfLUT::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		const auto shouldBuild = renderResRegistry->IsDirty(RenderResourceRegistry::TextureIndex::IBL_BrdfLut);

		// Nothing to build this frame. The texture is still imported into RG by RenderPassIBL.SetupResources
		if (!shouldBuild)
		{
			return;
		}

		EnsureInitialized(services);

		rg.AddPass<PassData>("RenderPassIBL.BuildBrdfLUT",
			[renderResRegistry](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				const auto* textureDesc = renderResRegistry->GetTextureDesc(
					RenderResourceRegistry::TextureIndex::IBL_BrdfLut);
				GGLAB_ASSERT_NOT_NULL(textureDesc);

				data.m_BrdfLut = builder.Write(iblRes.m_BrdfLut, RGTextureAccess::RenderTarget);
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BrdfLut);

				data.m_Width = textureDesc->m_Extent.m_Width;
				data.m_Height = textureDesc->m_Extent.m_Height;
			},
			[this, renderer, renderResRegistry](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const RHITextureViewHandle rtv = executeContext.GetViewHandle(data.m_Rtv);
				commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });
				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				commandContext->Draw(3);

				renderResRegistry->ClearDirty(RenderResourceRegistry::TextureIndex::IBL_BrdfLut);
			});
	}

	void RenderPassIBLBrdfLUT::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			// Shader
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassIBLBrdfLUT.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			// Pipeline recipe
			m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16Float;
			m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
			m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
			m_BaseRecipe.m_Formats.m_SampleCount = 1;
			m_BaseRecipe.m_Formats.m_SampleQuality = 0;

			m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
			m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;

			m_IsInitialized = true;
		}

	}

	RHIPipelineHandle RenderPassIBLBrdfLUT::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe);
	}
}
