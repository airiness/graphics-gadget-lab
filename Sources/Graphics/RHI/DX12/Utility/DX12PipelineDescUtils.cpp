#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12PipelineDescUtils.h"

#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	namespace
	{
		void MixShaderHash(uint64_t& lowBits, uint64_t& highBits, const ShaderHash128& hash) noexcept
		{
			FNV1a64::MixValue(lowBits, hash.m_LowBits);
			FNV1a64::MixValue(lowBits, hash.m_HighBits);
			FNV1a64::MixValue(highBits, hash.m_LowBits);
			FNV1a64::MixValue(highBits, hash.m_HighBits);
		}

		[[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY_TYPE ToDX12PrimitiveTopologyType(
			RHIPrimitiveTopologyType topologyType) noexcept
		{
			switch (topologyType)
			{
			case RHIPrimitiveTopologyType::Point:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
			case RHIPrimitiveTopologyType::Line:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			case RHIPrimitiveTopologyType::Triangle:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			case RHIPrimitiveTopologyType::Patch:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
			case RHIPrimitiveTopologyType::Unknown:
			default:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
			}
		}

		[[nodiscard]] D3D12_FILL_MODE ToDX12FillMode(RHIFillMode fillMode) noexcept
		{
			switch (fillMode)
			{
			case RHIFillMode::Wireframe:
				return D3D12_FILL_MODE_WIREFRAME;
			case RHIFillMode::Solid:
			default:
				return D3D12_FILL_MODE_SOLID;
			}
		}

		[[nodiscard]] D3D12_CULL_MODE ToDX12CullMode(RHICullMode cullMode) noexcept
		{
			switch (cullMode)
			{
			case RHICullMode::None:
				return D3D12_CULL_MODE_NONE;
			case RHICullMode::Front:
				return D3D12_CULL_MODE_FRONT;
			case RHICullMode::Back:
			default:
				return D3D12_CULL_MODE_BACK;
			}
		}

		[[nodiscard]] D3D12_COMPARISON_FUNC ToDX12ComparisonFunc(RHICompareOp compareOp) noexcept
		{
			switch (compareOp)
			{
			case RHICompareOp::Never:
				return D3D12_COMPARISON_FUNC_NEVER;
			case RHICompareOp::Less:
				return D3D12_COMPARISON_FUNC_LESS;
			case RHICompareOp::Equal:
				return D3D12_COMPARISON_FUNC_EQUAL;
			case RHICompareOp::LessEqual:
				return D3D12_COMPARISON_FUNC_LESS_EQUAL;
			case RHICompareOp::Greater:
				return D3D12_COMPARISON_FUNC_GREATER;
			case RHICompareOp::NotEqual:
				return D3D12_COMPARISON_FUNC_NOT_EQUAL;
			case RHICompareOp::GreaterEqual:
				return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			case RHICompareOp::Always:
			default:
				return D3D12_COMPARISON_FUNC_ALWAYS;
			}
		}

		[[nodiscard]] D3D12_BLEND ToDX12Blend(RHIBlendFactor blend) noexcept
		{
			switch (blend)
			{
			case RHIBlendFactor::Zero:
				return D3D12_BLEND_ZERO;
			case RHIBlendFactor::One:
				return D3D12_BLEND_ONE;
			case RHIBlendFactor::SrcColor:
				return D3D12_BLEND_SRC_COLOR;
			case RHIBlendFactor::OneMinusSrcColor:
				return D3D12_BLEND_INV_SRC_COLOR;
			case RHIBlendFactor::SrcAlpha:
				return D3D12_BLEND_SRC_ALPHA;
			case RHIBlendFactor::OneMinusSrcAlpha:
				return D3D12_BLEND_INV_SRC_ALPHA;
			case RHIBlendFactor::DstAlpha:
				return D3D12_BLEND_DEST_ALPHA;
			case RHIBlendFactor::OneMinusDstAlpha:
				return D3D12_BLEND_INV_DEST_ALPHA;
			case RHIBlendFactor::DstColor:
				return D3D12_BLEND_DEST_COLOR;
			case RHIBlendFactor::OneMinusDstColor:
				return D3D12_BLEND_INV_DEST_COLOR;
			default:
				return D3D12_BLEND_ONE;
			}
		}

		[[nodiscard]] D3D12_BLEND_OP ToDX12BlendOp(RHIBlendOp op) noexcept
		{
			switch (op)
			{
			case RHIBlendOp::Subtract:
				return D3D12_BLEND_OP_SUBTRACT;
			case RHIBlendOp::ReverseSubtract:
				return D3D12_BLEND_OP_REV_SUBTRACT;
			case RHIBlendOp::Min:
				return D3D12_BLEND_OP_MIN;
			case RHIBlendOp::Max:
				return D3D12_BLEND_OP_MAX;
			case RHIBlendOp::Add:
			default:
				return D3D12_BLEND_OP_ADD;
			}
		}

		[[nodiscard]] UINT8 ToDX12ColorWriteMask(RHIColorWriteMask mask) noexcept
		{
			UINT8 dx12Mask = 0;
			if (Test(mask, RHIColorWriteMask::Red))
			{
				dx12Mask |= D3D12_COLOR_WRITE_ENABLE_RED;
			}
			if (Test(mask, RHIColorWriteMask::Green))
			{
				dx12Mask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
			}
			if (Test(mask, RHIColorWriteMask::Blue))
			{
				dx12Mask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
			}
			if (Test(mask, RHIColorWriteMask::Alpha))
			{
				dx12Mask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
			}
			return dx12Mask;
		}

		[[nodiscard]] CD3DX12_RASTERIZER_DESC ToDX12RasterizerDesc(
			const RHIRasterizerDesc& rhiDesc) noexcept
		{
			CD3DX12_RASTERIZER_DESC desc(D3D12_DEFAULT);
			desc.FillMode = ToDX12FillMode(rhiDesc.m_FillMode);
			desc.CullMode = ToDX12CullMode(rhiDesc.m_CullMode);
			desc.FrontCounterClockwise = rhiDesc.m_FrontCounterClockwise;
			desc.DepthBias = rhiDesc.m_DepthBias;
			desc.DepthBiasClamp = rhiDesc.m_DepthBiasClamp;
			desc.SlopeScaledDepthBias = rhiDesc.m_SlopeScaledDepthBias;
			desc.DepthClipEnable = rhiDesc.m_DepthClipEnable;
			return desc;
		}

		[[nodiscard]] CD3DX12_DEPTH_STENCIL_DESC1 ToDX12DepthStencilDesc(
			const RHIDepthStencilDesc& rhiDesc) noexcept
		{
			CD3DX12_DEPTH_STENCIL_DESC1 desc(D3D12_DEFAULT);
			desc.DepthEnable = rhiDesc.m_DepthTestEnable;
			desc.DepthWriteMask = rhiDesc.m_DepthWriteEnable ?
				D3D12_DEPTH_WRITE_MASK_ALL :
				D3D12_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = ToDX12ComparisonFunc(rhiDesc.m_DepthCompareOp);
			desc.StencilEnable = rhiDesc.m_StencilEnable;
			return desc;
		}

		[[nodiscard]] CD3DX12_BLEND_DESC ToDX12BlendDesc(
			const RHIBlendDesc& rhiDesc,
			uint32_t renderTargetCount) noexcept
		{
			CD3DX12_BLEND_DESC desc(D3D12_DEFAULT);
			desc.AlphaToCoverageEnable = rhiDesc.m_AlphaToCoverageEnable;
			const uint32_t count = std::min(renderTargetCount, RHIBlendDesc::MaxRenderTargets);
			for (uint32_t i = 0; i < count; ++i)
			{
				const auto& rhiTarget = rhiDesc.m_RenderTargets[i];
				auto& dx12Target = desc.RenderTarget[i];
				dx12Target.BlendEnable = rhiTarget.m_BlendEnable;
				dx12Target.SrcBlend = ToDX12Blend(rhiTarget.m_SrcColor);
				dx12Target.DestBlend = ToDX12Blend(rhiTarget.m_DstColor);
				dx12Target.BlendOp = ToDX12BlendOp(rhiTarget.m_ColorOp);
				dx12Target.SrcBlendAlpha = ToDX12Blend(rhiTarget.m_SrcAlpha);
				dx12Target.DestBlendAlpha = ToDX12Blend(rhiTarget.m_DstAlpha);
				dx12Target.BlendOpAlpha = ToDX12BlendOp(rhiTarget.m_AlphaOp);
				dx12Target.RenderTargetWriteMask = ToDX12ColorWriteMask(rhiTarget.m_WriteMask);
			}
			return desc;
		}
	}

	GraphicsPipelineDesc BuildDX12GraphicsPipelineDesc(
		const RHIGraphicsPipelineDesc& rhiDesc,
		const RootSignatureHandle& rootSignature,
		const DX12GraphicsPipelineShaderInputs& shaderInputs,
		const DX12InputLayoutBuildResult& inputLayout) noexcept
	{
		GraphicsPipelineDesc desc{};
		desc.m_RootSignatureId = rootSignature.m_Id;
		desc.m_RootSignature = rootSignature.m_RootSignature;
		desc.m_InputLayoutId = InputLayoutID::None;
		desc.m_InputLayoutDesc = inputLayout.m_Desc;
		desc.m_VertexShader = shaderInputs.m_VertexShader;
		desc.m_PixelShader = shaderInputs.m_PixelShader;
		desc.m_DomainShader = shaderInputs.m_DomainShader;
		desc.m_HullShader = shaderInputs.m_HullShader;
		desc.m_GeometryShader = shaderInputs.m_GeometryShader;
		desc.m_Formats.m_RtvFormats.NumRenderTargets = rhiDesc.m_RenderTargetCount;
		for (uint32_t i = 0; i < rhiDesc.m_RenderTargetCount && i < RHIGraphicsPipelineDesc::MaxRenderTargets; ++i)
		{
			desc.m_Formats.m_RtvFormats.RTFormats[i] =
				ToDXGIFormat(rhiDesc.m_RenderTargetFormats[i]);
		}
		desc.m_Formats.m_DsvFormat = ToDXGIFormat(rhiDesc.m_DepthStencilFormat);
		desc.m_Formats.m_SampleCount = rhiDesc.m_SampleCount;
		desc.m_Formats.m_SampleQuality = rhiDesc.m_SampleQuality;
		desc.m_Topology = ToDX12PrimitiveTopologyType(rhiDesc.m_TopologyType);
		desc.m_SampleMask = rhiDesc.m_SampleMask;
		desc.m_RasterizerDesc = ToDX12RasterizerDesc(rhiDesc.m_Rasterizer);
		desc.m_DepthDesc = ToDX12DepthStencilDesc(rhiDesc.m_DepthStencil);
		desc.m_BlendDesc = ToDX12BlendDesc(rhiDesc.m_Blend, rhiDesc.m_RenderTargetCount);
		return desc;
	}

	DX12RHIGraphicsPSOKey MakeDX12RHIGraphicsPSOKey(
		const RHIGraphicsPipelineDesc& rhiDesc,
		RootSignatureID rootSignatureId,
		const DX12GraphicsPipelineShaderInputs& shaderInputs) noexcept
	{
		DX12RHIGraphicsPSOKey key{};
		key.m_LowBits = FNV1a64::OffsetBasis;
		key.m_HighBits = 0x9ae16a3b2f90404full;

		const auto mix = [&](auto value) noexcept
			{
				if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, bool>)
				{
					const uint8_t boolValue = value ? 1u : 0u;
					FNV1a64::MixValue(key.m_LowBits, boolValue);
					FNV1a64::MixValue(key.m_HighBits, boolValue);
				}
				else
				{
					FNV1a64::MixValue(key.m_LowBits, value);
					FNV1a64::MixValue(key.m_HighBits, value);
				}
			};

		mix(rootSignatureId.Value());
		mix(rhiDesc.m_BindingLayout.Index());
		mix(rhiDesc.m_BindingLayout.Generation());
		mix(rhiDesc.m_VertexShader.Index());
		mix(rhiDesc.m_VertexShader.Generation());
		mix(rhiDesc.m_PixelShader.Index());
		mix(rhiDesc.m_PixelShader.Generation());
		MixShaderHash(key.m_LowBits, key.m_HighBits, shaderInputs.m_VertexShaderHash);
		MixShaderHash(key.m_LowBits, key.m_HighBits, shaderInputs.m_PixelShaderHash);
		MixShaderHash(key.m_LowBits, key.m_HighBits, shaderInputs.m_DomainShaderHash);
		MixShaderHash(key.m_LowBits, key.m_HighBits, shaderInputs.m_HullShaderHash);
		MixShaderHash(key.m_LowBits, key.m_HighBits, shaderInputs.m_GeometryShaderHash);

		mix(rhiDesc.m_VertexInput.m_AttributeCount);
		for (uint32_t i = 0; i < rhiDesc.m_VertexInput.m_AttributeCount; ++i)
		{
			const auto& attribute = rhiDesc.m_VertexInput.m_Attributes[i];
			mix(attribute.m_SemanticName);
			mix(attribute.m_SemanticIndex);
			mix(attribute.m_Format);
			mix(attribute.m_InputSlot);
			mix(attribute.m_AlignedByteOffset);
		}
		mix(rhiDesc.m_VertexInput.m_VertexBufferCount);
		for (uint32_t i = 0; i < rhiDesc.m_VertexInput.m_VertexBufferCount; ++i)
		{
			const auto& vertexBuffer = rhiDesc.m_VertexInput.m_VertexBuffers[i];
			mix(vertexBuffer.m_InputSlot);
			mix(vertexBuffer.m_StrideInBytes);
			mix(vertexBuffer.m_InputRate);
			mix(vertexBuffer.m_InstanceStepRate);
		}

		mix(rhiDesc.m_TopologyType);
		mix(rhiDesc.m_PrimitiveTopology);
		mix(rhiDesc.m_Rasterizer.m_FillMode);
		mix(rhiDesc.m_Rasterizer.m_CullMode);
		mix(rhiDesc.m_Rasterizer.m_FrontCounterClockwise);
		mix(rhiDesc.m_Rasterizer.m_DepthBias);
		mix(rhiDesc.m_Rasterizer.m_DepthBiasClamp);
		mix(rhiDesc.m_Rasterizer.m_SlopeScaledDepthBias);
		mix(rhiDesc.m_Rasterizer.m_DepthClipEnable);
		mix(rhiDesc.m_DepthStencil.m_DepthTestEnable);
		mix(rhiDesc.m_DepthStencil.m_DepthWriteEnable);
		mix(rhiDesc.m_DepthStencil.m_DepthCompareOp);
		mix(rhiDesc.m_DepthStencil.m_StencilEnable);
		mix(rhiDesc.m_Blend.m_AlphaToCoverageEnable);
		for (uint32_t i = 0; i < RHIBlendDesc::MaxRenderTargets; ++i)
		{
			const auto& target = rhiDesc.m_Blend.m_RenderTargets[i];
			mix(target.m_BlendEnable);
			mix(target.m_SrcColor);
			mix(target.m_DstColor);
			mix(target.m_ColorOp);
			mix(target.m_SrcAlpha);
			mix(target.m_DstAlpha);
			mix(target.m_AlphaOp);
			mix(target.m_WriteMask);
		}
		mix(rhiDesc.m_RenderTargetCount);
		for (uint32_t i = 0; i < RHIGraphicsPipelineDesc::MaxRenderTargets; ++i)
		{
			mix(rhiDesc.m_RenderTargetFormats[i]);
		}
		mix(rhiDesc.m_DepthStencilFormat);
		mix(rhiDesc.m_SampleCount);
		mix(rhiDesc.m_SampleQuality);
		mix(rhiDesc.m_SampleMask);
		return key;
	}
}
