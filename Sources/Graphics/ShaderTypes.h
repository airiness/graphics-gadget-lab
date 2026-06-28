#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <tuple>

namespace gglab
{
	enum class ShaderBinaryFormat : uint8_t
	{
		Unknown,
		Dxil,
		SpirV,
	};

	struct ShaderHash128
	{
		uint64_t m_LowBits = 0;
		uint64_t m_HighBits = 0;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(m_LowBits, m_HighBits);
		}
		constexpr bool operator==(const ShaderHash128&) const noexcept = default;
	};

	struct ShaderBytecode
	{
		const void* m_Data = nullptr;
		size_t m_SizeInBytes = 0;
		ShaderBinaryFormat m_Format = ShaderBinaryFormat::Unknown;
		ShaderHash128 m_Hash{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Data != nullptr && m_SizeInBytes > 0;
		}
	};

	class ShaderBinary
	{
	public:
		ShaderBinary() = default;
		explicit ShaderBinary(size_t sizeInBytes) : m_Data(sizeInBytes) {}

		[[nodiscard]] void* Data() noexcept { return m_Data.data(); }
		[[nodiscard]] const void* Data() const noexcept { return m_Data.data(); }
		[[nodiscard]] size_t SizeInBytes() const noexcept { return m_Data.size(); }
		[[nodiscard]] bool IsValid() const noexcept { return !m_Data.empty(); }
		void Reset() noexcept { m_Data.clear(); }

	private:
		std::vector<std::byte> m_Data;
	};
}
