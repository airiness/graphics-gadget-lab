#pragma once
#include "Graphics/RHI/RHIPipelineSystem.h"

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace gglab
{
	class DX12Device;
	class DX12PipelineState;
	class DX12PSOCache;
	class DX12RootSignature;
	class DX12RootSignatureCache;

	class DX12PipelineSystem final : public RHIPipelineSystem
	{
	public:
		explicit DX12PipelineSystem(DX12Device* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12PipelineSystem);
		~DX12PipelineSystem() override;

		RHIBindingLayoutHandle CreateBindingLayout(
			const RHIBindingLayoutDesc& desc) noexcept override;
		RHIPipelineHandle CreateGraphicsPipeline(
			const RHIGraphicsPipelineCreateInfo& createInfo) noexcept override;
		RHIPipelineHandle CreateComputePipeline(
			const RHIComputePipelineCreateInfo& createInfo) noexcept override;
		bool IsAlive(RHIBindingLayoutHandle layout) const noexcept override;
		bool IsAlive(RHIPipelineHandle pipeline) const noexcept override;
		uint64_t GetRevision() const noexcept override
		{
			return m_Revision.load(std::memory_order_relaxed);
		}
		void Clear() noexcept override;

		bool ResolveGraphicsPipeline(
			RHIPipelineHandle pipeline,
			DX12PipelineState*& outPipelineState,
			DX12RootSignature*& outRootSignature) const noexcept;
		bool ResolveComputePipeline(
			RHIPipelineHandle pipeline,
			DX12PipelineState*& outPipelineState,
			DX12RootSignature*& outRootSignature) const noexcept;

	private:
		enum class PipelineType : uint8_t
		{
			Graphics,
			Compute,
		};

		struct PipelineBinding
		{
			DX12PipelineState* m_PipelineState = nullptr;
			DX12RootSignature* m_RootSignature = nullptr;
			PipelineType m_Type = PipelineType::Graphics;
		};

		DX12RootSignature* ResolveBindingLayout(RHIBindingLayoutHandle layout) const noexcept;
		RHIPipelineHandle RegisterPipeline(const PipelineBinding& binding) noexcept;
		bool ResolvePipeline(RHIPipelineHandle pipeline,
			PipelineType expectedType,
			DX12PipelineState*& outPipelineState,
			DX12RootSignature*& outRootSignature) const noexcept;

		DX12Device* m_Device = nullptr;
		std::unique_ptr<DX12RootSignatureCache> m_RootSignatureCache;
		std::unique_ptr<DX12PSOCache> m_PSOCache;
		std::vector<PipelineBinding> m_Pipelines;
		mutable std::shared_mutex m_Mutex;
		std::atomic_uint64_t m_Revision = 1;
		RHIBindingLayoutHandle::GenerationType m_BindingLayoutGeneration = 1;
		RHIPipelineHandle::GenerationType m_PipelineGeneration = 1;
	};
}
