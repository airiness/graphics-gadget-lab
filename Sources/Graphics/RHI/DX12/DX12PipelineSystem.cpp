#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/Cache/DX12PSOCache.h"
#include "Graphics/RHI/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/RHI/DX12/Cache/PSOCreator.h"
#include "Graphics/RHI/DX12/DX12PipelineState.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/Utility/DX12PipelineDescUtils.h"

namespace gglab
{
	namespace
	{
		D3D12_SHADER_BYTECODE ToDX12Bytecode(const RHIShaderBytecode& bytecode) noexcept
		{
			return {
				.pShaderBytecode = bytecode.m_Data,
				.BytecodeLength = bytecode.m_SizeInBytes,
			};
		}

		ShaderHash128 ToDX12ShaderHash(const RHIShaderBytecode& bytecode) noexcept
		{
			return {
				.m_LowBits = bytecode.m_HashLow,
				.m_HighBits = bytecode.m_HashHigh,
			};
		}
	}

	DX12PipelineSystem::DX12PipelineSystem(DX12Device* device) noexcept :
		m_Device(device),
		m_RootSignatureCache(std::make_unique<DX12RootSignatureCache>(device)),
		m_PSOCache(std::make_unique<DX12PSOCache>(device, std::make_unique<StreamPSOCreator>()))
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
	}

	DX12PipelineSystem::~DX12PipelineSystem() = default;

	RHIBindingLayoutHandle DX12PipelineSystem::CreateBindingLayout(
		const RHIBindingLayoutDesc& desc) noexcept
	{
		const RootSignatureHandle rootSignature = m_RootSignatureCache->GetOrCreate(desc);
		if (!rootSignature.IsValid())
		{
			return {};
		}
		return RHIBindingLayoutHandle(
			static_cast<RHIBindingLayoutHandle::IndexType>(rootSignature.m_Id.Value()),
			m_BindingLayoutGeneration);
	}

	RHIPipelineHandle DX12PipelineSystem::CreateGraphicsPipeline(
		const RHIGraphicsPipelineCreateInfo& createInfo) noexcept
	{
		DX12RootSignature* rootSignature = ResolveBindingLayout(createInfo.m_Desc.m_BindingLayout);
		if (!rootSignature || !createInfo.m_VertexShader.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12PipelineSystem::CreateGraphicsPipeline received invalid inputs.");
			return {};
		}

		const RootSignatureHandle nativeRoot{
			.m_Id = RootSignatureID{ createInfo.m_Desc.m_BindingLayout.Index() },
			.m_RootSignature = rootSignature->Get(),
		};
		DX12GraphicsPipelineShaderInputs shaders{};
		shaders.m_VertexShader = ToDX12Bytecode(createInfo.m_VertexShader);
		shaders.m_PixelShader = ToDX12Bytecode(createInfo.m_PixelShader);
		shaders.m_DomainShader = ToDX12Bytecode(createInfo.m_DomainShader);
		shaders.m_HullShader = ToDX12Bytecode(createInfo.m_HullShader);
		shaders.m_GeometryShader = ToDX12Bytecode(createInfo.m_GeometryShader);
		shaders.m_VertexShaderHash = ToDX12ShaderHash(createInfo.m_VertexShader);
		shaders.m_PixelShaderHash = ToDX12ShaderHash(createInfo.m_PixelShader);
		shaders.m_DomainShaderHash = ToDX12ShaderHash(createInfo.m_DomainShader);
		shaders.m_HullShaderHash = ToDX12ShaderHash(createInfo.m_HullShader);
		shaders.m_GeometryShaderHash = ToDX12ShaderHash(createInfo.m_GeometryShader);

		DX12PipelineState* pipelineState = m_PSOCache->GetOrCreate(
			createInfo.m_Desc,
			nativeRoot,
			shaders);
		return RegisterPipeline({ pipelineState, rootSignature, PipelineType::Graphics });
	}

	RHIPipelineHandle DX12PipelineSystem::CreateComputePipeline(
		const RHIComputePipelineCreateInfo& createInfo) noexcept
	{
		DX12RootSignature* rootSignature = ResolveBindingLayout(createInfo.m_Desc.m_BindingLayout);
		if (!rootSignature || !createInfo.m_ComputeShader.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12PipelineSystem::CreateComputePipeline received invalid inputs.");
			return {};
		}

		ComputePipelineDesc desc{};
		desc.m_RootSignatureId = RootSignatureID{ createInfo.m_Desc.m_BindingLayout.Index() };
		desc.m_RootSignature = rootSignature->Get();
		desc.m_ComputeShader = ToDX12Bytecode(createInfo.m_ComputeShader);
		const ComputePSOKey key = desc.MakeKey(ToDX12ShaderHash(createInfo.m_ComputeShader));
		DX12PipelineState* pipelineState = m_PSOCache->GetOrCreate(key, desc);
		return RegisterPipeline({ pipelineState, rootSignature, PipelineType::Compute });
	}

	bool DX12PipelineSystem::IsAlive(RHIBindingLayoutHandle layout) const noexcept
	{
		return ResolveBindingLayout(layout) != nullptr;
	}

	bool DX12PipelineSystem::IsAlive(RHIPipelineHandle pipeline) const noexcept
	{
		if (!pipeline.IsValid() || pipeline.Generation() != m_PipelineGeneration)
		{
			return false;
		}
		std::shared_lock lock(m_Mutex);
		return pipeline.Index() < m_Pipelines.size() &&
			m_Pipelines[pipeline.Index()].m_PipelineState != nullptr;
	}

	void DX12PipelineSystem::Clear() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_Pipelines.clear();
		m_PSOCache->Clear();
		++m_PipelineGeneration;
		if (m_PipelineGeneration == RHIPipelineHandle::InvalidGeneration)
		{
			++m_PipelineGeneration;
		}
		m_Revision.fetch_add(1, std::memory_order_relaxed);
	}

	bool DX12PipelineSystem::ResolveGraphicsPipeline(
		RHIPipelineHandle pipeline,
		DX12PipelineState*& outPipelineState,
		DX12RootSignature*& outRootSignature) const noexcept
	{
		return ResolvePipeline(
			pipeline, PipelineType::Graphics, outPipelineState, outRootSignature);
	}

	bool DX12PipelineSystem::ResolveComputePipeline(
		RHIPipelineHandle pipeline,
		DX12PipelineState*& outPipelineState,
		DX12RootSignature*& outRootSignature) const noexcept
	{
		return ResolvePipeline(
			pipeline, PipelineType::Compute, outPipelineState, outRootSignature);
	}

	DX12RootSignature* DX12PipelineSystem::ResolveBindingLayout(
		RHIBindingLayoutHandle layout) const noexcept
	{
		if (!layout.IsValid() || layout.Generation() != m_BindingLayoutGeneration)
		{
			return nullptr;
		}
		return m_RootSignatureCache->GetDX12RootSignature(
			RootSignatureID{ layout.Index() });
	}

	RHIPipelineHandle DX12PipelineSystem::RegisterPipeline(
		const PipelineBinding& binding) noexcept
	{
		if (!binding.m_PipelineState || !binding.m_RootSignature)
		{
			return {};
		}
		std::unique_lock lock(m_Mutex);
		for (uint32_t index = 0; index < m_Pipelines.size(); ++index)
		{
			const auto& existing = m_Pipelines[index];
			if (existing.m_PipelineState == binding.m_PipelineState &&
				existing.m_RootSignature == binding.m_RootSignature &&
				existing.m_Type == binding.m_Type)
			{
				return RHIPipelineHandle(index, m_PipelineGeneration);
			}
		}
		const auto index = static_cast<RHIPipelineHandle::IndexType>(m_Pipelines.size());
		m_Pipelines.push_back(binding);
		return RHIPipelineHandle(index, m_PipelineGeneration);
	}

	bool DX12PipelineSystem::ResolvePipeline(
		RHIPipelineHandle pipeline,
		PipelineType expectedType,
		DX12PipelineState*& outPipelineState,
		DX12RootSignature*& outRootSignature) const noexcept
	{
		outPipelineState = nullptr;
		outRootSignature = nullptr;
		if (!pipeline.IsValid() || pipeline.Generation() != m_PipelineGeneration)
		{
			return false;
		}
		std::shared_lock lock(m_Mutex);
		if (pipeline.Index() >= m_Pipelines.size())
		{
			return false;
		}
		const auto& binding = m_Pipelines[pipeline.Index()];
		if (binding.m_Type != expectedType)
		{
			return false;
		}
		outPipelineState = binding.m_PipelineState;
		outRootSignature = binding.m_RootSignature;
		return outPipelineState != nullptr && outRootSignature != nullptr;
	}
}
