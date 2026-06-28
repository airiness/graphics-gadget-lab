#pragma once
#include "Core/Hash/FNV1a.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Shader/ShaderTypes.h"

namespace gglab
{
	struct PipelineFormats
	{
		D3D12_RT_FORMAT_ARRAY m_RtvFormats = {};
		DXGI_FORMAT m_DsvFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RtvFormats.NumRenderTargets,
				m_RtvFormats.RTFormats[0],
				m_RtvFormats.RTFormats[1],
				m_RtvFormats.RTFormats[2],
				m_RtvFormats.RTFormats[3],
				m_RtvFormats.RTFormats[4],
				m_RtvFormats.RTFormats[5],
				m_RtvFormats.RTFormats[6],
				m_RtvFormats.RTFormats[7],
				m_DsvFormat,
				m_SampleCount,
				m_SampleQuality);
		}
		constexpr bool operator==(const PipelineFormats& rhs) const noexcept
		{
			return AsTuple() == rhs.AsTuple();
		}
	};

	// ComputePSOKey
	struct ComputePSOKey
	{
		RootSignatureID m_RootSignatureId{};
		ShaderHash128 m_ComputeShader{};

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_RootSignatureId.Value(),
				m_ComputeShader.m_LowBits, m_ComputeShader.m_HighBits);
		}
		constexpr bool operator==(const ComputePSOKey&) const noexcept = default;
	};
	using ComputePSOKeyHash = KeyHash<ComputePSOKey>;
}
