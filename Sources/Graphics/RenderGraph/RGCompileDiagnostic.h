#pragma once
#include "Graphics/RenderGraph/RGResourceHandle.h"

#include <cstdint>
#include <string>

namespace gglab
{
	enum class RGCompileDiagnosticSeverity : uint8_t
	{
		Info,
		Warning,
		Error,
	};

	enum class RGCompileDiagnosticCode : uint8_t
	{
		InvalidDeclaration,
		DependencyCycle,
		InvalidExecutionPlan,
	};

	struct RGCompileDiagnostic
	{
		RGCompileDiagnosticSeverity m_Severity = RGCompileDiagnosticSeverity::Error;
		RGCompileDiagnosticCode m_Code = RGCompileDiagnosticCode::InvalidExecutionPlan;
		std::string m_Message;
		RGPassNodeIndex m_Pass = InvalidRGPassNodeIndex;
		RGVirtualResourceIndex m_Resource = InvalidRGVirtualResourceIndex;
	};
}
