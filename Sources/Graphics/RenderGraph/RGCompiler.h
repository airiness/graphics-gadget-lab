#pragma once
#include "Graphics/RenderGraph/RGExecutionPlan.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class RenderGraph;

	struct RGCompileResult
	{
		std::unique_ptr<RGExecutionPlan> m_Plan;
		std::vector<RGCompileDiagnostic> m_Diagnostics;

		[[nodiscard]] explicit operator bool() const noexcept { return m_Plan != nullptr; }
	};

	class RGCompiler final
	{
	public:
		[[nodiscard]] static RGCompileResult Compile(const RenderGraph& graph) noexcept;

	private:
		explicit RGCompiler(const RenderGraph& graph) noexcept;

		[[nodiscard]] RGCompileResult Build() noexcept;
		void InitializePlan() noexcept;
		void BuildDependencyGraph() noexcept;
		void CullPasses() noexcept;
		[[nodiscard]] bool TopologicalSortPasses() noexcept;
		void BuildResourceLifetimes() noexcept;
		[[nodiscard]] bool HasErrors() const noexcept;
		void AddDiagnostic(
			RGCompileDiagnosticCode code,
			const char* message,
			RGPassNodeIndex pass = InvalidRGPassNodeIndex,
			RGVirtualResourceIndex resource = InvalidRGVirtualResourceIndex) noexcept;

		const RenderGraph& m_Graph;
		std::unique_ptr<RGExecutionPlan> m_Plan;
		std::vector<RGCompileDiagnostic> m_Diagnostics;
		std::unordered_map<const RGVirtualResourceBase*, RGVirtualResourceIndex> m_ResourceIndices;
	};
}
