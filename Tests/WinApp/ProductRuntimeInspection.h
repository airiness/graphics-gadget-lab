#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "RuntimePaths.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gglab
{
	struct ProductInspectionResult
	{
		bool m_Completed = false;
		bool m_Valid = false;
		bool m_ArtifactOnly = false;
		uint32_t m_ExitCode = 0;
		RHIBackendType m_Backend = RHIBackendType::DX12;
		RuntimePaths m_Paths;
		std::string m_Output;
	};

	ProductInspectionResult InspectProductRuntime(const std::filesystem::path& application,
		const std::wstring& options = {}) noexcept;
}
