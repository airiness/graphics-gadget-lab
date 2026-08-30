#pragma once
#include "Graphics/Shader/ShaderTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	enum class SpirVExecutionModel : uint8_t
	{
		Unknown,
		Vertex,
		TessellationControl,
		TessellationEvaluation,
		Geometry,
		Fragment,
		Compute,
		Task,
		Mesh,
	};

	struct SpirVDescriptorBindingReflection
	{
		uint32_t m_TargetId = 0;
		uint32_t m_DescriptorSet = 0;
		uint32_t m_Binding = 0;
	};

	struct SpirVEntryPointReflection
	{
		uint32_t m_Id = 0;
		SpirVExecutionModel m_ExecutionModel = SpirVExecutionModel::Unknown;
		std::string m_Name;
		std::vector<uint32_t> m_InputLocations;
		std::vector<uint32_t> m_OutputLocations;
		uint32_t m_InputBuiltInCount = 0;
		uint32_t m_OutputBuiltInCount = 0;
	};

	struct SpirVStructMemberLayoutReflection
	{
		std::string m_Name;
		uint32_t m_Offset = 0;
	};

	struct SpirVStructLayoutReflection
	{
		uint32_t m_TypeId = 0;
		std::string m_Name;
		std::vector<SpirVStructMemberLayoutReflection> m_Members;
		std::optional<uint32_t> m_Size;
		std::optional<uint32_t> m_ArrayStride;
	};

	struct SpirVDecorationReflection
	{
		std::vector<SpirVEntryPointReflection> m_EntryPoints;
		std::vector<SpirVDescriptorBindingReflection> m_DescriptorBindings;
		std::vector<SpirVStructLayoutReflection> m_StructLayouts;
		std::string m_Error;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Error.empty() && !m_EntryPoints.empty();
		}
		[[nodiscard]] const SpirVEntryPointReflection* FindEntryPoint(
			std::string_view name) const noexcept;
		[[nodiscard]] const SpirVStructLayoutReflection* FindStructLayout(
			std::string_view name) const noexcept;
	};

	[[nodiscard]] bool ReadSpirVDecorations(
		const ShaderBinary& binary, SpirVDecorationReflection& outReflection) noexcept;
}
