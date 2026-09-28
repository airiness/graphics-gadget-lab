#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gglab
{
	namespace
	{
		enum class EnvironmentSourceMode : uint32_t
		{
			Equirectangular,
			Cubemap,
			PhysicalSky,
		};

		struct IBLEnvironmentPassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t SourceTextureIndex = 0;
			uint32_t SourceSamplerIndex = 0;
			uint32_t SourceMode = 0;
			Vector3 SunDirection = Vector3::UnitY;
			float Padding = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<IBLEnvironmentPassParameters>);
		static_assert(sizeof(IBLEnvironmentPassParameters) == 32);
		static_assert(offsetof(IBLEnvironmentPassParameters, SunDirection) == 16);

		struct PassData
		{
			RGTextureId m_SourceTexture{};
			RGTextureViewId m_SourceSrv{};
			RGTextureId m_EnvironmentCubemap{};
			std::array<RGTextureViewId, CubemapFaceCount> m_Rtvs{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SourceTextureIndex = 0;
			uint32_t m_SourceSamplerIndex = 0;
			uint32_t m_SourceMode = 0;
			RHIFormat m_RenderTargetFormat = RHIFormat::Unknown;
		};
	}

	void RenderPassIBLEnvironment::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		auto* assetManager = services.m_TextureAssets;

		auto* renderResRegistry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);
		auto* bakeScheduler = services.m_Environment;
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);

		RHITextureHandle sourceTextureHandle{};
		RHITextureDesc sourceTextureDesc{};
		uint32_t sourceTextureIndex = 0;
		uint32_t sourceSamplerIndex = 0;
		bool hasSource = false;
		EnvironmentSourceMode sourceMode = EnvironmentSourceMode::Equirectangular;
		const bool physical = bakeScheduler->GetBakingAtmosphereParameters() != nullptr;
		const EnvironmentTextureSource source = bakeScheduler->GetBakingSource();
		if (physical)
		{
			if (!rg.GetBlackboard().TryGet<RGAtmosphereResources>(BakeAtmosphereResourcesName)) return;
			sourceMode = EnvironmentSourceMode::PhysicalSky;
			sourceSamplerIndex = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp);
			hasSource = true;
		}
		else if (source.IsValid())
		{
			GGLAB_ASSERT_NOT_NULL(assetManager);
			const auto sourceResource = assetManager->GetResidentTextureResource(source.m_Content);
			if (sourceResource)
			{
				assetManager->MarkTextureUsed(source.m_Content.m_Id);
				sourceTextureHandle = sourceResource->m_Texture;
				sourceTextureDesc = sourceResource->m_Desc;
				sourceTextureIndex = sourceResource->m_SrvIndex;
				sourceMode = source.m_Type == EnvironmentTextureSourceType::Cubemap
					? EnvironmentSourceMode::Cubemap
					: EnvironmentSourceMode::Equirectangular;
				sourceSamplerIndex = services.m_Samplers->GetSamplerIndex(
					sourceMode == EnvironmentSourceMode::Cubemap
					? SamplerPreset::LinearClamp
					: SamplerPreset::LinearWrapUClampV);
				hasSource = true;
			}
		}
		GGLAB_ASSERT_MSG(hasSource, "IBL environment bake requires a ready source texture.");

		const uint64_t bakeGeneration = bakeScheduler->GetBakingGeneration();

		EnsureInitialized(services);

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[renderResRegistry, sourceTextureHandle, sourceTextureDesc, sourceTextureIndex,
			sourceSamplerIndex, sourceMode,
			hasSource](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();
				if (sourceMode == EnvironmentSourceMode::PhysicalSky)
				{
					const auto& atmosphere = builder.GetBlackboard().Get<RGAtmosphereResources>(BakeAtmosphereResourcesName);
					data.m_SourceTexture = builder.Read(atmosphere.m_Luts[2], RGTextureAccess::Sample, RHIStage::PixelShader);
					data.m_SourceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(data.m_SourceTexture);
				}
				else if (hasSource)
				{
					data.m_SourceTexture = builder.ImportTexture("IBL.SourceEnvironment",
						sourceTextureHandle, sourceTextureDesc, RGTextureAccess::Sample,
						RGContentValidity::Defined);
					data.m_SourceTexture =
						builder.Read(data.m_SourceTexture, RGTextureAccess::Sample);
				}

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				const auto* textureDesc = renderResRegistry->GetIBLBakeTextureDesc(
					RenderTextureIndex::IBL_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(textureDesc);

				const RHISubresourceRange mipZeroRange{
					.m_BaseMip = 0,
					.m_MipCount = 1,
					.m_BaseArraySlice = 0,
					.m_ArraySliceCount = CubemapFaceCount,
					.m_Aspects = RHITextureAspect::Color,
				};
				builder.WriteInPlace(
					iblRes.m_BakeEnvironmentCubemap, RGTextureAccess::RenderTarget, mipZeroRange);
				data.m_EnvironmentCubemap = iblRes.m_BakeEnvironmentCubemap;

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					const auto rtvDesc =
						MakeRHITexture2DArrayViewDesc(textureDesc->m_Format, 0, face, 1);
					data.m_Rtvs[face] = builder.CreateView<RHITextureViewType::RenderTarget>(
						data.m_EnvironmentCubemap, rtvDesc);
				}

				data.m_Width = textureDesc->m_Extent.m_Width;
				data.m_Height = textureDesc->m_Extent.m_Height;
				data.m_SourceTextureIndex = sourceTextureIndex;
				data.m_SourceSamplerIndex = sourceSamplerIndex;
				data.m_SourceMode = static_cast<uint32_t>(sourceMode);
				data.m_RenderTargetFormat = textureDesc->m_Format;
			},
			[this, services, bakeScheduler, bakeGeneration](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				commandContext->SetPipeline(GetOrCreatePSO(services, data.m_RenderTargetFormat));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					const auto rtv = executeContext.GetViewHandle(data.m_Rtvs[face]);
					const RHIRenderingAttachment colorAttachment{
						.m_View = rtv,
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
					commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });

					const IBLEnvironmentPassParameters passParameters{
						.CubemapFaceIndex = face,
						.SourceTextureIndex = data.m_SourceSrv.IsValid()
							? executeContext.GetViewDescriptor(data.m_SourceSrv).m_Index : data.m_SourceTextureIndex,
						.SourceSamplerIndex = data.m_SourceSamplerIndex,
						.SourceMode = data.m_SourceMode,
						.SunDirection = bakeScheduler->GetBakingSunDirection(),
					};
					commandContext->SetPushConstants(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
						passParameters);

					commandContext->DrawFullscreenTriangle();
					commandContext->EndRendering();
				}

				bakeScheduler->NotifyStageExecuted(IBLBakeStage::Environment, bakeGeneration);
			});
	}
	void RenderPassIBLEnvironment::EnsureInitialized(const RenderServices& services) noexcept
	{

		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			// Shader
			const auto vsId = shaderManager->LoadProgram(shader_programs::IBLEnvironmentVertex);
			const auto psId = shaderManager->LoadProgram(shader_programs::IBLEnvironmentPixel);

			// Pipeline recipe
			m_BaseRecipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
			m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
			m_BaseRecipe.m_VSId = vsId;
			m_BaseRecipe.m_PSId = psId;

			m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
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

	RHIPipelineHandle RenderPassIBLEnvironment::GetOrCreatePSO(
		const RenderServices& services, RHIFormat renderTargetFormat) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GraphicsPhysicalPipelineKey recipe = m_BaseRecipe;
		recipe.m_Formats.m_RenderTargetFormats[0] = renderTargetFormat;
		return pipelineCache->Resolve(m_PipelineSlot, recipe, GetInfo());
	}
}
