#pragma once
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	struct ExternalResourceIndex
	{
		using Rep = ResourceIndex::ValueType;
		enum class Type : Rep
		{
			Texture = 0,
			Buffer = 1,
		};

		static constexpr Rep ExternalBit = (Rep{ 1 } << (std::numeric_limits<Rep>::digits - 1));
		static constexpr Rep TypeBit = (ExternalBit >> 1);

		static constexpr Rep ExternalMask = ExternalBit;
		static constexpr Rep PayloadMask = ~ExternalMask;
		static constexpr Rep TypeMask = TypeBit;
		static constexpr Rep IdMask = PayloadMask & ~TypeMask;

		static constexpr ResourceIndex Base() noexcept { return ResourceIndex(ExternalBit); }

		[[nodiscard]] static constexpr bool IsExternal(ResourceIndex index) noexcept
		{
			return index.IsValid() && ((index.Value() & ExternalMask) != 0);
		}

		[[nodiscard]] static constexpr Rep Payload(ResourceIndex index) noexcept
		{
			GGLAB_ASSERT_MSG(IsExternal(index), "Payload called on non-external ResourceIndex.");
			return (index.Value() & PayloadMask);
		}

		[[nodiscard]] static constexpr Type GetType(ResourceIndex index) noexcept
		{
			GGLAB_ASSERT_MSG(IsExternal(index), "GetType called on non-external ResourceIndex.");
			return ((Payload(index) & TypeMask) != 0) ? Type::Buffer : Type::Texture;
		}

		[[nodiscard]] static constexpr Rep GetId(ResourceIndex index) noexcept
		{
			GGLAB_ASSERT_MSG(IsExternal(index), "GetId called on non-external ResourceIndex.");
			return (Payload(index) & IdMask);
		}

		[[nodiscard]] static constexpr ResourceIndex MakeIndex(Type type, Rep id) noexcept
		{
			GGLAB_ASSERT_MSG((id & ~IdMask) == 0, "External id overflowed.");

			const Rep typeBits = (type == Type::Buffer) ? TypeBit : 0;
			const Rep payload = typeBits | (id & IdMask);
			const Rep value = ExternalBit | payload;

			GGLAB_ASSERT_MSG(value != ResourceIndex::InvalidValue, "External ResourceIndex overflowed to InvalidValue.");
			return ResourceIndex(value);
		}

		[[nodiscard]] static constexpr ResourceIndex MakeTexture(Rep id) noexcept
		{
			return MakeIndex(Type::Texture, id);
		}

		[[nodiscard]] static constexpr ResourceIndex MakeBuffer(Rep id) noexcept
		{
			return MakeIndex(Type::Buffer, id);
		}
	};

	class DX12ViewCache;
	class DX12Texture;
	class DX12Buffer;
	class RGExternalResourceRegistry
	{
	public:
		explicit RGExternalResourceRegistry(DX12ViewCache* viewCache) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(RGExternalResourceRegistry);
		~RGExternalResourceRegistry();

		ResourceIndex GetOrCreate(const DX12Texture* texture) noexcept;
		ResourceIndex GetOrCreate(const DX12Buffer* buffer) noexcept;
		std::optional<ResourceIndex> TryGet(const DX12Texture* texture) const noexcept;
		std::optional<ResourceIndex> TryGet(const DX12Buffer* buffer) const noexcept;

		void Forget(const DX12Texture* texture, bool freeViewsImmediately,
			const DX12FencePoint* fencePointOpt = nullptr) noexcept;
		void Forget(const DX12Buffer* buffer) noexcept;

		void Clear(bool freeViewsImmediately = true) noexcept;

	private:
		ResourceIndex GetOrCreateImpl(const ID3D12Resource* resource, ExternalResourceIndex::Type type) noexcept;
		std::optional<ResourceIndex> TryGetImpl(const ID3D12Resource* resource) const noexcept;

		void ForgetTexture(const ID3D12Resource* resource, bool freeViewsImmediately,
			const DX12FencePoint* fencePointOpt = nullptr) noexcept;
		void ForgetBuffer(const ID3D12Resource* resource) noexcept;

	private:
		DX12ViewCache* m_ViewCache = nullptr;
		std::unordered_map<const ID3D12Resource*, ResourceIndex> m_Table;
		ExternalResourceIndex::Rep m_NextId = 1;

		mutable std::shared_mutex m_Mutex;
	};
}