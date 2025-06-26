#pragma once
#include "DX12Descriptor.h"
namespace graphicsGadgetLab
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
		Vector3 m_Position;
		Vector3 m_Normal;
		Vector2 m_TexCoord;
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

	// Simple Cube Model
}