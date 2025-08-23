#pragma once
#include "DX12Descriptor.h"
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

	enum class RenderTargetIndex : uint32_t
	{
		RT0,
		RT1,
		RT2,
		DS0,
		RTCount
	};
	using RenderTargetArray = std::array<std::unique_ptr<DX12Texture>, static_cast<uint32_t>(RenderTargetIndex::RTCount)>;
	using RenderTargetDescriptors = std::array<DX12Descriptor, static_cast<uint32_t>(RenderTargetIndex::RTCount)>;

	struct GlobalConstantBuffer
	{
		Matrix m_ModelMatrix;
		Matrix m_ViewMatrix;
		Matrix m_ProjectionMatrix;
	};
}