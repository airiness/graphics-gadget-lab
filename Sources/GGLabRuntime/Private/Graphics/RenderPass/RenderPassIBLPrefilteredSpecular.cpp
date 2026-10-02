#include "Graphics/RenderPass/RenderPassIBLPrefilteredSpecular.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/IBLBakeScheduler.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/SamplerRegistry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr uint32_t SpecularImportanceMaxResolution = 64u;

		struct IBLPrefilteredSpecularPassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t MipLevel = 0;
			uint32_t MipLevels = 0;
			uint32_t EnvironmentTextureIndex = 0;
			uint32_t EnvironmentSamplerIndex = 0;
			uint32_t ImportanceTextureIndex = 0;
			uint32_t ImportanceResolution = 0;
			uint32_t SampleCount = 0;
			float MaxSampleLuminance = 0.0f;
			uint32_t PhysicalSky = 0;
			uint32_t Padding[2]{};
		};
		static_assert(IsPassRootConstantStruct<IBLPrefilteredSpecularPassParameters>);
		static_assert(sizeof(IBLPrefilteredSpecularPassParameters) == 48);
		static_assert(offsetof(IBLPrefilteredSpecularPassParameters, PhysicalSky) == 36);
		static_assert(offsetof(IBLPrefilteredSpecularPassParameters, ImportanceTextureIndex) == 20);
		static_assert(offsetof(IBLPrefilteredSpecularPassParameters, ImportanceResolution) == 24);

		struct IBLImportancePassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t MipLevel = 0;
			uint32_t SourceTextureIndex = 0;
			uint32_t EnvironmentSamplerIndex = 0;
			uint32_t ImportanceResolution = 0;
			float EnvironmentSourceMip = 0.0f;
			uint32_t PhysicalSky = 0;
			uint32_t Padding = 0;
		};
		static_assert(IsPassRootConstantStruct<IBLImportancePassParameters>);
		static_assert(sizeof(IBLImportancePassParameters) == 32);
		static_assert(offsetof(IBLImportancePassParameters, EnvironmentSourceMip) == 20);
		static_assert(offsetof(IBLImportancePassParameters, PhysicalSky) == 24);

		struct ImportancePassData
		{
			RGTextureViewId m_SourceSrv{};
			std::array<RGTextureViewId, CubemapFaceCount> m_Rtvs{};
		};

		struct PassData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_PrefilteredSpecularCubemap{};
			RGTextureViewId m_ImportanceSrv{};
			uint32_t m_ImportanceResolution = 0;
			std::vector<RGTextureViewId> m_Rtvs;

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_MipLevels = 0;
			uint32_t m_EnvironmentTextureIndex = 0;
			uint32_t m_EnvironmentSamplerIndex = 0;
			uint32_t m_SampleCount = 0;
			float m_MaxSampleLuminance = 0.0f;
			RHIFormat m_RenderTargetFormat = RHIFormat::Unknown;
		};
	}

	void RenderPassIBLPrefilteredSpecular::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);


		auto* renderResRegistry = services.m_Resources;
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);
		auto* bakeScheduler = services.m_Environment;
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);
		const uint64_t bakeGeneration = bakeScheduler->GetBakingGeneration();

		EnsureInitialized(services);
		const auto importanceTexture = AddImportancePasses(rg, services,
			bakeScheduler->GetBakingAtmosphereParameters() ? 1u : 0u);

		const auto& config = bakeScheduler->GetBakingConfig();
		const uint32_t sampleCount = config.m_PrefilteredSpecularSampleCount;
		const float maxSampleLuminance = config.m_PrefilteredSpecularMaxSampleLuminance;
		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[services, renderResRegistry, sampleCount, maxSampleLuminance, importanceTexture](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				data.m_EnvironmentCubemap =
					builder.Read(iblRes.m_BakeEnvironmentCubemap, RGTextureAccess::Sample);
				const auto importance = builder.Read(importanceTexture, RGTextureAccess::Sample);
				const auto& importanceDesc = builder.GetTextureDesc(importance);
				auto importanceSrvDesc = MakeRHITexture2DArrayViewDesc(
					importanceDesc.m_Format, 0u, 0u, CubemapFaceCount);
				importanceSrvDesc.m_Subresources.m_MipCount = importanceDesc.m_MipLevels;
				data.m_ImportanceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
					importance, importanceSrvDesc);
				data.m_ImportanceResolution = importanceDesc.m_Extent.m_Width;
				builder.WriteInPlace(
					iblRes.m_BakePrefilteredSpecularCubemap, RGTextureAccess::RenderTarget);
				data.m_PrefilteredSpecularCubemap = iblRes.m_BakePrefilteredSpecularCubemap;

				const auto* textureDesc = renderResRegistry->GetIBLBakeTextureDesc(
					RenderTextureIndex::IBL_PrefilteredSpecularCubemap);
				GGLAB_ASSERT_NOT_NULL(textureDesc);

				data.m_Rtvs.resize(textureDesc->m_MipLevels * CubemapFaceCount);
				for (uint32_t mip = 0; mip < textureDesc->m_MipLevels; ++mip)
				{
					for (uint32_t face = 0; face < CubemapFaceCount; ++face)
					{
						const auto rtvDesc =
							MakeRHITexture2DArrayViewDesc(textureDesc->m_Format, mip, face, 1);
						const auto rtvIndex = mip * CubemapFaceCount + face;

						data.m_Rtvs[rtvIndex] =
							builder.CreateView<RHITextureViewType::RenderTarget>(
								data.m_PrefilteredSpecularCubemap, rtvDesc);
					}
				}

				data.m_Width = textureDesc->m_Extent.m_Width;
				data.m_Height = textureDesc->m_Extent.m_Height;
				data.m_MipLevels = textureDesc->m_MipLevels;
				data.m_EnvironmentTextureIndex = renderResRegistry->GetIBLBakeShaderVisibleSrvIndex(
					RenderTextureIndex::IBL_EnvironmentCubemap);
				data.m_EnvironmentSamplerIndex =
					services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp);

				data.m_SampleCount = sampleCount;
				data.m_MaxSampleLuminance = maxSampleLuminance;
				data.m_RenderTargetFormat = textureDesc->m_Format;
			},
			[this, services, bakeScheduler, bakeGeneration](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto importanceSrv = executeContext.GetViewDescriptor(data.m_ImportanceSrv);
				GGLAB_ASSERT_MSG(importanceSrv.IsValid(), "IBL importance SRV must be shader visible.");
				commandContext->SetPipeline(GetOrCreatePSO(services, data.m_RenderTargetFormat));

				for (uint32_t mip = 0; mip < data.m_MipLevels; ++mip)
				{
					const uint32_t mipWidth = std::max(1u, data.m_Width >> mip);
					const uint32_t mipHeight = std::max(1u, data.m_Height >> mip);

					commandContext->SetViewport(
						{ 0.0f, 0.0f, static_cast<float>(mipWidth), static_cast<float>(mipHeight) });
					commandContext->SetScissorRect(
						{ 0, 0, static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) });

					for (uint32_t face = 0; face < CubemapFaceCount; ++face)
					{
						const auto rtvIndex = mip * CubemapFaceCount + face;
						const auto rtv = executeContext.GetViewHandle(data.m_Rtvs[rtvIndex]);
						const RHIRenderingAttachment colorAttachment{
							.m_View = rtv,
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
						commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });

						const IBLPrefilteredSpecularPassParameters passParameters{
							.CubemapFaceIndex = face,
							.MipLevel = mip,
							.MipLevels = data.m_MipLevels,
							.EnvironmentTextureIndex = data.m_EnvironmentTextureIndex,
							.EnvironmentSamplerIndex = data.m_EnvironmentSamplerIndex,
							.ImportanceTextureIndex = importanceSrv.m_Index,
							.ImportanceResolution = data.m_ImportanceResolution,
							.SampleCount = data.m_SampleCount,
							.MaxSampleLuminance = data.m_MaxSampleLuminance,
							.PhysicalSky = bakeScheduler->GetBakingAtmosphereParameters() ? 1u : 0u,
						};
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							passParameters);

						commandContext->DrawFullscreenTriangle();
						commandContext->EndRendering();
					}
				}

				bakeScheduler->NotifyStageExecuted(
					IBLBakeStage::PrefilteredSpecular, bakeGeneration);
			});
	}

	RGTextureId RenderPassIBLPrefilteredSpecular::AddImportancePasses(RenderGraph& rg,
		const RenderServices& services, uint32_t physicalSky) noexcept
	{
		auto* registry = services.m_Resources;
		const auto* environmentDesc = registry->GetIBLBakeTextureDesc(RenderTextureIndex::IBL_EnvironmentCubemap);
		GGLAB_ASSERT_NOT_NULL(environmentDesc);
		// A power-of-two proposal grid also supports custom non-power-of-two environments.
		const uint32_t resolution = std::bit_floor(std::min(environmentDesc->m_Extent.m_Width,
			SpecularImportanceMaxResolution));
		const uint32_t mipLevels = std::bit_width(resolution);
		const float sourceMip = std::min(std::log2(static_cast<float>(environmentDesc->m_Extent.m_Width) /
			static_cast<float>(resolution)), static_cast<float>(environmentDesc->m_MipLevels - 1u));
		const uint32_t samplerIndex = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp);
		RGTextureId importanceTexture{};
		for (uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			const std::string name = MakeRenderGraphPassName("Importance." + std::to_string(mip));
			rg.AddPass<ImportancePassData>(name.c_str(),
				[&importanceTexture, mip, resolution, mipLevels](RenderGraph::RGBuilder& builder, ImportancePassData& data)
				{
					if (mip == 0u)
					{
						RHITextureDesc desc{};
						desc.m_Format = RHIFormat::R32Float;
						desc.m_Extent = { resolution, resolution, 1u };
						desc.m_ArraySize = CubemapFaceCount;
						desc.m_MipLevels = static_cast<uint16_t>(mipLevels);
						importanceTexture = builder.CreateTexture("IBL.SpecularImportance", desc);
						const auto& ibl = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
						builder.Read(ibl.m_BakeEnvironmentCubemap, RGTextureAccess::Sample);
					}
					else
					{
						const auto sourceDesc = MakeRHITexture2DArrayViewDesc(RHIFormat::R32Float,
							mip - 1u, 0u, CubemapFaceCount);
						const auto source = builder.Read(importanceTexture, RGTextureAccess::Sample, sourceDesc.m_Subresources);
						data.m_SourceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(source, sourceDesc);
					}
					const auto targetDesc = MakeRHITexture2DArrayViewDesc(RHIFormat::R32Float, mip, 0u, CubemapFaceCount);
					builder.WriteInPlace(importanceTexture, RGTextureAccess::RenderTarget, targetDesc.m_Subresources);
					for (uint32_t face = 0u; face < CubemapFaceCount; ++face)
					{
						data.m_Rtvs[face] = builder.CreateView<RHITextureViewType::RenderTarget>(importanceTexture,
							MakeRHITexture2DArrayViewDesc(RHIFormat::R32Float, mip, face, 1u));
					}
				},
				[this, services, registry, mip, resolution, sourceMip, samplerIndex, physicalSky](
					RGExecuteContext& executeContext, ImportancePassData& data)
				{
					auto* commandContext = executeContext.GetGraphicsCommandContext();
					commandContext->SetPipeline(services.m_PipelineResolver->Resolve(
						m_ImportancePipelineSlot, m_ImportanceRecipe, GetInfo()));
					const uint32_t size = resolution >> mip;
					commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size) });
					commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(size), static_cast<int32_t>(size) });
					uint32_t sourceIndex;
					if (mip == 0u)
						sourceIndex = registry->GetIBLBakeShaderVisibleSrvIndex(RenderTextureIndex::IBL_EnvironmentCubemap);
					else
					{
						const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
						GGLAB_ASSERT_MSG(sourceSrv.IsValid(), "IBL importance reduction SRV must be shader visible.");
						sourceIndex = sourceSrv.m_Index;
					}
					for (uint32_t face = 0u; face < CubemapFaceCount; ++face)
					{
						const RHIRenderingAttachment attachment{
							.m_View = executeContext.GetViewHandle(data.m_Rtvs[face]),
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&attachment, 1) });
						commandContext->SetPushConstants(static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							IBLImportancePassParameters{
								.CubemapFaceIndex = face,
								.MipLevel = mip,
								.SourceTextureIndex = sourceIndex,
								.EnvironmentSamplerIndex = samplerIndex,
								.ImportanceResolution = resolution,
								.EnvironmentSourceMip = sourceMip,
								.PhysicalSky = physicalSky,
							});
						commandContext->DrawFullscreenTriangle();
						commandContext->EndRendering();
					}
				});
		}
		return importanceTexture;
	}

	void RenderPassIBLPrefilteredSpecular::EnsureInitialized(
		const RenderServices& services) noexcept
	{

		auto* shaderManager = services.m_ShaderPrograms;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			const auto vsId =
				shaderManager->LoadProgram(shader_programs::IBLPrefilteredSpecularVertex);
			const auto psId =
				shaderManager->LoadProgram(shader_programs::IBLPrefilteredSpecularPixel);

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
			m_ImportanceRecipe = m_BaseRecipe;
			m_ImportanceRecipe.m_VSId = shaderManager->LoadProgram(shader_programs::IBLImportanceVertex);
			m_ImportanceRecipe.m_PSId = shaderManager->LoadProgram(shader_programs::IBLImportancePixel);
			m_ImportanceRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R32Float;

			m_IsInitialized = true;
		}
	}

	RHIPipelineHandle RenderPassIBLPrefilteredSpecular::GetOrCreatePSO(
		const RenderServices& services, RHIFormat renderTargetFormat) noexcept
	{
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GraphicsPhysicalPipelineKey recipe = m_BaseRecipe;
		recipe.m_Formats.m_RenderTargetFormats[0] = renderTargetFormat;
		return pipelineCache->Resolve(m_PipelineSlot, recipe, GetInfo());
	}
}
