#pragma once

namespace gglab
{
	class DX12RootSignature;
	class DX12PipelineState;
	class DX12Texture;

	enum class CommonRSRootParamIndex : uint32_t
	{
		ConstantBufferIndex,
		TextureDescriptorTable,
		RootParamCount
	};

	// TODO: move to GpuStructures.h
	struct GlobalConstantBuffer
	{
		Matrix m_ModelMatrix;
		Matrix m_ViewMatrix;
		Matrix m_ProjectionMatrix;
	};
}