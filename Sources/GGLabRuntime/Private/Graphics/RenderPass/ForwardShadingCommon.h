#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

// Mechanics shared by the Forward PBR shading passes: root layout, pipeline-key
// construction, scene inputs and draw submission. Each pass owns its lighting,
// output and bucket policy.
namespace gglab::forward_shading
{
	enum class RootParameter : uint32_t
	{
		ShadowConstants = static_cast<uint32_t>(CommonRSRootParamIndex::Count),
		TileHeaders,
		TileIndices,
	};

	struct PassParameters
	{
		uint32_t m_ViewIndex = 0;
		uint32_t m_ShadowMapTextureIndex = 0;
		uint32_t m_ShadowMapSamplerIndex = 0;
		uint32_t m_ShadowMapSize = 0;
		uint32_t m_ShadowFlags = 0;
		uint32_t m_AtmosphereTransmittanceIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_AtmosphereSamplerIndex = 0;
		uint32_t m_ForwardPlusTileCountX = 0;
		uint32_t m_ForwardPlusTileCountY = 0;
		uint32_t m_ForwardPlusGlobalLightCount = 0;
		std::array<uint32_t, 2> m_ForwardPlusGlobalLightIndices01{};
		std::array<uint32_t, 2> m_ForwardPlusGlobalLightIndices23{};
		// The Forward+ tile grid and GTAO are render-domain and indexed by raster pixel;
		// only pre-temporal shading binds them.
		uint32_t m_GTAOTextureIndex = 0;
		uint32_t m_GTAOFlags = 0;
	};
	static_assert(IsPassRootConstantStruct<PassParameters>);
	static_assert(sizeof(PassParameters) == 64);

	inline constexpr uint32_t GTAOEnabledFlag = 1u;
	inline constexpr size_t MaterialDiagnosticTargetCount = 3;

	// Creates the common binding layout plus shadow constants and, for Forward+
	// shading, the tile light-list buffers.
	[[nodiscard]] RHIBindingLayoutHandle CreateBindingLayout(
		const RenderServices& services, bool forwardPlusLightLists) noexcept;

	// HDR scene-color target over D32 depth; presets are resolved per draw variant.
	[[nodiscard]] GraphicsPhysicalPipelineKey MakeBasePhysicalKey(RHIBindingLayoutHandle bindingLayout,
		ShaderID vertexShader, ShaderID pixelShader) noexcept;

	// Appends the color, coverage and lighting material diagnostic targets.
	void AppendMaterialDiagnosticTargets(GraphicsPhysicalPipelineKey& key) noexcept;

	[[nodiscard]] RasterizerPreset GetRasterizerPreset(uint64_t variantBits) noexcept;

	// Display-view inputs read by every Forward shading pass.
	struct SceneInputs
	{
		RGTextureId m_SceneColor{};
		RGTextureId m_Depth{};
		RGTextureId m_IrradianceCubemap{};
		RGTextureId m_PrefilteredSpecularCubemap{};
		RGTextureId m_BrdfLut{};
		RGTextureId m_ShadowMap{};
		RGTextureId m_AtmosphereTransmittance{};

		RGTextureViewId m_Rtv{};
		RGTextureViewId m_Dsv{};
		RGTextureViewId m_ShadowSrv{};
		RGTextureViewId m_AtmosphereTransmittanceSrv{};
		std::array<RGTextureViewId, MaterialDiagnosticTargetCount> m_MaterialDiagnosticRtvs{};
		bool m_MaterialDiagnostics = false;

		const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
		const RenderQueue* m_ExpectedRenderQueue = nullptr;
		uint32_t m_ShadowMapSize = 0;
		uint32_t m_ShadowSamplerIndex = 0;
		uint32_t m_ShadowFlags = 0;
	};

	// Resolution domain a Forward shading pass composes into.
	enum class CompositionDomain : uint8_t
	{
		// Render-domain scene color, scene depth and the coverage raster domain, before the
		// temporal resolve.
		PreTemporal,
		// Display-domain color, display depth and the post-temporal raster domain, after
		// the temporal resolve.
		PostTemporal,
	};

	// Declares the domain's color and material diagnostics as render targets, IBL,
	// atmosphere and shadow inputs as samples, and the domain's depth as read-only depth.
	void DeclareSceneInputs(RenderGraph::RGBuilder& builder, const RenderFrameContext& context,
		const RenderServices& services, RenderViewID viewId, CompositionDomain domain,
		SceneInputs& inputs) noexcept;

	[[nodiscard]] RHIRenderingAttachment GetSceneColorAttachment(
		RGExecuteContext& executeContext, const SceneInputs& inputs) noexcept;
	[[nodiscard]] RHIRenderingAttachment GetDepthAttachment(
		RGExecuteContext& executeContext, const SceneInputs& inputs) noexcept;
	// Appends the material diagnostic attachments after the pass-specific targets.
	void AppendMaterialDiagnosticAttachments(RGExecuteContext& executeContext,
		const SceneInputs& inputs, std::span<RHIRenderingAttachment> attachments,
		uint32_t& attachmentCount) noexcept;

	// Resolves the view, shadow and atmosphere pass constants; the caller adds
	// lighting-recipe and GTAO constants.
	[[nodiscard]] PassParameters ResolveScenePassParameters(RGExecuteContext& executeContext,
		const SceneInputs& inputs, const RenderServices& services, RenderViewID viewId) noexcept;

	[[nodiscard]] const DrawItemsRange* FindFirstDrawRange(
		const RenderQueue& renderQueue, std::span<const RenderBucket> buckets) noexcept;

	// Binds raster state, scene and shadow constants and the frame structured buffers.
	// A pipeline must already be set so that the root layout is established.
	void BindSceneResources(RHIGraphicsCommandContext& graphicsContext,
		const RenderFrameContext& context, const RenderServices& services,
		const SceneInputs& inputs, const RenderQueue& renderQueue) noexcept;

	// Draws the bucket ranges with the exact draw packets accepted by the frame plan.
	template <typename ResolvePipeline>
	void DrawBuckets(RHIGraphicsCommandContext& graphicsContext, const RenderQueue& renderQueue,
		std::span<const RenderBucket> buckets, const RenderQueue* expectedRenderQueue,
		ResolvePipeline&& resolvePipeline) noexcept
	{
		GGLAB_ASSERT_MSG(renderQueue.m_CoverageRasterDomain.IsValid(),
			"Forward depth coverage requires one valid raster domain per view.");
		for (const RenderBucket bucket : buckets)
		{
			const DrawItemsRange range = renderQueue.m_BucketDrawRanges[utils::ToIndex(bucket)];
			uint64_t lastVariantBits = std::numeric_limits<uint64_t>::max();
			MeshID lastMeshId{};
			bool hasBoundMesh = false;
			for (uint32_t index = 0; index < range.m_Count; ++index)
			{
				const auto& drawItem = renderQueue.m_DrawItems[range.m_Start + index];
				const DepthCoverageDrawPacket& drawPacket = drawItem.m_CoverageDrawPacket;
				GGLAB_ASSERT_MSG(drawPacket.IsValid(),
					"Forward received an incomplete shared depth coverage draw packet.");
				GGLAB_ASSERT_MSG(expectedRenderQueue == std::addressof(renderQueue) &&
					IsSameDepthCoverageDrawPacket(drawPacket,
						expectedRenderQueue->m_DrawItems[range.m_Start + index]
						.m_CoverageDrawPacket),
					"Forward must consume the exact draw-packet instances accepted by the frame plan.");

				if (drawItem.m_VariantBits != lastVariantBits)
				{
					graphicsContext.SetPipeline(resolvePipeline(drawItem.m_VariantBits));
					lastVariantBits = drawItem.m_VariantBits;
				}

				const auto& geometry = drawPacket.m_Geometry;
				if (!hasBoundMesh || geometry.m_MeshId != lastMeshId)
				{
					graphicsContext.SetVertexBuffers(
						0, std::span<const RHIVertexBufferBinding>(&geometry.m_VertexBuffer, 1));
					graphicsContext.SetIndexBuffer(geometry.m_IndexBuffer);
					lastMeshId = geometry.m_MeshId;
					hasBoundMesh = true;
				}

				graphicsContext.SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
					drawPacket.m_DrawParameters);

				const auto& draw = drawPacket.m_IndexedDraw;
				graphicsContext.DrawIndexed(draw.m_IndexCount, draw.m_InstanceCount,
					draw.m_StartIndexLocation, draw.m_BaseVertexLocation, draw.m_StartInstanceLocation);
			}
		}
	}
}
