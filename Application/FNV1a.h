#pragma once
#include <cstdint>
#include <type_traits>
#include <tuple>
#include <utility>

namespace gglab
{
	struct FNV1a64 final
	{
		static constexpr uint64_t OffsetBasis = 0xcbf29ce484222325ull;
		static constexpr uint64_t Prime = 0x00000100000001B3ull;

		static inline void Mix(uint64_t& hash, uint64_t value) noexcept
		{
			hash ^= value;
			hash ^= Prime;
		}

		template<typename T>
		static constexpr uint64_t ToU64(const T& value) noexcept
		{
			if constexpr (std::is_enum_v<T>)
			{
				using U = std::underlying_type_t<T>;
				return static_cast<uint64_t>(static_cast<U>(value));
			}
			else
			{
				return static_cast<uint64_t>(value);
			}
		}

		template<typename Tuple>
		static size_t HashTuple(const Tuple& tuple) noexcept
		{
			uint64_t hash = OffsetBasis;
			std::apply([&hash](auto const&... values)
				{
					(Mix(hash, ToU64(values)), ...);
				}, tuple);
			return static_cast<size_t>(hash);
		}
	};

	template<typename Key>
	struct KeyHash
	{
		size_t operator()(const Key& key) const noexcept
		{
			return FNV1a64::HashTuple(key.AsTuple());
		}
	};
}