#include "Core/Precompiled.h"
#include "Graphics/RHI/RHIPipelineSystemSnapshot.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/DX12PipelineState.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/Cache/DX12PSOCache.h"
#include "Graphics/RHI/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/PipelineCache.h"
#include "Graphics/ShaderManager.h"

namespace gglab
{
	namespace
	{
		bool IsBindless(RHIBindingType type) noexcept
		{
			return type == RHIBindingType::BindlessSampledTextureTable ||
				type == RHIBindingType::BindlessSamplerTable;
		}

		const char* BackendBindingType(const RHIBindingSlotDesc& slot) noexcept
		{
			if (slot.m_Type == RHIBindingType::PushConstants)
			{
				return "Constants";
			}
			if (slot.m_Count == 1)
			{
				switch (slot.m_Type)
				{
				case RHIBindingType::ConstantBuffer: return "RootCBV";
				case RHIBindingType::ReadOnlyStorageBuffer: return "RootSRV";
				case RHIBindingType::ReadWriteStorageBuffer: return "RootUAV";
				default: break;
				}
			}
			return "DescriptorTable";
		}

		RHIShaderSnapshot MakeShaderSnapshot(
			RHIShaderHandle handle,
			ShaderHash128 hash,
			const ShaderManager* shaderManager) noexcept
		{
			RHIShaderSnapshot result{};
			result.m_Handle = handle;
			result.m_Hash = hash;
			result.m_Present = handle.IsValid() || hash.m_LowBits != 0 || hash.m_HighBits != 0;
			if (handle.IsValid() && shaderManager)
			{
				result.m_DebugName = shaderManager->GetDebugName(ShaderID{ handle.Index() });
			}
			return result;
		}
	}

	void BuildDX12PipelineSystemSnapshot(
		const DX12PipelineSystem& system,
		const PipelineCache* pipelineCache,
		RHIPipelineSystemSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		outSnapshot.m_BackendName = "Direct3D 12";
		outSnapshot.m_Cache.m_PipelineSystemRevision = system.GetRevision();
		const ShaderManager* shaderManager =
			pipelineCache ? pipelineCache->GetShaderManager() : nullptr;

		std::shared_lock systemLock(system.m_Mutex);
		outSnapshot.m_BindingLayouts.reserve(system.m_BindingLayouts.size());
		for (const auto& binding : system.m_BindingLayouts)
		{
			if (!binding.m_Registered)
			{
				continue;
			}

			RHIBindingLayoutSnapshot layout{};
			layout.m_Handle = binding.m_Handle;
			layout.m_Alive = binding.m_RootSignature != nullptr &&
				binding.m_RootSignature->Get() != nullptr;
			layout.m_DebugName = binding.m_DebugName;
			layout.m_BackendRootSignatureId = binding.m_Handle.Index();
			layout.m_BackendRootSignaturePointer = reinterpret_cast<uint64_t>(
				binding.m_RootSignature ? binding.m_RootSignature->Get() : nullptr);
			layout.m_Slots.reserve(binding.m_SlotCount);

			int32_t rootParameter = 0;
			for (uint32_t slotIndex = 0; slotIndex < binding.m_SlotCount; ++slotIndex)
			{
				const auto& source = binding.m_Slots[slotIndex];
				RHIBindingSlotSnapshot slot{};
				slot.m_Slot = slotIndex;
				slot.m_Type = source.m_Type;
				slot.m_Visibility = source.m_Visibility;
				slot.m_Binding = source.m_Binding;
				slot.m_Space = source.m_Space;
				slot.m_Count = source.m_Count;
				slot.m_SizeInBytes = source.m_SizeInBytes;
				slot.m_DebugName = binding.m_SlotDebugNames[slotIndex];

				if (IsBindless(source.m_Type))
				{
					slot.m_BackendMapping = RHIBindingBackendMapping::DirectlyIndexed;
					slot.m_BackendBindingType = "DIRECTLY_INDEXED";
					layout.m_DirectlyIndexedResources |=
						source.m_Type == RHIBindingType::BindlessSampledTextureTable;
					layout.m_DirectlyIndexedSamplers |=
						source.m_Type == RHIBindingType::BindlessSamplerTable;
				}
				else if (source.m_Type != RHIBindingType::Unknown)
				{
					slot.m_BackendMapping = RHIBindingBackendMapping::RootParameter;
					slot.m_RootParameter = rootParameter++;
					slot.m_BackendBindingType = BackendBindingType(source);
				}
				layout.m_Slots.push_back(std::move(slot));
			}
			layout.m_RootParameterCount = static_cast<uint32_t>(rootParameter);
			outSnapshot.m_BindingLayouts.push_back(std::move(layout));
		}

		outSnapshot.m_Pipelines.reserve(system.m_Pipelines.size());
		for (uint32_t index = 0; index < system.m_Pipelines.size(); ++index)
		{
			const auto& binding = system.m_Pipelines[index];
			RHIPipelineSnapshot pipeline{};
			pipeline.m_Handle = RHIPipelineHandle(index, system.m_PipelineGeneration);
			pipeline.m_Alive = binding.m_PipelineState != nullptr &&
				binding.m_PipelineState->Get() != nullptr;
			if (pipelineCache)
			{
				pipelineCache->GetPipelineUsages(pipeline.m_Handle, pipeline.m_RenderPasses);
			}
			pipeline.m_BackendPipelinePointer = reinterpret_cast<uint64_t>(
				binding.m_PipelineState ? binding.m_PipelineState->Get() : nullptr);
			pipeline.m_BackendRootSignatureId = binding.m_Type == DX12PipelineSystem::PipelineType::Graphics ?
				binding.m_GraphicsDesc.m_BindingLayout.Index() :
				binding.m_ComputeDesc.m_BindingLayout.Index();
			pipeline.m_BackendRootSignaturePointer = reinterpret_cast<uint64_t>(
				binding.m_RootSignature ? binding.m_RootSignature->Get() : nullptr);

			if (binding.m_Type == DX12PipelineSystem::PipelineType::Graphics)
			{
				++outSnapshot.m_Cache.m_RegisteredGraphicsPipelines;
				pipeline.m_Type = RHIPipelineSnapshotType::Graphics;
				const auto& desc = binding.m_GraphicsDesc;
				pipeline.m_BindingLayout = desc.m_BindingLayout;
				pipeline.m_VertexShader = MakeShaderSnapshot(
					desc.m_VertexShader, binding.m_VertexShaderHash, shaderManager);
				pipeline.m_PixelShader = MakeShaderSnapshot(
					desc.m_PixelShader, binding.m_PixelShaderHash, shaderManager);
				pipeline.m_DomainShader = MakeShaderSnapshot(
					desc.m_DomainShader, binding.m_DomainShaderHash, shaderManager);
				pipeline.m_HullShader = MakeShaderSnapshot(
					desc.m_HullShader, binding.m_HullShaderHash, shaderManager);
				pipeline.m_GeometryShader = MakeShaderSnapshot(
					desc.m_GeometryShader, binding.m_GeometryShaderHash, shaderManager);
				const uint32_t attributeCount = std::min(
					desc.m_VertexInput.m_AttributeCount,
					RHIVertexInputLayoutDesc::MaxAttributes);
				pipeline.m_VertexAttributes.reserve(attributeCount);
				for (uint32_t attributeIndex = 0; attributeIndex < attributeCount; ++attributeIndex)
				{
					const auto& attribute = desc.m_VertexInput.m_Attributes[attributeIndex];
					pipeline.m_VertexAttributes.push_back({
						.m_SemanticName = binding.m_SemanticNames[attributeIndex],
						.m_SemanticIndex = attribute.m_SemanticIndex,
						.m_Format = attribute.m_Format,
						.m_InputSlot = attribute.m_InputSlot,
						.m_AlignedByteOffset = attribute.m_AlignedByteOffset,
					});
				}
				const uint32_t vertexBufferCount = std::min(
					desc.m_VertexInput.m_VertexBufferCount,
					RHIVertexInputLayoutDesc::MaxVertexBuffers);
				pipeline.m_VertexBuffers.assign(
					desc.m_VertexInput.m_VertexBuffers.begin(),
					desc.m_VertexInput.m_VertexBuffers.begin() + vertexBufferCount);
				pipeline.m_TopologyType = desc.m_TopologyType;
				pipeline.m_PrimitiveTopology = desc.m_PrimitiveTopology;
				pipeline.m_Rasterizer = desc.m_Rasterizer;
				pipeline.m_DepthStencil = desc.m_DepthStencil;
				pipeline.m_Blend = desc.m_Blend;
				pipeline.m_RenderTargetFormats.assign(
					desc.m_RenderTargetFormats.begin(),
					desc.m_RenderTargetFormats.begin() +
						std::min(desc.m_RenderTargetCount, RHIGraphicsPipelineDesc::MaxRenderTargets));
				pipeline.m_DepthStencilFormat = desc.m_DepthStencilFormat;
				pipeline.m_SampleCount = desc.m_SampleCount;
				pipeline.m_SampleQuality = desc.m_SampleQuality;
			}
			else
			{
				++outSnapshot.m_Cache.m_RegisteredComputePipelines;
				pipeline.m_Type = RHIPipelineSnapshotType::Compute;
				pipeline.m_BindingLayout = binding.m_ComputeDesc.m_BindingLayout;
				pipeline.m_ComputeShader = MakeShaderSnapshot(
					binding.m_ComputeDesc.m_ComputeShader,
					binding.m_ComputeShaderHash,
					shaderManager);
			}

			outSnapshot.m_Pipelines.push_back(std::move(pipeline));
		}

		if (system.m_PSOCache)
		{
			std::shared_lock cacheLock(system.m_PSOCache->m_Mutex);
			outSnapshot.m_Cache.m_BackendCacheRevision = system.m_PSOCache->GetRevision();
			outSnapshot.m_Cache.m_BackendRHIGraphicsPSOs =
				static_cast<uint32_t>(system.m_PSOCache->m_RHIGraphicsPSOMap.size());
			outSnapshot.m_Cache.m_BackendComputePSOs =
				static_cast<uint32_t>(system.m_PSOCache->m_ComputePSOMap.size());
			outSnapshot.m_Cache.m_BackendLegacyGraphicsPSOs =
				static_cast<uint32_t>(system.m_PSOCache->m_GraphicsPSOMap.size());
		}
		if (system.m_RootSignatureCache)
		{
			std::shared_lock rootLock(system.m_RootSignatureCache->m_Mutex);
			outSnapshot.m_Cache.m_BackendRootSignatures =
				static_cast<uint32_t>(system.m_RootSignatureCache->m_RootSignatures.size());
		}
	}
}
