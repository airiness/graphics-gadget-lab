#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12InputLayoutUtils.h"

#include "Graphics/Utility/DXGIFormatUtils.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] D3D12_INPUT_CLASSIFICATION ToDX12InputClassification(
			RHIVertexInputRate inputRate) noexcept
		{
			switch (inputRate)
			{
			case RHIVertexInputRate::PerInstance:
				return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
			case RHIVertexInputRate::PerVertex:
			default:
				return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			}
		}

		[[nodiscard]] const RHIVertexBufferLayoutDesc* FindVertexBufferLayout(
			const RHIVertexInputLayoutDesc& rhiDesc,
			uint32_t inputSlot) noexcept
		{
			for (uint32_t i = 0; i < rhiDesc.m_VertexBufferCount; ++i)
			{
				if (rhiDesc.m_VertexBuffers[i].m_InputSlot == inputSlot)
				{
					return &rhiDesc.m_VertexBuffers[i];
				}
			}
			return nullptr;
		}
	}

	void BuildDX12InputLayoutDesc(
		const RHIVertexInputLayoutDesc& rhiDesc,
		DX12InputLayoutBuildResult& outDesc) noexcept
	{
		outDesc = {};
		outDesc.m_ElementCount = rhiDesc.m_AttributeCount;
		GGLAB_ASSERT(outDesc.m_ElementCount <= RHIVertexInputLayoutDesc::MaxAttributes);

		for (uint32_t i = 0; i < outDesc.m_ElementCount; ++i)
		{
			const auto& attribute = rhiDesc.m_Attributes[i];
			const auto* vertexBuffer = FindVertexBufferLayout(rhiDesc, attribute.m_InputSlot);

			auto& element = outDesc.m_Elements[i];
			element.SemanticName = attribute.m_SemanticName;
			element.SemanticIndex = attribute.m_SemanticIndex;
			element.Format = ToDXGIFormat(attribute.m_Format);
			element.InputSlot = attribute.m_InputSlot;
			element.AlignedByteOffset = attribute.m_AlignedByteOffset;
			element.InputSlotClass = vertexBuffer ?
				ToDX12InputClassification(vertexBuffer->m_InputRate) :
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			element.InstanceDataStepRate = vertexBuffer ? vertexBuffer->m_InstanceStepRate : 0;
		}

		outDesc.m_Desc = D3D12_INPUT_LAYOUT_DESC{
			outDesc.m_Elements.data(),
			outDesc.m_ElementCount
		};
	}
}
