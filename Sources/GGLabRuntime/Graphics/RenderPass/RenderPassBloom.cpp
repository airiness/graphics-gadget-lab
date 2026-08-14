#include "Graphics/RenderPass/RenderPassBloom.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/PostProcess/PostProcessResolution.h"
#include "Graphics/Renderer.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace gglab
{
	namespace
	{
		enum class BloomFilterMode : uint32_t
		{
			Prefilter = 0,
			Downsample = 1,
			Upsample = 2,
		};

		struct BloomPassParameters
		{
			uint32_t SourceTextureIndex = 0;
			uint32_t SourceSamplerIndex = 0;
			uint32_t FilterMode = 0;
			uint32_t Padding0 = 0;
			float SourceTexelSizeX = 0.0f;
			float SourceTexelSizeY = 0.0f;
			float Threshold = 0.0f;
			float SoftKnee = 0.0f;
			float ExposureScaleOverPreExposure = 1.0f;
			float Scatter = 1.0f;
			float Padding1 = 0.0f;
			float Padding2 = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<BloomPassParameters>);
		static_assert(sizeof(BloomPassParameters) == 48);

		struct PassData
		{
			RGTextureId m_Source{};
			RGTextureId m_Output{};
			RGTextureViewId m_SourceSrv{};
			RGTextureViewId m_OutputRtv{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SourceWidth = 0;
			uint32_t m_SourceHeight = 0;
			uint32_t m_SamplerIndex = 0;
			BloomFilterMode m_FilterMode = BloomFilterMode::Downsample;
			float m_Threshold = 0.0f;
			float m_SoftKnee = 0.0f;
			float m_ExposureScaleOverPreExposure = 1.0f;
			float m_Scatter = 1.0f;
			RHIFormat m_RenderTargetFormat = RHIFormat::Unknown;
		};

		constexpr std::array<const char*, MaxBloomPyramidLevels> BloomPyramidNames = {
			"PostProcess.Bloom.Pyramid.0",
			"PostProcess.Bloom.Pyramid.1",
			"PostProcess.Bloom.Pyramid.2",
			"PostProcess.Bloom.Pyramid.3",
			"PostProcess.Bloom.Pyramid.4",
			"PostProcess.Bloom.Pyramid.5",
			"PostProcess.Bloom.Pyramid.6",
			"PostProcess.Bloom.Pyramid.7",
		};
	}

	void RenderPassBloom::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		AddPass(rg, context, services, DebugTapCallback{});
	}

	void RenderPassBloom::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services, const DebugTapCallback& debugTapCallback) noexcept
	{
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const auto settings = context.GetViewRenderSettings(displayViewId).m_PostProcess.m_Bloom;
		auto& postProcess =
			rg.GetBlackboard().Get<RGPostProcessResources>(PostProcessResourcesName);
		postProcess.m_Bloom = {};
		if (!settings.m_Enabled || settings.m_Intensity <= 0.0f)
		{
			return;
		}

		GGLAB_ASSERT_MSG(
			postProcess.m_Inputs.m_SceneColor.m_State == PostProcessColorState::SceneLinearRec709,
			"Bloom requires scene-linear Rec.709 input.");
		GGLAB_ASSERT_MSG(postProcess.m_Inputs.m_SceneColor.m_PreExposure > 0.0f,
			"Bloom requires a positive scene pre-exposure.");
		EnsureInitialized(services);

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		const uint32_t samplerIndex =
			renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::LinearClamp);
		const float exposureScaleOverPreExposure =
			context.GetViewRenderSettings(displayViewId).m_Exposure.m_ExposureScale /
			postProcess.m_Inputs.m_SceneColor.m_PreExposure;

		const std::string prefilterName = MakeRenderGraphPassName("Prefilter");
		rg.AddPass<PassData>(
			prefilterName.c_str(),
			[settings, samplerIndex, exposureScaleOverPreExposure](
				RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& resources =
					builder.GetBlackboard().Get<RGPostProcessResources>(PostProcessResourcesName);
				auto& bloom = resources.m_Bloom;
				const RGPostProcessColor& sceneColor = resources.m_Inputs.m_SceneColor;
				const RHITextureDesc& sceneDesc = builder.GetTextureDesc(sceneColor.m_Texture);
				GGLAB_ASSERT_MSG(sceneDesc.m_Dimension == RHITextureDimension::Texture2D &&
					sceneDesc.m_SampleCount == 1,
					"Bloom requires a single-sampled 2D scene color texture.");

				RGTextureId extentSource = sceneColor.m_Texture;
				const uint32_t requestedLevels =
					std::min(settings.m_MaxLevels, MaxBloomPyramidLevels);
				for (uint32_t level = 0; level < requestedLevels; ++level)
				{
					const RHITextureDesc levelDesc = MakeRelativeTextureDesc(builder, extentSource,
						PostProcessResolutionScale::Half, RHIFormat::R16G16B16A16Float);
					bloom.m_Pyramid[level] =
						builder.CreateTexture(BloomPyramidNames[level], levelDesc);
					++bloom.m_LevelCount;
					extentSource = bloom.m_Pyramid[level];
					if (levelDesc.m_Extent.m_Width == 1 && levelDesc.m_Extent.m_Height == 1)
					{
						break;
					}
				}

				GGLAB_ASSERT_MSG(bloom.m_LevelCount > 0, "Bloom must create at least one level.");
				data.m_Source = builder.Read(sceneColor.m_Texture, RGTextureAccess::Sample);
				builder.WriteInPlace(bloom.m_Pyramid[0], RGTextureAccess::RenderTarget);
				data.m_Output = bloom.m_Pyramid[0];
				data.m_SourceSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Source);
				data.m_OutputRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);

				const RHITextureDesc& outputDesc = builder.GetTextureDesc(data.m_Output);
				data.m_Width = outputDesc.m_Extent.m_Width;
				data.m_Height = outputDesc.m_Extent.m_Height;
				data.m_SourceWidth = sceneDesc.m_Extent.m_Width;
				data.m_SourceHeight = sceneDesc.m_Extent.m_Height;
				data.m_SamplerIndex = samplerIndex;
				data.m_FilterMode = BloomFilterMode::Prefilter;
				data.m_Threshold = settings.m_Threshold;
				data.m_SoftKnee = settings.m_SoftKnee;
				data.m_ExposureScaleOverPreExposure = exposureScaleOverPreExposure;
				data.m_RenderTargetFormat = outputDesc.m_Format;
			},
			[this, renderer](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
				const auto outputRtv = executeContext.GetViewHandle(data.m_OutputRtv);
				GGLAB_ASSERT_MSG(sourceSrv.IsValid(), "Bloom source SRV must be shader visible.");
				const RHIRenderingAttachment colorAttachment{
					.m_View = outputRtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
				commandContext->SetPipeline(
					GetOrCreatePSO(*renderer, data.m_RenderTargetFormat, false));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });
				const BloomPassParameters parameters{
					.SourceTextureIndex = sourceSrv.m_Index,
					.SourceSamplerIndex = data.m_SamplerIndex,
					.FilterMode = static_cast<uint32_t>(data.m_FilterMode),
					.SourceTexelSizeX = 1.0f / static_cast<float>(data.m_SourceWidth),
					.SourceTexelSizeY = 1.0f / static_cast<float>(data.m_SourceHeight),
					.Threshold = data.m_Threshold,
					.SoftKnee = data.m_SoftKnee,
					.ExposureScaleOverPreExposure = data.m_ExposureScaleOverPreExposure,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->DrawFullscreenTriangle();
			});
		postProcess.m_Bloom.m_Prefilter = {
			.m_Texture = postProcess.m_Bloom.m_Pyramid[0],
			.m_State = PostProcessColorState::SceneLinearRec709,
			.m_PreExposure = postProcess.m_Inputs.m_SceneColor.m_PreExposure,
		};
		postProcess.m_Bloom.m_DownsampledPyramid[0] = postProcess.m_Bloom.m_Prefilter;
		if (debugTapCallback)
		{
			debugTapCallback(
				postProcess.m_Bloom.m_Prefilter, PostProcessDebugTap::BloomPrefilter, 0);
			debugTapCallback(
				postProcess.m_Bloom.m_DownsampledPyramid[0], PostProcessDebugTap::BloomPyramid, 0);
		}

		const uint32_t levelCount = postProcess.m_Bloom.m_LevelCount;
		for (uint32_t level = 1; level < levelCount; ++level)
		{
			const std::string passName =
				MakeRenderGraphPassName(std::format("Downsample.{}", level));
			rg.AddPass<PassData>(
				passName.c_str(),
				[level, samplerIndex](RenderGraph::RGBuilder& builder, PassData& data)
				{
					auto& bloom = builder.GetBlackboard()
						.Get<RGPostProcessResources>(PostProcessResourcesName)
						.m_Bloom;
					data.m_Source =
						builder.Read(bloom.m_Pyramid[level - 1], RGTextureAccess::Sample);
					builder.WriteInPlace(bloom.m_Pyramid[level], RGTextureAccess::RenderTarget);
					data.m_Output = bloom.m_Pyramid[level];
					data.m_SourceSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Source);
					data.m_OutputRtv =
						builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);
					const auto& sourceDesc = builder.GetTextureDesc(data.m_Source);
					const auto& outputDesc = builder.GetTextureDesc(data.m_Output);
					data.m_SourceWidth = sourceDesc.m_Extent.m_Width;
					data.m_SourceHeight = sourceDesc.m_Extent.m_Height;
					data.m_Width = outputDesc.m_Extent.m_Width;
					data.m_Height = outputDesc.m_Extent.m_Height;
					data.m_SamplerIndex = samplerIndex;
					data.m_FilterMode = BloomFilterMode::Downsample;
					data.m_RenderTargetFormat = outputDesc.m_Format;
				},
				[this, renderer](RGExecuteContext& executeContext, PassData& data)
				{
					auto* commandContext = executeContext.GetGraphicsCommandContext();
					const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
					const auto outputRtv = executeContext.GetViewHandle(data.m_OutputRtv);
					GGLAB_ASSERT_MSG(
						sourceSrv.IsValid(), "Bloom source SRV must be shader visible.");
					const RHIRenderingAttachment colorAttachment{
						.m_View = outputRtv,
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
					commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
					commandContext->SetPipeline(
						GetOrCreatePSO(*renderer, data.m_RenderTargetFormat, false));
					commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
						static_cast<float>(data.m_Height) });
					commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
						static_cast<int32_t>(data.m_Height) });
					const BloomPassParameters parameters{
						.SourceTextureIndex = sourceSrv.m_Index,
						.SourceSamplerIndex = data.m_SamplerIndex,
						.FilterMode = static_cast<uint32_t>(data.m_FilterMode),
						.SourceTexelSizeX = 1.0f / static_cast<float>(data.m_SourceWidth),
						.SourceTexelSizeY = 1.0f / static_cast<float>(data.m_SourceHeight),
					};
					commandContext->SetPushConstants(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
					commandContext->DrawFullscreenTriangle();
				});
			postProcess.m_Bloom.m_DownsampledPyramid[level] = {
				.m_Texture = postProcess.m_Bloom.m_Pyramid[level],
				.m_State = PostProcessColorState::SceneLinearRec709,
				.m_PreExposure = postProcess.m_Inputs.m_SceneColor.m_PreExposure,
			};
			if (debugTapCallback)
			{
				debugTapCallback(postProcess.m_Bloom.m_DownsampledPyramid[level],
					PostProcessDebugTap::BloomPyramid, level);
			}
		}

		for (uint32_t sourceLevel = levelCount - 1; sourceLevel > 0; --sourceLevel)
		{
			const uint32_t targetLevel = sourceLevel - 1;
			const std::string passName =
				MakeRenderGraphPassName(std::format("Upsample.{}", targetLevel));
			rg.AddPass<PassData>(
				passName.c_str(),
				[sourceLevel, targetLevel, samplerIndex, settings](
					RenderGraph::RGBuilder& builder, PassData& data)
				{
					auto& bloom = builder.GetBlackboard()
						.Get<RGPostProcessResources>(PostProcessResourcesName)
						.m_Bloom;
					data.m_Source =
						builder.Read(bloom.m_Pyramid[sourceLevel], RGTextureAccess::Sample);
					builder.ReadWriteInPlace(
						bloom.m_Pyramid[targetLevel], RGTextureAccess::RenderTarget);
					data.m_Output = bloom.m_Pyramid[targetLevel];
					data.m_SourceSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Source);
					data.m_OutputRtv =
						builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);
					const auto& sourceDesc = builder.GetTextureDesc(data.m_Source);
					const auto& outputDesc = builder.GetTextureDesc(data.m_Output);
					data.m_SourceWidth = sourceDesc.m_Extent.m_Width;
					data.m_SourceHeight = sourceDesc.m_Extent.m_Height;
					data.m_Width = outputDesc.m_Extent.m_Width;
					data.m_Height = outputDesc.m_Extent.m_Height;
					data.m_SamplerIndex = samplerIndex;
					data.m_FilterMode = BloomFilterMode::Upsample;
					data.m_Scatter = settings.m_Scatter;
					data.m_RenderTargetFormat = outputDesc.m_Format;
				},
				[this, renderer](RGExecuteContext& executeContext, PassData& data)
				{
					auto* commandContext = executeContext.GetGraphicsCommandContext();
					const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
					const auto outputRtv = executeContext.GetViewHandle(data.m_OutputRtv);
					GGLAB_ASSERT_MSG(
						sourceSrv.IsValid(), "Bloom source SRV must be shader visible.");
					commandContext->SetPipeline(
						GetOrCreatePSO(*renderer, data.m_RenderTargetFormat, true));
					const RHIRenderingAttachment colorAttachment{ .m_View = outputRtv };
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
					commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
						static_cast<float>(data.m_Height) });
					commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
						static_cast<int32_t>(data.m_Height) });
					const BloomPassParameters parameters{
						.SourceTextureIndex = sourceSrv.m_Index,
						.SourceSamplerIndex = data.m_SamplerIndex,
						.FilterMode = static_cast<uint32_t>(data.m_FilterMode),
						.SourceTexelSizeX = 1.0f / static_cast<float>(data.m_SourceWidth),
						.SourceTexelSizeY = 1.0f / static_cast<float>(data.m_SourceHeight),
						.Scatter = data.m_Scatter,
					};
					commandContext->SetPushConstants(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
					commandContext->DrawFullscreenTriangle();
				});
		}

		postProcess.m_Bloom.m_Result = {
			.m_Texture = postProcess.m_Bloom.m_Pyramid[0],
			.m_State = PostProcessColorState::SceneLinearRec709,
			.m_PreExposure = postProcess.m_Inputs.m_SceneColor.m_PreExposure,
		};
	}

	void RenderPassBloom::EnsureInitialized(const RenderServices& services) noexcept
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
		shaderDesc.m_SourcePath = L"Passes/PassBloom.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		shaderDesc.m_Entry = L"VSMain";
		m_BaseRecipe.m_VSId = shaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Stage = ShaderStage::Pixel;
		shaderDesc.m_Entry = L"PSMain";
		m_BaseRecipe.m_PSId = shaderManager->LoadShader(shaderDesc);

		m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
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

	RHIPipelineHandle RenderPassBloom::GetOrCreatePSO(
		const Renderer& renderer, RHIFormat renderTargetFormat, bool additive) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GraphicsPhysicalPipelineKey recipe = m_BaseRecipe;
		recipe.m_Formats.m_RenderTargetFormats[0] = renderTargetFormat;
		recipe.m_BlendPreset = additive ? BlendPreset::Additive : BlendPreset::Default;
		return pipelineCache->Resolve(
			additive ? m_AdditivePipelineSlot : m_FilterPipelineSlot, recipe, GetInfo());
	}
}
