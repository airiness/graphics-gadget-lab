#include "Core/Precompiled.h"
#include "Graphics/RHIPipelineRecipeAdapter.h"

#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	namespace
	{
		constexpr uint32_t Float2Size = sizeof(float) * 2u;
		constexpr uint32_t Float3Size = sizeof(float) * 3u;
		constexpr uint32_t Float4Size = sizeof(float) * 4u;

		[[nodiscard]] RHIShaderHandle ToRHIShaderHandle(ShaderID shaderId) noexcept
		{
			if (!shaderId.IsValid())
			{
				return {};
			}
			return RHIShaderHandle{ shaderId.Value(), 1u };
		}

		void AddVertexAttribute(
			RHIVertexInputLayoutDesc& desc,
			const char* semanticName,
			uint32_t semanticIndex,
			RHIFormat format,
			uint32_t offset) noexcept
		{
			GGLAB_ASSERT(desc.m_AttributeCount < RHIVertexInputLayoutDesc::MaxAttributes);
			auto& attribute = desc.m_Attributes[desc.m_AttributeCount++];
			attribute.m_SemanticName = semanticName;
			attribute.m_SemanticIndex = semanticIndex;
			attribute.m_Format = format;
			attribute.m_InputSlot = 0;
			attribute.m_AlignedByteOffset = offset;
		}

		void SetVertexBufferLayout(
			RHIVertexInputLayoutDesc& desc,
			uint32_t strideInBytes) noexcept
		{
			GGLAB_ASSERT(desc.m_VertexBufferCount < RHIVertexInputLayoutDesc::MaxVertexBuffers);
			auto& vertexBuffer = desc.m_VertexBuffers[desc.m_VertexBufferCount++];
			vertexBuffer.m_InputSlot = 0;
			vertexBuffer.m_StrideInBytes = strideInBytes;
			vertexBuffer.m_InputRate = RHIVertexInputRate::PerVertex;
			vertexBuffer.m_InstanceStepRate = 0;
		}

		[[nodiscard]] RHIPrimitiveTopologyType ToRHITopologyType(
			D3D12_PRIMITIVE_TOPOLOGY_TYPE topology) noexcept
		{
			switch (topology)
			{
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
				return RHIPrimitiveTopologyType::Point;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
				return RHIPrimitiveTopologyType::Line;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
				return RHIPrimitiveTopologyType::Triangle;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
				return RHIPrimitiveTopologyType::Patch;
			default:
				return RHIPrimitiveTopologyType::Unknown;
			}
		}

		[[nodiscard]] RHIPrimitiveTopology ToRHIPrimitiveTopology(
			D3D12_PRIMITIVE_TOPOLOGY_TYPE topology) noexcept
		{
			switch (topology)
			{
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
				return RHIPrimitiveTopology::PointList;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
				return RHIPrimitiveTopology::LineList;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
				return RHIPrimitiveTopology::TriangleList;
			default:
				return RHIPrimitiveTopology::Unknown;
			}
		}

		[[nodiscard]] RHIRasterizerDesc ToRHIRasterizerDesc(
			const GraphicsPipelineRecipe& recipe) noexcept
		{
			RHIRasterizerDesc desc{};
			switch (recipe.m_RasterizerPreset)
			{
			case RasterizerPreset::Wireframe:
				desc.m_FillMode = RHIFillMode::Wireframe;
				desc.m_CullMode = RHICullMode::None;
				break;
			case RasterizerPreset::TwoSided:
				desc.m_FillMode = RHIFillMode::Solid;
				desc.m_CullMode = RHICullMode::None;
				break;
			case RasterizerPreset::CullFront:
				desc.m_FillMode = RHIFillMode::Solid;
				desc.m_CullMode = RHICullMode::Front;
				break;
			case RasterizerPreset::Default:
			default:
				desc.m_FillMode = RHIFillMode::Solid;
				desc.m_CullMode = RHICullMode::Back;
				break;
			}
			desc.m_DepthBias = recipe.m_DepthBias;
			desc.m_DepthBiasClamp = recipe.m_DepthBiasClamp;
			desc.m_SlopeScaledDepthBias = recipe.m_SlopeScaledDepthBias;
			desc.m_DepthClipEnable = true;
			return desc;
		}

		[[nodiscard]] RHIDepthStencilDesc ToRHIDepthStencilDesc(
			DepthPreset preset) noexcept
		{
			RHIDepthStencilDesc desc{};
			switch (preset)
			{
			case DepthPreset::DepthReadOnly:
				desc.m_DepthTestEnable = true;
				desc.m_DepthWriteEnable = false;
				desc.m_DepthCompareOp = RHICompareOp::LessEqual;
				break;
			case DepthPreset::DepthDisabled:
				desc.m_DepthTestEnable = false;
				desc.m_DepthWriteEnable = false;
				desc.m_DepthCompareOp = RHICompareOp::Less;
				break;
			case DepthPreset::ReverseZ:
				desc.m_DepthTestEnable = true;
				desc.m_DepthWriteEnable = true;
				desc.m_DepthCompareOp = RHICompareOp::Greater;
				break;
			case DepthPreset::ReverseZReadOnly:
				desc.m_DepthTestEnable = true;
				desc.m_DepthWriteEnable = false;
				desc.m_DepthCompareOp = RHICompareOp::GreaterEqual;
				break;
			case DepthPreset::Default:
			default:
				desc.m_DepthTestEnable = true;
				desc.m_DepthWriteEnable = true;
				desc.m_DepthCompareOp = RHICompareOp::Less;
				break;
			}
			return desc;
		}

		[[nodiscard]] RHIBlendDesc ToRHIBlendDesc(BlendPreset preset) noexcept
		{
			RHIBlendDesc desc{};
			auto& rt0 = desc.m_RenderTargets[0];
			switch (preset)
			{
			case BlendPreset::AlphaBlend:
				rt0.m_BlendEnable = true;
				rt0.m_SrcColor = RHIBlendFactor::SrcAlpha;
				rt0.m_DstColor = RHIBlendFactor::OneMinusSrcAlpha;
				rt0.m_SrcAlpha = RHIBlendFactor::One;
				rt0.m_DstAlpha = RHIBlendFactor::OneMinusSrcAlpha;
				break;
			case BlendPreset::Additive:
				rt0.m_BlendEnable = true;
				rt0.m_SrcColor = RHIBlendFactor::One;
				rt0.m_DstColor = RHIBlendFactor::One;
				rt0.m_SrcAlpha = RHIBlendFactor::One;
				rt0.m_DstAlpha = RHIBlendFactor::One;
				break;
			case BlendPreset::PremultipliedAlpha:
				rt0.m_BlendEnable = true;
				rt0.m_SrcColor = RHIBlendFactor::One;
				rt0.m_DstColor = RHIBlendFactor::OneMinusSrcAlpha;
				rt0.m_SrcAlpha = RHIBlendFactor::One;
				rt0.m_DstAlpha = RHIBlendFactor::OneMinusSrcAlpha;
				break;
			case BlendPreset::ColorWriteDisable:
				rt0.m_BlendEnable = false;
				rt0.m_WriteMask = RHIColorWriteMask::None;
				break;
			case BlendPreset::Default:
			default:
				break;
			}
			return desc;
		}
	}

	RHIVertexInputLayoutDesc BuildRHIVertexInputLayoutDesc(InputLayoutID inputLayoutId) noexcept
	{
		RHIVertexInputLayoutDesc desc{};
		uint32_t offset = 0;
		switch (inputLayoutId)
		{
		case InputLayoutID::P3:
			AddVertexAttribute(desc, "POSITION", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			SetVertexBufferLayout(desc, offset);
			break;
		case InputLayoutID::P3T2:
			AddVertexAttribute(desc, "POSITION", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			AddVertexAttribute(desc, "TEXCOORD", 0, RHIFormat::R32G32Float, offset);
			offset += Float2Size;
			SetVertexBufferLayout(desc, offset);
			break;
		case InputLayoutID::P3N3:
			AddVertexAttribute(desc, "POSITION", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			AddVertexAttribute(desc, "NORMAL", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			SetVertexBufferLayout(desc, offset);
			break;
		case InputLayoutID::P3N3T2:
			AddVertexAttribute(desc, "POSITION", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			AddVertexAttribute(desc, "NORMAL", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			AddVertexAttribute(desc, "TEXCOORD", 0, RHIFormat::R32G32Float, offset);
			offset += Float2Size;
			SetVertexBufferLayout(desc, offset);
			break;
		case InputLayoutID::P3N3T2T2Tan4:
			AddVertexAttribute(desc, "POSITION", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			AddVertexAttribute(desc, "NORMAL", 0, RHIFormat::R32G32B32Float, offset);
			offset += Float3Size;
			AddVertexAttribute(desc, "TEXCOORD", 0, RHIFormat::R32G32Float, offset);
			offset += Float2Size;
			AddVertexAttribute(desc, "TEXCOORD", 1, RHIFormat::R32G32Float, offset);
			offset += Float2Size;
			AddVertexAttribute(desc, "TANGENT", 0, RHIFormat::R32G32B32A32Float, offset);
			offset += Float4Size;
			SetVertexBufferLayout(desc, offset);
			break;
		case InputLayoutID::None:
		default:
			break;
		}
		return desc;
	}

	RHIGraphicsPipelineDesc BuildRHIGraphicsPipelineDesc(
		const GraphicsPipelineRecipe& recipe,
		RHIBindingLayoutHandle bindingLayout) noexcept
	{
		RHIGraphicsPipelineDesc desc{};
		desc.m_BindingLayout = bindingLayout;
		desc.m_VertexShader = ToRHIShaderHandle(recipe.m_VSId);
		desc.m_PixelShader = ToRHIShaderHandle(recipe.m_PSId);
		desc.m_VertexInput = BuildRHIVertexInputLayoutDesc(recipe.m_InputLayoutId);
		desc.m_TopologyType = ToRHITopologyType(recipe.m_Topology);
		desc.m_PrimitiveTopology = ToRHIPrimitiveTopology(recipe.m_Topology);
		desc.m_Rasterizer = ToRHIRasterizerDesc(recipe);
		desc.m_DepthStencil = ToRHIDepthStencilDesc(recipe.m_DepthPreset);
		desc.m_Blend = ToRHIBlendDesc(recipe.m_BlendPreset);
		desc.m_RenderTargetCount = recipe.m_Formats.m_RtvFormats.NumRenderTargets;
		for (uint32_t i = 0; i < desc.m_RenderTargetCount && i < RHIGraphicsPipelineDesc::MaxRenderTargets; ++i)
		{
			desc.m_RenderTargetFormats[i] = ToRHIFormat(recipe.m_Formats.m_RtvFormats.RTFormats[i]);
		}
		desc.m_DepthStencilFormat = ToRHIFormat(recipe.m_Formats.m_DsvFormat);
		desc.m_SampleCount = recipe.m_Formats.m_SampleCount;
		desc.m_SampleQuality = recipe.m_Formats.m_SampleQuality;
		desc.m_SampleMask = recipe.m_SampleMask;
		return desc;
	}
}
