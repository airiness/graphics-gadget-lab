#include "Core/Precompiled.h"
#include "Graphics/Pipeline/DepthCoverage.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"

namespace gglab
{
	namespace
	{
		void AppendMismatch(
			std::string& output,
			std::string_view category,
			std::string_view field)
		{
			if (!output.empty())
			{
				output += ", ";
			}
			output += category;
			output += '.';
			output += field;
		}

		void CompareSignatureFields(
			const DepthCoverageSignature& lhs,
			const DepthCoverageSignature& rhs,
			std::string& mismatch)
		{
			if (lhs.m_VertexProgram != rhs.m_VertexProgram)
				AppendMismatch(mismatch, "Signature", "VertexProgram");
			if (lhs.m_Deformation != rhs.m_Deformation)
				AppendMismatch(mismatch, "Signature", "Deformation");
			if (lhs.m_InputLayout != rhs.m_InputLayout)
				AppendMismatch(mismatch, "Signature", "InputLayout");
			if (lhs.m_PositionPrecision != rhs.m_PositionPrecision)
				AppendMismatch(mismatch, "Signature", "PositionPrecision");
			if (lhs.m_PositionFormat != rhs.m_PositionFormat)
				AppendMismatch(mismatch, "Signature", "PositionFormat");
			if (lhs.m_TopologyType != rhs.m_TopologyType)
				AppendMismatch(mismatch, "Signature", "TopologyType");
			if (lhs.m_PrimitiveTopology != rhs.m_PrimitiveTopology)
				AppendMismatch(mismatch, "Signature", "PrimitiveTopology");
			if (lhs.m_FillMode != rhs.m_FillMode)
				AppendMismatch(mismatch, "Signature", "FillMode");
			if (lhs.m_CullMode != rhs.m_CullMode)
				AppendMismatch(mismatch, "Signature", "CullMode");
			if (lhs.m_FrontCounterClockwise != rhs.m_FrontCounterClockwise)
				AppendMismatch(mismatch, "Signature", "FrontCounterClockwise");
			if (lhs.m_DepthBias != rhs.m_DepthBias)
				AppendMismatch(mismatch, "Signature", "DepthBias");
			if (lhs.m_DepthBiasClamp != rhs.m_DepthBiasClamp)
				AppendMismatch(mismatch, "Signature", "DepthBiasClamp");
			if (lhs.m_SlopeScaledDepthBias != rhs.m_SlopeScaledDepthBias)
				AppendMismatch(mismatch, "Signature", "SlopeScaledDepthBias");
			if (lhs.m_DepthClipEnable != rhs.m_DepthClipEnable)
				AppendMismatch(mismatch, "Signature", "DepthClipEnable");
			if (lhs.m_DoubleSided != rhs.m_DoubleSided)
				AppendMismatch(mismatch, "Signature", "DoubleSided");
			if (lhs.m_SampleCount != rhs.m_SampleCount)
				AppendMismatch(mismatch, "Signature", "SampleCount");
			if (lhs.m_SampleQuality != rhs.m_SampleQuality)
				AppendMismatch(mismatch, "Signature", "SampleQuality");
			if (lhs.m_SampleMask != rhs.m_SampleMask)
				AppendMismatch(mismatch, "Signature", "SampleMask");
			if (lhs.m_AlphaToCoverageEnable != rhs.m_AlphaToCoverageEnable)
				AppendMismatch(mismatch, "Signature", "AlphaToCoverage");
			if (lhs.m_AlphaVariant != rhs.m_AlphaVariant)
				AppendMismatch(mismatch, "Signature", "AlphaVariant");
		}

		void CompareBindingFields(
			const DepthCoverageBinding& lhs,
			const DepthCoverageBinding& rhs,
			std::string& mismatch)
		{
			if (lhs.m_FrameSerial != rhs.m_FrameSerial)
				AppendMismatch(mismatch, "Binding", "FrameSerial");
			if (lhs.m_ViewBindingId != rhs.m_ViewBindingId)
				AppendMismatch(mismatch, "Binding", "ViewBindingId");
			if (lhs.m_CurrentModelSource != rhs.m_CurrentModelSource)
				AppendMismatch(mismatch, "Binding", "CurrentModelSource");
			if (lhs.m_CurrentViewSource != rhs.m_CurrentViewSource)
				AppendMismatch(mismatch, "Binding", "CurrentViewSource");
			if (lhs.m_CurrentJitteredProjectionSource !=
				rhs.m_CurrentJitteredProjectionSource)
			{
				AppendMismatch(
					mismatch,
					"Binding",
					"CurrentJitteredProjectionSource");
			}
			if (lhs.m_ProjectionSource != rhs.m_ProjectionSource)
				AppendMismatch(mismatch, "Binding", "ProjectionSource");
			if (lhs.m_MaterialAlphaSource != rhs.m_MaterialAlphaSource)
				AppendMismatch(mismatch, "Binding", "MaterialAlphaSource");
		}
	}

	DepthCoverageSignature BuildDepthCoverageSignature(
		const GraphicsPipelineRecipe& recipe,
		DepthCoverageVertexProgram vertexProgram,
		DepthCoverageDeformationVariant deformation,
		DepthCoveragePositionPrecision positionPrecision,
		RHIFormat positionFormat,
		bool doubleSided,
		DepthCoverageAlphaVariant alphaVariant) noexcept
	{
		const RHIGraphicsPipelineDesc pipeline =
			BuildRHIGraphicsPipelineDesc(recipe);
		return {
			.m_VertexProgram = vertexProgram,
			.m_Deformation = deformation,
			.m_InputLayout = recipe.m_InputLayoutId,
			.m_PositionPrecision = positionPrecision,
			.m_PositionFormat = positionFormat,
			.m_TopologyType = pipeline.m_TopologyType,
			.m_PrimitiveTopology = pipeline.m_PrimitiveTopology,
			.m_FillMode = pipeline.m_Rasterizer.m_FillMode,
			.m_CullMode = pipeline.m_Rasterizer.m_CullMode,
			.m_FrontCounterClockwise =
				pipeline.m_Rasterizer.m_FrontCounterClockwise,
			.m_DepthBias = pipeline.m_Rasterizer.m_DepthBias,
			.m_DepthBiasClamp = pipeline.m_Rasterizer.m_DepthBiasClamp,
			.m_SlopeScaledDepthBias =
				pipeline.m_Rasterizer.m_SlopeScaledDepthBias,
			.m_DepthClipEnable =
				pipeline.m_Rasterizer.m_DepthClipEnable,
			.m_DoubleSided = doubleSided,
			.m_SampleCount = pipeline.m_SampleCount,
			.m_SampleQuality = pipeline.m_SampleQuality,
			.m_SampleMask = pipeline.m_SampleMask,
			.m_AlphaToCoverageEnable =
				pipeline.m_Blend.m_AlphaToCoverageEnable,
			.m_AlphaVariant = alphaVariant,
		};
	}

	DepthCoverageComparison CompareDepthCoverageContracts(
		const DepthCoverageSignature& lhsSignature,
		const DepthCoverageBinding& lhsBinding,
		const DepthCoverageSignature& rhsSignature,
		const DepthCoverageBinding& rhsBinding)
	{
		const bool lhsSignatureValid = lhsSignature.IsValid();
		const bool rhsSignatureValid = rhsSignature.IsValid();
		const bool lhsBindingValid = lhsBinding.IsValid();
		const bool rhsBindingValid = rhsBinding.IsValid();
		DepthCoverageComparison comparison{
			.m_SignatureMatches =
				lhsSignatureValid &&
				rhsSignatureValid &&
				lhsSignature == rhsSignature,
			.m_BindingMatches =
				lhsBindingValid &&
				rhsBindingValid &&
				lhsBinding == rhsBinding,
		};
		if (!lhsSignatureValid)
		{
			AppendMismatch(comparison.m_Mismatch, "Signature", "LhsInvalid");
		}
		if (!rhsSignatureValid)
		{
			AppendMismatch(comparison.m_Mismatch, "Signature", "RhsInvalid");
		}
		if (lhsSignatureValid &&
			rhsSignatureValid &&
			!comparison.m_SignatureMatches)
		{
			CompareSignatureFields(
				lhsSignature,
				rhsSignature,
				comparison.m_Mismatch);
		}
		if (!lhsBindingValid)
		{
			AppendMismatch(comparison.m_Mismatch, "Binding", "LhsInvalid");
		}
		if (!rhsBindingValid)
		{
			AppendMismatch(comparison.m_Mismatch, "Binding", "RhsInvalid");
		}
		if (lhsBindingValid &&
			rhsBindingValid &&
			!comparison.m_BindingMatches)
		{
			CompareBindingFields(
				lhsBinding,
				rhsBinding,
				comparison.m_Mismatch);
		}
		return comparison;
	}

	std::string DescribeDepthCoverageSignature(
		const DepthCoverageSignature& signature)
	{
		return std::format(
			"VertexProgram={} Deformation={} InputLayout={} "
			"PositionPrecision={} PositionFormat={} Topology={}/{} "
			"Raster={}/{}/ccw:{} Bias={}/{}/{} DepthClip={} "
			"DoubleSided={} Samples={}/{} Mask=0x{:08X} "
			"AlphaToCoverage={} AlphaVariant={}",
			std::to_underlying(signature.m_VertexProgram),
			std::to_underlying(signature.m_Deformation),
			std::to_underlying(signature.m_InputLayout),
			std::to_underlying(signature.m_PositionPrecision),
			std::to_underlying(signature.m_PositionFormat),
			std::to_underlying(signature.m_TopologyType),
			std::to_underlying(signature.m_PrimitiveTopology),
			std::to_underlying(signature.m_FillMode),
			std::to_underlying(signature.m_CullMode),
			signature.m_FrontCounterClockwise,
			signature.m_DepthBias,
			signature.m_DepthBiasClamp,
			signature.m_SlopeScaledDepthBias,
			signature.m_DepthClipEnable,
			signature.m_DoubleSided,
			signature.m_SampleCount,
			signature.m_SampleQuality,
			signature.m_SampleMask,
			signature.m_AlphaToCoverageEnable,
			std::to_underlying(signature.m_AlphaVariant));
	}

	std::string DescribeDepthCoverageBinding(
		const DepthCoverageBinding& binding)
	{
		const auto describeSource =
			[](const DepthCoverageBufferSource& source)
			{
				return std::format(
					"{}:{}:{}",
					source.m_Buffer.Index(),
					source.m_Buffer.Generation(),
					source.m_ElementIndex);
			};
		return std::format(
			"Frame={} ViewBinding={} Model={} View={} "
			"JitteredProjection={} ProjectionSource={} MaterialAlpha={}",
			binding.m_FrameSerial,
			binding.m_ViewBindingId,
			describeSource(binding.m_CurrentModelSource),
			describeSource(binding.m_CurrentViewSource),
			describeSource(binding.m_CurrentJitteredProjectionSource),
			std::to_underlying(binding.m_ProjectionSource),
			describeSource(binding.m_MaterialAlphaSource));
	}
}
