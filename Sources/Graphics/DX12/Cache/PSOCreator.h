#pragma once
#include "Graphics/DX12/Cache/PipelineDesc.h"
#include "Graphics/DX12/DX12PipelineState.h"
namespace gglab
{
	class DX12Device;
	class IPSOCreator
	{
	public:
		virtual ~IPSOCreator() = default;

		virtual std::unique_ptr<DX12PipelineState> CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc) = 0;
		virtual std::unique_ptr<DX12PipelineState> CreateComputePSO(DX12Device* dx12Device, const ComputePipelineDesc& desc) = 0;
	};

	class StreamPSOCreator final : public IPSOCreator
	{
	private:
		class StreamWriter
		{
		public:
			explicit StreamWriter(void* storage, size_t capacityBytes) noexcept :
				m_Begin(AlignUp(reinterpret_cast<std::byte*>(storage), alignof(void*))),
				m_Current(m_Begin),
				m_End(reinterpret_cast<std::byte*>(storage) + capacityBytes)
			{
			}

			template<typename SubObjType>
			void Add(const SubObjType& subObj) noexcept
			{
				static_assert(std::is_trivially_copyable_v<SubObjType>, "SubObj must be trivially copyable.");
				m_Current = AlignUp(m_Current, alignof(void*));
				GGLAB_ASSERT_MSG(m_Current + sizeof(SubObjType) <= m_End, "Stream memory is overflow.");
				std::memcpy(m_Current, std::addressof(subObj), sizeof(SubObjType)); // this place must be std::addressof, operator& is overloaded, can not get right address.
				m_Current += sizeof(SubObjType);
			}

			template<typename SubObjType>
			void AddIf(bool condition, const SubObjType& subObj) noexcept
			{
				if (condition) { Add(subObj); }
			}

			D3D12_PIPELINE_STATE_STREAM_DESC GetDesc() const noexcept
			{
				D3D12_PIPELINE_STATE_STREAM_DESC desc{};
				desc.pPipelineStateSubobjectStream = m_Begin;
				desc.SizeInBytes = static_cast<UINT>(m_Current - m_Begin);
				return desc;
			}

		private:
			static std::byte* AlignUp(std::byte* ptr, size_t alignment) noexcept
			{
				auto value = reinterpret_cast<uintptr_t>(ptr);
				value = (value + (alignment - 1)) & ~(uintptr_t(alignment - 1));
				return reinterpret_cast<std::byte*>(value);
			}

		private:
			std::byte* m_Begin = nullptr;
			std::byte* m_Current = nullptr;
			std::byte* m_End = nullptr;
		};

	public:
		std::unique_ptr<DX12PipelineState> CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc) override;
		std::unique_ptr<DX12PipelineState> CreateComputePSO(DX12Device* dx12Device, const ComputePipelineDesc& desc) override;

	private:
		static D3D12_PIPELINE_STATE_STREAM_DESC BuildGraphicsStream(const GraphicsPipelineDesc& desc, StreamWriter& writer) noexcept;
		static D3D12_PIPELINE_STATE_STREAM_DESC BuildComputeStream(const ComputePipelineDesc& desc, StreamWriter& writer) noexcept;
	};

	class LibraryPSOCreator final : public IPSOCreator
	{
	public:
		std::unique_ptr<DX12PipelineState> CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc) override;
		std::unique_ptr<DX12PipelineState> CreateComputePSO(DX12Device* dx12Device, const ComputePipelineDesc& desc) override;

	private:
		ID3D12PipelineLibrary* m_PipelineLibrary = nullptr;
	};
}