#pragma once
#include "Core/Hash/FNV1a.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHIBindingLayout.h"

namespace gglab
{
	class DX12PipelineSystem;
	class ShaderManager;
	struct RHIPipelineSystemSnapshot;
	struct RootSignatureKey
	{
		uint64_t m_LowBits = 0;
		uint64_t m_HighBits = 0;

		bool operator==(const RootSignatureKey&) const noexcept = default;
		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_LowBits, m_HighBits);
		}
	};
	using RootSignatureKeyHash = KeyHash<RootSignatureKey>;

	struct RootSignatureHandle
	{
		RootSignatureID m_Id{};
		ID3D12RootSignature* m_RootSignature = nullptr;
		bool IsValid() const noexcept { return m_Id.IsValid() && m_RootSignature != nullptr; }
	};

	class DX12Device;
	class DX12RootSignature;
	class DX12RootSignatureCache
	{
	public:
		explicit DX12RootSignatureCache(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12RootSignatureCache);
		~DX12RootSignatureCache() = default;

		RootSignatureHandle GetOrCreate(const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept;
		RootSignatureHandle GetOrCreate(const RHIBindingLayoutDesc& desc) noexcept;
		DX12RootSignature* GetDX12RootSignature(RootSignatureID id) const noexcept;

		void Clear() noexcept;

	private:
		static RootSignatureKey MakeKey(const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		std::unordered_map<RootSignatureKey, RootSignatureID, RootSignatureKeyHash> m_RootSignatureMap;
		std::vector<std::unique_ptr<DX12RootSignature>> m_RootSignatures;
		mutable std::shared_mutex m_Mutex;

		friend void BuildDX12PipelineSystemSnapshot(
			const DX12PipelineSystem& system,
			const ShaderManager* shaderManager,
			RHIPipelineSystemSnapshot& outSnapshot) noexcept;
	};
}
