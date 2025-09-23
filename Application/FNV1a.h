#pragma once
#include <cstdint>
#include <type_traits>
#include <tuple>
#include <string_view>
#include <concepts>
#include <bit>
#include <memory>

namespace gglab
{
	template<class T>
	concept Fnv1aScalar =
		std::integral<std::remove_cvref_t<T>> ||
		std::is_enum_v<std::remove_cvref_t<T>> ||
		std::floating_point<std::remove_cvref_t<T>> ||
		std::is_pointer_v<std::remove_cvref_t<T>> ||
		std::same_as<std::remove_cvref_t<T>, std::string_view>;

	struct FNV1a64 final
	{
		static constexpr uint64_t OffsetBasis = 0xcbf29ce484222325ull;
		static constexpr uint64_t Prime = 0x00000100000001B3ull;

		static constexpr void MixBytes(uint64_t& hash, const void* data, size_t length) noexcept
		{
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < length; ++i)
			{
				hash ^= static_cast<uint64_t>(bytes[i]);
				hash *= Prime;
			}
		}

		template<typename T>
		static constexpr void MixValue(uint64_t& hash, const T& value) noexcept
		{
			using U = std::remove_cvref_t<T>;
			if constexpr (std::is_enum_v<U>)
			{
				using Under = std::underlying_type_t<U>;
				Under underlyingValue = static_cast<Under>(value);
				MixBytes(hash, std::addressof(underlyingValue), sizeof(underlyingValue));
			}
			else if constexpr (std::is_integral_v<U>)
			{
				// bool is also integral, std::make_unsigned_t<bool> maybe error
				using UnsignedU = std::conditional_t<std::is_same_v<U, bool>, uint8_t, std::make_unsigned_t<U>>;
				UnsignedU unsignedValue = static_cast<UnsignedU>(value);
				MixBytes(hash, std::addressof(unsignedValue), sizeof(unsignedValue));
			}
			else if constexpr (std::same_as<U, const char*> || std::same_as<U, char*>)
			{
				const char* str = reinterpret_cast<const char*>(value);
				const size_t length = std::char_traits<char>::length(str);
				MixBytes(hash, str, length);
			}
			else if constexpr (std::same_as<U, std::string_view>)
			{
				MixBytes(hash, value.data(), value.size());
			}
			else if constexpr (std::is_pointer_v<U>)
			{
				auto ptrValue = reinterpret_cast<uintptr_t>(value);
				MixBytes(hash, std::addressof(ptrValue), sizeof(ptrValue));
			}
			else if constexpr (std::is_floating_point_v<U>)
			{
				static_assert(sizeof(U) <= sizeof(uint64_t), "Floating point type is too large to hash.");
				if constexpr (sizeof(U) == sizeof(float))
				{
					constexpr auto canonicalZero = [](float v) noexcept { return v == 0.0f ? 0.0f : v; };
					float floatValue = canonicalZero(static_cast<float>(value));
					uint32_t intValue = std::bit_cast<uint32_t>(floatValue);
					MixBytes(hash, std::addressof(intValue), sizeof(intValue));
				}
				else if constexpr (sizeof(U) == sizeof(double))
				{
					constexpr auto canonicalZero = [](double v) noexcept { return v == 0.0 ? 0.0 : v; };
					double doubleValue = canonicalZero(static_cast<double>(value));
					uint64_t intValue = std::bit_cast<uint64_t>(doubleValue);
					MixBytes(hash, std::addressof(intValue), sizeof(intValue));
				}
			}
			else
			{
				static_assert(!sizeof(U), "Type is not supported for hashing.");
			}
		}

		template<typename... Ts>
		static constexpr void MixValue(uint64_t& hash, const std::tuple<Ts...> values) noexcept
		{
			std::apply([&hash](auto const&... vals)
				{
					(MixValue(hash, vals), ...);
				}, values);
		}

		template<Fnv1aScalar T>
		[[nodiscard]] static constexpr size_t HashValue(const T& value) noexcept
		{
			uint64_t hash = OffsetBasis;
			MixValue(hash, value);
			return static_cast<size_t>(hash);
		}

		template<typename Tuple>
		[[nodiscard]] static constexpr size_t HashTuple(const Tuple& tuple) noexcept
		{
			uint64_t hash = OffsetBasis;
			std::apply([&hash](auto const&... values)
				{
					(MixValue(hash, values), ...);
				}, tuple);
			return static_cast<size_t>(hash);
		}

		[[nodiscard]] static constexpr uint64_t HashBytes64(const void* data, size_t length, uint64_t seed = OffsetBasis) noexcept
		{
			uint64_t hash = seed;
			MixBytes(hash, data, length);
			return hash;
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