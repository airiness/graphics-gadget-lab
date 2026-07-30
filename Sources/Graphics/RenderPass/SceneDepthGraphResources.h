#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <optional>

namespace gglab
{
	struct RGSceneDepthResources
	{
		RGTextureId m_Texture{};
		RHITextureViewDesc m_DsvDesc{};
		RHITextureViewDesc m_SrvDesc{};
		DepthConvention m_Convention = DepthConvention::Reversed;
	};

	inline constexpr const char* SceneDepthResourcesName = "RGSceneDepthResources";

	struct RGDepthCoverageContract
	{
		const RenderQueue* m_SourceRenderQueue = nullptr;
		const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
		std::array<
			std::optional<DepthCoveragePipelineSignature>,
			RenderQueueBuilder::VariantCount>
			m_PrepassPipelineSignatures{};
	};

	inline constexpr const char* DepthCoverageContractName =
		"RGDepthCoverageContract";
}
