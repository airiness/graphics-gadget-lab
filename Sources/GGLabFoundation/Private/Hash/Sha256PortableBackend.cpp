#include "Hash/Sha256Backend.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>

namespace gglab::foundation::detail
{
	namespace
	{
		constexpr std::array<std::uint32_t, 64> RoundConstants{
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
			0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
			0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
			0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
			0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
			0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
			0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
			0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
			0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
		};

		[[nodiscard]] std::uint32_t LoadBigEndian(const std::byte* bytes) noexcept
		{
			return (std::to_integer<std::uint32_t>(bytes[0]) << 24) |
				(std::to_integer<std::uint32_t>(bytes[1]) << 16) |
				(std::to_integer<std::uint32_t>(bytes[2]) << 8) |
				std::to_integer<std::uint32_t>(bytes[3]);
		}

		class PortableSha256Backend final : public Sha256Backend
		{
		public:
			PortableSha256Backend() noexcept :
				m_State{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu,
					0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u }
			{
			}

			[[nodiscard]] bool IsValid() const noexcept override
			{
				return !m_Finished && !m_Failed;
			}

			bool AddBytes(std::span<const std::byte> bytes) noexcept override
			{
				constexpr std::uint64_t MaximumMessageBytes =
					std::numeric_limits<std::uint64_t>::max() / 8;
				if (!IsValid() || bytes.size() > MaximumMessageBytes - m_TotalBytes)
				{
					m_Failed = true;
					return false;
				}

				m_TotalBytes += static_cast<std::uint64_t>(bytes.size());
				while (!bytes.empty())
				{
					const std::size_t copySize =
						std::min(bytes.size(), m_Buffer.size() - m_BufferSize);
					std::copy_n(bytes.begin(), copySize, m_Buffer.begin() + m_BufferSize);
					m_BufferSize += copySize;
					bytes = bytes.subspan(copySize);
					if (m_BufferSize == m_Buffer.size())
					{
						TransformBlock(m_Buffer);
						m_BufferSize = 0;
					}
				}
				return true;
			}

			[[nodiscard]] Sha256Digest Finish() noexcept override
			{
				if (!IsValid())
				{
					m_Failed = true;
					return {};
				}

				const std::uint64_t bitLength = m_TotalBytes * 8;
				m_Buffer[m_BufferSize++] = std::byte{ 0x80 };
				if (m_BufferSize > 56)
				{
					std::fill(m_Buffer.begin() + m_BufferSize, m_Buffer.end(), std::byte{});
					TransformBlock(m_Buffer);
					m_BufferSize = 0;
				}
				std::fill(m_Buffer.begin() + m_BufferSize, m_Buffer.begin() + 56, std::byte{});
				for (std::size_t index = 0; index < 8; ++index)
				{
					m_Buffer[56 + index] =
						static_cast<std::byte>(bitLength >> ((7 - index) * 8));
				}
				TransformBlock(m_Buffer);

				Sha256Digest result{};
				for (std::size_t index = 0; index < m_State.size(); ++index)
				{
					const std::uint32_t value = m_State[index];
					result.m_Value[index * 4] = static_cast<std::byte>(value >> 24);
					result.m_Value[index * 4 + 1] = static_cast<std::byte>(value >> 16);
					result.m_Value[index * 4 + 2] = static_cast<std::byte>(value >> 8);
					result.m_Value[index * 4 + 3] = static_cast<std::byte>(value);
				}
				m_Finished = true;
				return result;
			}

		private:
			void TransformBlock(std::span<const std::byte, 64> block) noexcept
			{
				std::array<std::uint32_t, 64> schedule{};
				for (std::size_t index = 0; index < 16; ++index)
				{
					schedule[index] = LoadBigEndian(block.data() + index * 4);
				}
				for (std::size_t index = 16; index < schedule.size(); ++index)
				{
					const std::uint32_t s0 = std::rotr(schedule[index - 15], 7) ^
						std::rotr(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3);
					const std::uint32_t s1 = std::rotr(schedule[index - 2], 17) ^
						std::rotr(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10);
					schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
				}

				std::uint32_t a = m_State[0];
				std::uint32_t b = m_State[1];
				std::uint32_t c = m_State[2];
				std::uint32_t d = m_State[3];
				std::uint32_t e = m_State[4];
				std::uint32_t f = m_State[5];
				std::uint32_t g = m_State[6];
				std::uint32_t h = m_State[7];

				for (std::size_t index = 0; index < schedule.size(); ++index)
				{
					const std::uint32_t sum1 =
						std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
					const std::uint32_t choose = (e & f) ^ (~e & g);
					const std::uint32_t temporary1 =
						h + sum1 + choose + RoundConstants[index] + schedule[index];
					const std::uint32_t sum0 =
						std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
					const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
					const std::uint32_t temporary2 = sum0 + majority;

					h = g;
					g = f;
					f = e;
					e = d + temporary1;
					d = c;
					c = b;
					b = a;
					a = temporary1 + temporary2;
				}

				m_State[0] += a;
				m_State[1] += b;
				m_State[2] += c;
				m_State[3] += d;
				m_State[4] += e;
				m_State[5] += f;
				m_State[6] += g;
				m_State[7] += h;
			}

			std::array<std::uint32_t, 8> m_State{};
			std::array<std::byte, 64> m_Buffer{};
			std::uint64_t m_TotalBytes = 0;
			std::size_t m_BufferSize = 0;
			bool m_Finished = false;
			bool m_Failed = false;
		};
	}

	std::unique_ptr<Sha256Backend> CreatePortableSha256Backend() noexcept
	{
		return std::unique_ptr<Sha256Backend>(new (std::nothrow) PortableSha256Backend);
	}
}
