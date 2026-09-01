#include "GGLabFoundation/Base/TypeUtils.h"
#include "GGLabRuntime/Graphics/Pipeline/DepthCoverage.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"

#include <format>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		void AppendMismatch(std::string& output, std::string_view category, std::string_view field)
		{
			if (!output.empty())
			{
				output += ", ";
			}
			output += category;
			output += '.';
			output += field;
		}

		void ComparePipelineSignatureFields(const DepthCoveragePipelineSignature& lhs,
			const DepthCoveragePipelineSignature& rhs, std::string& mismatch)
		{
			if (lhs.m_CoverageVertexShader != rhs.m_CoverageVertexShader)
			{
				AppendMismatch(mismatch, "Pipeline", "CoverageVertexShader");
			}
			if (lhs.m_VertexProgram != rhs.m_VertexProgram)
				AppendMismatch(mismatch, "Pipeline", "VertexProgram");
			if (lhs.m_Deformation != rhs.m_Deformation)
				AppendMismatch(mismatch, "Pipeline", "Deformation");
			if (lhs.m_InputLayout != rhs.m_InputLayout)
				AppendMismatch(mismatch, "Pipeline", "InputLayout");
			if (lhs.m_PositionPrecision != rhs.m_PositionPrecision)
				AppendMismatch(mismatch, "Pipeline", "PositionPrecision");
			if (lhs.m_PositionFormat != rhs.m_PositionFormat)
				AppendMismatch(mismatch, "Pipeline", "PositionFormat");
			if (lhs.m_TopologyType != rhs.m_TopologyType)
				AppendMismatch(mismatch, "Pipeline", "TopologyType");
			if (lhs.m_PrimitiveTopology != rhs.m_PrimitiveTopology)
				AppendMismatch(mismatch, "Pipeline", "PrimitiveTopology");
			if (lhs.m_FillMode != rhs.m_FillMode)
				AppendMismatch(mismatch, "Pipeline", "FillMode");
			if (lhs.m_CullMode != rhs.m_CullMode)
				AppendMismatch(mismatch, "Pipeline", "CullMode");
			if (lhs.m_FrontCounterClockwise != rhs.m_FrontCounterClockwise)
				AppendMismatch(mismatch, "Pipeline", "FrontCounterClockwise");
			if (lhs.m_DepthBias != rhs.m_DepthBias)
				AppendMismatch(mismatch, "Pipeline", "DepthBias");
			if (lhs.m_DepthBiasClamp != rhs.m_DepthBiasClamp)
				AppendMismatch(mismatch, "Pipeline", "DepthBiasClamp");
			if (lhs.m_SlopeScaledDepthBias != rhs.m_SlopeScaledDepthBias)
				AppendMismatch(mismatch, "Pipeline", "SlopeScaledDepthBias");
			if (lhs.m_DepthClipEnable != rhs.m_DepthClipEnable)
				AppendMismatch(mismatch, "Pipeline", "DepthClipEnable");
			if (lhs.m_DoubleSided != rhs.m_DoubleSided)
				AppendMismatch(mismatch, "Pipeline", "DoubleSided");
			if (lhs.m_SampleCount != rhs.m_SampleCount)
				AppendMismatch(mismatch, "Pipeline", "SampleCount");
			if (lhs.m_SampleQuality != rhs.m_SampleQuality)
				AppendMismatch(mismatch, "Pipeline", "SampleQuality");
			if (lhs.m_SampleMask != rhs.m_SampleMask)
				AppendMismatch(mismatch, "Pipeline", "SampleMask");
			if (lhs.m_AlphaToCoverageEnable != rhs.m_AlphaToCoverageEnable)
				AppendMismatch(mismatch, "Pipeline", "AlphaToCoverage");
			if (lhs.m_AlphaVariant != rhs.m_AlphaVariant)
				AppendMismatch(mismatch, "Pipeline", "AlphaVariant");
		}

		void CompareRasterDomainFields(const DepthCoverageRasterDomain& lhs,
			const DepthCoverageRasterDomain& rhs, std::string& mismatch)
		{
			if (lhs.m_FrameSerial != rhs.m_FrameSerial)
				AppendMismatch(mismatch, "RasterDomain", "FrameSerial");
			if (lhs.m_ViewBindingId != rhs.m_ViewBindingId)
				AppendMismatch(mismatch, "RasterDomain", "ViewBindingId");
			if (lhs.m_CurrentViewSource != rhs.m_CurrentViewSource)
				AppendMismatch(mismatch, "RasterDomain", "CurrentViewSource");
			if (lhs.m_CurrentJitteredProjectionSource != rhs.m_CurrentJitteredProjectionSource)
			{
				AppendMismatch(mismatch, "RasterDomain", "CurrentJitteredProjectionSource");
			}
			if (lhs.m_ProjectionSource != rhs.m_ProjectionSource)
				AppendMismatch(mismatch, "RasterDomain", "ProjectionSource");
			if (lhs.m_TargetWidth != rhs.m_TargetWidth)
				AppendMismatch(mismatch, "RasterDomain", "TargetWidth");
			if (lhs.m_TargetHeight != rhs.m_TargetHeight)
				AppendMismatch(mismatch, "RasterDomain", "TargetHeight");
			if (lhs.m_Viewport != rhs.m_Viewport)
				AppendMismatch(mismatch, "RasterDomain", "Viewport");
			if (lhs.m_Scissor != rhs.m_Scissor)
				AppendMismatch(mismatch, "RasterDomain", "Scissor");
			if (lhs.m_DepthConvention != rhs.m_DepthConvention)
				AppendMismatch(mismatch, "RasterDomain", "DepthConvention");
		}

		std::string DescribeSource(const DepthCoverageBufferSource& source)
		{
			return std::format("{}:{}:{}", source.m_Buffer.Index(), source.m_Buffer.Generation(),
				source.m_ElementIndex);
		}
	}

	DepthCoveragePipelineSignature BuildDepthCoveragePipelineSignature(
		const GraphicsPhysicalPipelineKey& physicalKey, DepthCoverageVertexProgram vertexProgram,
		DepthCoverageDeformationVariant deformation,
		DepthCoveragePositionPrecision positionPrecision, RHIFormat positionFormat,
		bool doubleSided, DepthCoverageAlphaVariant alphaVariant) noexcept
	{
		const RHIGraphicsPipelineDesc pipeline = BuildRHIGraphicsPipelineDesc(physicalKey);
		return {
			.m_CoverageVertexShader = physicalKey.m_VSId,
			.m_VertexProgram = vertexProgram,
			.m_Deformation = deformation,
			.m_InputLayout = physicalKey.m_InputLayoutId,
			.m_PositionPrecision = positionPrecision,
			.m_PositionFormat = positionFormat,
			.m_TopologyType = pipeline.m_TopologyType,
			.m_PrimitiveTopology = pipeline.m_PrimitiveTopology,
			.m_FillMode = pipeline.m_Rasterizer.m_FillMode,
			.m_CullMode = pipeline.m_Rasterizer.m_CullMode,
			.m_FrontCounterClockwise = pipeline.m_Rasterizer.m_FrontCounterClockwise,
			.m_DepthBias = pipeline.m_Rasterizer.m_DepthBias,
			.m_DepthBiasClamp = pipeline.m_Rasterizer.m_DepthBiasClamp,
			.m_SlopeScaledDepthBias = pipeline.m_Rasterizer.m_SlopeScaledDepthBias,
			.m_DepthClipEnable = pipeline.m_Rasterizer.m_DepthClipEnable,
			.m_DoubleSided = doubleSided,
			.m_SampleCount = pipeline.m_SampleCount,
			.m_SampleQuality = pipeline.m_SampleQuality,
			.m_SampleMask = pipeline.m_SampleMask,
			.m_AlphaToCoverageEnable = pipeline.m_Blend.m_AlphaToCoverageEnable,
			.m_AlphaVariant = alphaVariant,
		};
	}

	DepthCoverageValidationResult CompareDepthCoveragePipelineSignatures(
		const DepthCoveragePipelineSignature& lhs, const DepthCoveragePipelineSignature& rhs)
	{
		const bool lhsValid = lhs.IsValid();
		const bool rhsValid = rhs.IsValid();
		DepthCoverageValidationResult result{
			.m_Matches = lhsValid && rhsValid && lhs == rhs,
		};
		if (!lhsValid)
		{
			AppendMismatch(result.m_Mismatch, "Pipeline", "LhsInvalid");
		}
		if (!rhsValid)
		{
			AppendMismatch(result.m_Mismatch, "Pipeline", "RhsInvalid");
		}
		if (lhsValid && rhsValid && !result.m_Matches)
		{
			ComparePipelineSignatureFields(lhs, rhs, result.m_Mismatch);
		}
		return result;
	}

	DepthCoverageValidationResult CompareDepthCoverageRasterDomains(
		const DepthCoverageRasterDomain& lhs, const DepthCoverageRasterDomain& rhs)
	{
		const bool lhsValid = lhs.IsValid();
		const bool rhsValid = rhs.IsValid();
		DepthCoverageValidationResult result{
			.m_Matches = lhsValid && rhsValid && lhs == rhs,
		};
		if (!lhsValid)
		{
			AppendMismatch(result.m_Mismatch, "RasterDomain", "LhsInvalid");
		}
		if (!rhsValid)
		{
			AppendMismatch(result.m_Mismatch, "RasterDomain", "RhsInvalid");
		}
		if (lhsValid && rhsValid && !result.m_Matches)
		{
			CompareRasterDomainFields(lhs, rhs, result.m_Mismatch);
		}
		return result;
	}

	bool IsDepthCoverageTargetExtentCompatible(
		const DepthCoverageRasterDomain& rasterDomain, const RHITextureDesc& textureDesc) noexcept
	{
		return textureDesc.m_Extent.m_Depth == 1 &&
			rasterDomain.MatchesTargetExtent(
				textureDesc.m_Extent.m_Width, textureDesc.m_Extent.m_Height);
	}

	bool AreDepthCoverageTargetExtentsCompatible(const DepthCoverageRasterDomain& rasterDomain,
		const RHITextureDesc& colorDesc, const RHITextureDesc& depthDesc) noexcept
	{
		return IsDepthCoverageTargetExtentCompatible(rasterDomain, colorDesc) &&
			IsDepthCoverageTargetExtentCompatible(rasterDomain, depthDesc) &&
			colorDesc.m_Extent.m_Width == depthDesc.m_Extent.m_Width &&
			colorDesc.m_Extent.m_Height == depthDesc.m_Extent.m_Height &&
			colorDesc.m_Extent.m_Depth == depthDesc.m_Extent.m_Depth &&
			colorDesc.m_SampleCount == depthDesc.m_SampleCount;
	}

	bool IsSameDepthCoverageDrawPacket(
		const DepthCoverageDrawPacket& lhs, const DepthCoverageDrawPacket& rhs) noexcept
	{
		return std::addressof(lhs) == std::addressof(rhs);
	}

	std::string DescribeDepthCoveragePipelineSignature(
		const DepthCoveragePipelineSignature& signature)
	{
		return std::format("CoverageVS={} VertexProgram={} Deformation={} InputLayout={} "
			"PositionPrecision={} PositionFormat={} Topology={}/{} "
			"Raster={}/{}/ccw:{} Bias={}/{}/{} DepthClip={} "
			"DoubleSided={} Samples={}/{} Mask=0x{:08X} "
			"AlphaToCoverage={} AlphaVariant={}",
			signature.m_CoverageVertexShader.Value(), utils::ToUnderlying(signature.m_VertexProgram),
			utils::ToUnderlying(signature.m_Deformation),
			utils::ToUnderlying(signature.m_InputLayout),
			utils::ToUnderlying(signature.m_PositionPrecision),
			utils::ToUnderlying(signature.m_PositionFormat),
			utils::ToUnderlying(signature.m_TopologyType),
			utils::ToUnderlying(signature.m_PrimitiveTopology),
			utils::ToUnderlying(signature.m_FillMode), utils::ToUnderlying(signature.m_CullMode),
			signature.m_FrontCounterClockwise, signature.m_DepthBias, signature.m_DepthBiasClamp,
			signature.m_SlopeScaledDepthBias, signature.m_DepthClipEnable, signature.m_DoubleSided,
			signature.m_SampleCount, signature.m_SampleQuality, signature.m_SampleMask,
			signature.m_AlphaToCoverageEnable, utils::ToUnderlying(signature.m_AlphaVariant));
	}

	std::string DescribeDepthCoverageRasterDomain(const DepthCoverageRasterDomain& domain)
	{
		return std::format("Frame={} ViewBinding={} View={} JitteredProjection={} "
			"ProjectionSource={} Target={}x{} "
			"Viewport={},{},{},{} Depth={}:{} "
			"Scissor={},{},{},{} DepthConvention={}",
			domain.m_FrameSerial, domain.m_ViewBindingId,
			DescribeSource(domain.m_CurrentViewSource),
			DescribeSource(domain.m_CurrentJitteredProjectionSource),
			utils::ToUnderlying(domain.m_ProjectionSource), domain.m_TargetWidth,
			domain.m_TargetHeight, domain.m_Viewport.m_X, domain.m_Viewport.m_Y,
			domain.m_Viewport.m_Width, domain.m_Viewport.m_Height, domain.m_Viewport.m_MinDepth,
			domain.m_Viewport.m_MaxDepth, domain.m_Scissor.m_Left, domain.m_Scissor.m_Top,
			domain.m_Scissor.m_Right, domain.m_Scissor.m_Bottom,
			utils::ToUnderlying(domain.m_DepthConvention));
	}

	std::string DescribeDepthCoverageDrawPacket(const DepthCoverageDrawPacket& packet)
	{
		const uint32_t meshId = packet.m_Geometry.m_MeshId.IsValid()
			? packet.m_Geometry.m_MeshId.Value()
			: std::numeric_limits<uint32_t>::max();
		return std::format("Mesh={} VertexBuffer={}:{}+{} stride={} size={} "
			"IndexBuffer={}:{}+{} format={} size={} "
			"DrawIndexed={},{},{},{},{} Draw.ObjectOffset={} "
			"Model={} MaterialAlpha={}",
			meshId, packet.m_Geometry.m_VertexBuffer.m_Buffer.Index(),
			packet.m_Geometry.m_VertexBuffer.m_Buffer.Generation(),
			packet.m_Geometry.m_VertexBuffer.m_Offset, packet.m_Geometry.m_VertexBuffer.m_Stride,
			packet.m_Geometry.m_VertexBuffer.m_SizeInBytes,
			packet.m_Geometry.m_IndexBuffer.m_Buffer.Index(),
			packet.m_Geometry.m_IndexBuffer.m_Buffer.Generation(),
			packet.m_Geometry.m_IndexBuffer.m_Offset,
			utils::ToUnderlying(packet.m_Geometry.m_IndexBuffer.m_Format),
			packet.m_Geometry.m_IndexBuffer.m_SizeInBytes, packet.m_IndexedDraw.m_IndexCount,
			packet.m_IndexedDraw.m_InstanceCount, packet.m_IndexedDraw.m_StartIndexLocation,
			packet.m_IndexedDraw.m_BaseVertexLocation, packet.m_IndexedDraw.m_StartInstanceLocation,
			packet.m_DrawParameters.ObjectOffset, DescribeSource(packet.m_CurrentModelSource),
			DescribeSource(packet.m_MaterialAlphaSource));
	}
}
