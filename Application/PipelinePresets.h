#pragma once
#include <d3dx12.h>

namespace gglab
{
	enum class RasterizerPreset : uint8_t
	{
		Default,
		Wireframe,
		TwoSided,
		CullFront,
	};
	constexpr inline CD3DX12_RASTERIZER_DESC ApplyRasterizerPreset(RasterizerPreset preset) noexcept
	{
		CD3DX12_RASTERIZER_DESC desc(D3D12_DEFAULT);
		switch (preset)
		{
		default: [[fallthrough]];
		case RasterizerPreset::Default:
			break;
		case RasterizerPreset::Wireframe:
			desc.FillMode = D3D12_FILL_MODE_WIREFRAME;
			desc.CullMode = D3D12_CULL_MODE_NONE;
			break;
		case RasterizerPreset::TwoSided:
			desc.FillMode = D3D12_FILL_MODE_SOLID;
			desc.CullMode = D3D12_CULL_MODE_NONE;
			break;
		case RasterizerPreset::CullFront:
			desc.FillMode = D3D12_FILL_MODE_SOLID;
			desc.CullMode = D3D12_CULL_MODE_FRONT;
			break;
		}
		return desc;
	}

	enum class BlendPreset : uint8_t
	{
		Default,
		AlphaBlend,
		Additive,
		PremultipliedAlpha,
		ColorWriteDisable,
	};
	constexpr inline CD3DX12_BLEND_DESC ApplyBlendPreset(BlendPreset preset) noexcept
	{
		CD3DX12_BLEND_DESC desc(D3D12_DEFAULT);
		auto& rt0 = desc.RenderTarget[0];
		switch (preset)
		{
		default: [[fallthrough]];
		case BlendPreset::Default:
			break;
		case BlendPreset::AlphaBlend:
			rt0.BlendEnable = TRUE;
			rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt0.BlendOp = D3D12_BLEND_OP_ADD;
			rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			break;
		case BlendPreset::Additive:
			rt0.BlendEnable = TRUE;
			rt0.SrcBlend = D3D12_BLEND_ONE;
			rt0.DestBlend = D3D12_BLEND_ONE;
			rt0.BlendOp = D3D12_BLEND_OP_ADD;
			rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt0.DestBlendAlpha = D3D12_BLEND_ONE;
			rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			break;
		case BlendPreset::PremultipliedAlpha:
			rt0.BlendEnable = TRUE;
			rt0.SrcBlend = D3D12_BLEND_ONE;
			rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt0.BlendOp = D3D12_BLEND_OP_ADD;
			rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			break;
		case BlendPreset::ColorWriteDisable:
			rt0.BlendEnable = FALSE;
			rt0.RenderTargetWriteMask = 0;
			break;
		}
		return desc;
	}

	enum class DepthPreset : uint8_t
	{
		Default,
		DepthReadOnly,
		DepthDisabled,
		ReverseZ,
		ReverseZReadOnly,
	};
	constexpr inline CD3DX12_DEPTH_STENCIL_DESC1 ApplyDepthPreset(DepthPreset preset) noexcept
	{
		CD3DX12_DEPTH_STENCIL_DESC1 desc(D3D12_DEFAULT);
		switch (preset)
		{
		default: [[fallthrough]];
		case DepthPreset::Default:
			break;
		case DepthPreset::DepthReadOnly:
			desc.DepthEnable = TRUE;
			desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			break;
		case DepthPreset::DepthDisabled:
			desc.DepthEnable = FALSE;
			desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			break;
		case DepthPreset::ReverseZ:
			desc.DepthEnable = TRUE;
			desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
			break;
		case DepthPreset::ReverseZReadOnly:
			desc.DepthEnable = TRUE;
			desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			break;
		}
		return desc;
	}
}