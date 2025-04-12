#pragma once
namespace graphicsGadgetLab
{
	class DX12RootSignature;
	class DX12PipelineState;

	enum class CommonRSRootParamIndex : uint32_t
	{
		ConstantBufferIndex,
		TextureDescriptorTable,
		RootParamCount
	};

	enum class RootSignatureIndex : uint32_t
	{
		CommonRootSignature = 0,
		RootSignatureCount
	};
	using RootSignatureArray = std::array<std::unique_ptr<DX12RootSignature>, static_cast<size_t>(RootSignatureIndex::RootSignatureCount)>;

	enum class PSOIndex : uint32_t
	{
		TexturedModelPSO = 0,
		PSOCount
	};
	using PSOArray = std::array<std::unique_ptr<DX12PipelineState>, static_cast<size_t>(PSOIndex::PSOCount)>;


	struct Vertex
	{
		XMFLOAT3 m_Position;
		XMFLOAT3 m_Normal;
		XMFLOAT2 m_TexCoord;
	};
}