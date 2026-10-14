#pragma once
#include <cstdint>
#include <span>

namespace gglab
{
	enum class GpuBufferScalar : uint8_t { Uint8, Uint32, Float16, Float32 };
	struct GpuBufferFieldLayout
	{
		const char* m_Name;
		uint32_t m_Offset;
		GpuBufferScalar m_Scalar;
		uint32_t m_Count = 1;
	};
	struct GpuBufferLayout
	{
		uint32_t m_Stride = 0;
		std::span<const GpuBufferFieldLayout> m_Fields;
	};
}
