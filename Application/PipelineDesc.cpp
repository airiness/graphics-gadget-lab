#include "Precompiled.h"
#include "PipelineDesc.h"

namespace gglab
{
	D3D12_PIPELINE_STATE_STREAM_DESC GraphicsPipelineDesc::ToStreamDesc(void** outStreamStorage, size_t outStreamCount) const noexcept
	{

		return D3D12_PIPELINE_STATE_STREAM_DESC();
	}

	PSOKey GraphicsPipelineDesc::MakeKey(ShaderHash128 vsHash, ShaderHash128 psHash,
		ShaderHash128 dsHash, ShaderHash128 hsHash, ShaderHash128 gsHash) const noexcept
	{
		PSOKey key{};
		key.m_RootSignatureId = m_RootSignatureId;
		key.m_VertexShader = vsHash;
		key.m_PixelShader = psHash;
		key.m_DomainShader = dsHash;
		key.m_HullShader = hsHash;
		key.m_GeometryShader = gsHash;
		key.m_RtvCount = m_RtvCount;
		for (uint32_t i = 0; i < m_RtvCount && i < 8; ++i)
		{
			key.m_Rtv[i] = m_RtvFormats[i];
		}
		key.m_Dsv = m_DsvFormat;
		key.m_Topology = m_Topology;
		key.m_SampleCount = m_SampleCount;
		key.m_SampleQuality = m_SampleQuality;
		key.m_SampleMask = m_SampleMask;
		key.m_Rasterizer.PackRasterizerBits(m_RasterizerDesc);
		key.m_Depth.PackDepthBits(m_DepthDesc);
		key.m_Blend.PackBlendBits(m_BlendDesc, m_RtvCount);
		return key;
	}
}

