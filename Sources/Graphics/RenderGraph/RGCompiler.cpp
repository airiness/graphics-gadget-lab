#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RGCompiler.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGBarrierPlanner.h"

namespace gglab
{
	RGCompiler::RGCompiler(const RenderGraph& graph) noexcept :
		m_Graph(graph),
		m_Plan(new RGExecutionPlan())
	{}

	RGCompileResult RGCompiler::Compile(const RenderGraph& graph) noexcept
	{
		return RGCompiler(graph).Build();
	}

	RGCompileResult RGCompiler::Build() noexcept
	{
		if (!m_Graph.m_BuildValid)
		{
			AddDiagnostic(
				RGCompileDiagnosticCode::InvalidDeclaration,
				"RenderGraph compilation rejected invalid resource declarations.");
			return { nullptr, std::move(m_Diagnostics) };
		}

		InitializePlan();
		if (HasErrors())
		{
			return { nullptr, std::move(m_Diagnostics) };
		}
		BuildDependencyGraph();
		if (HasErrors())
		{
			return { nullptr, std::move(m_Diagnostics) };
		}
		CullPasses();
		if (!TopologicalSortPasses())
		{
			return { nullptr, std::move(m_Diagnostics) };
		}
		BuildResourceLifetimes();
		RGBarrierPlanner(
			m_Plan->m_Passes,
			m_Plan->m_Resources,
			m_Plan->m_ExecutionOrder).Build();

		m_Plan->m_Diagnostics = m_Diagnostics;
		return { std::move(m_Plan), std::move(m_Diagnostics) };
	}

	void RGCompiler::InitializePlan() noexcept
	{
		m_ResourceIndices.reserve(m_Graph.m_VirtualResources.size());
		m_Plan->m_Resources.reserve(m_Graph.m_VirtualResources.size());
		for (uint32_t resourceIndex = 0; resourceIndex < m_Graph.m_VirtualResources.size(); ++resourceIndex)
		{
			auto* resource = m_Graph.m_VirtualResources[resourceIndex];
			const RGVirtualResourceIndex stableIndex{ resourceIndex };
			m_ResourceIndices.emplace(resource, stableIndex);
			m_Plan->m_Resources.push_back(
				{
					.m_Declaration = stableIndex,
					.m_Resource = resource,
					.m_NameId = resource->m_NameId,
					.m_ResourceType = resource->m_ResourceType,
					.m_Imported = resource->m_Imported,
					.m_InitialState = resource->m_InitialBarrierState,
					.m_FinalState = resource->m_FinalBarrierState,
					.m_FinalSubresources = resource->m_FinalBarrierSubresources,
					.m_ExportPass = resource->m_ExportPass,
					.m_ExportResourceNode = resource->m_ExportResourceNodeIndex,
				});
		}

		m_Plan->m_Passes.reserve(m_Graph.m_PassNodes.size());
		for (uint32_t passIndex = 0; passIndex < m_Graph.m_PassNodes.size(); ++passIndex)
		{
			const auto& declaration = m_Graph.m_PassNodes[passIndex];
			RGCompiledPass compiledPass{};
			compiledPass.m_Declaration = RGPassNodeIndex{ passIndex };
			compiledPass.m_NameId = declaration.m_NameId;
			compiledPass.m_Executor = declaration.m_Pass;
			compiledPass.m_SideEffect = declaration.m_SideEffect;
			compiledPass.m_Accesses.reserve(declaration.m_Accesses.size());
			for (const auto& access : declaration.m_Accesses)
			{
				const auto* resource = m_Graph.m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource;
				const auto resourceIter = m_ResourceIndices.find(resource);
				GGLAB_ASSERT_MSG(resourceIter != m_ResourceIndices.end(),
					"RenderGraph access references an unknown virtual resource.");
				if (resourceIter == m_ResourceIndices.end())
				{
					AddDiagnostic(
						RGCompileDiagnosticCode::InvalidExecutionPlan,
						"RenderGraph access references an unknown virtual resource.",
						compiledPass.m_Declaration);
				}
				compiledPass.m_Accesses.push_back(
					{
						.m_ResourceNodeIndex = access.m_ResourceNodeIndex,
						.m_Resource = resourceIter != m_ResourceIndices.end() ?
							resourceIter->second : InvalidRGVirtualResourceIndex,
						.m_AccessValue = access.m_AccessValue,
						.m_Stages = access.m_Stages,
						.m_ResourceType = access.m_ResourceType,
						.m_DependencyAccess = access.m_DependencyAccess,
						.m_Subresources = access.m_Subresources,
					});
			}
			m_Plan->m_Passes.push_back(std::move(compiledPass));
		}

		m_Plan->m_TextureViews.reserve(m_Graph.m_TextureViews.size());
		for (const auto& view : m_Graph.m_TextureViews)
		{
			const auto* resource = m_Graph.GetVirtualResource(view.m_Texture);
			const auto resourceIter = m_ResourceIndices.find(resource);
			GGLAB_ASSERT_MSG(resourceIter != m_ResourceIndices.end(),
				"RenderGraph texture view references an unknown virtual resource.");
			if (resourceIter == m_ResourceIndices.end())
			{
				AddDiagnostic(
					RGCompileDiagnosticCode::InvalidExecutionPlan,
					"RenderGraph texture view references an unknown virtual resource.");
			}

			RHITextureViewDesc desc = view.m_Desc.value_or(RHITextureViewDesc{});
			desc.m_Type = view.m_Type;
			m_Plan->m_TextureViews.push_back(
				{
					.m_Resource = resourceIter != m_ResourceIndices.end() ?
						resourceIter->second : InvalidRGVirtualResourceIndex,
					.m_Desc = desc,
				});
		}
	}

	void RGCompiler::BuildDependencyGraph() noexcept
	{
		auto addEdge = [this](RGPassNodeIndex from,
			RGPassNodeIndex to,
			RGResourceNodeIndex resourceNodeIndex,
			RGDependencyReason reason) noexcept
			{
				if (!from.IsValid() || !to.IsValid() || from == to)
				{
					return;
				}

				auto edgeIter = std::ranges::find_if(m_Plan->m_DependencyEdges,
					[&](const RGPassDependencyEdge& edge)
					{
						return edge.m_From == from &&
							edge.m_To == to &&
							edge.m_ResourceNodeIndex == resourceNodeIndex;
					});
				if (edgeIter != m_Plan->m_DependencyEdges.end())
				{
					if (!IsRGLivenessDependency(edgeIter->m_Reason) && IsRGLivenessDependency(reason))
					{
						edgeIter->m_Reason = reason;
					}
					return;
				}

				m_Plan->m_DependencyEdges.push_back({ from, to, resourceNodeIndex, reason });
				m_Plan->m_Passes[from.Value()].m_Dependents.push_back(to);
				m_Plan->m_Passes[to.Value()].m_Dependencies.push_back(from);
			};

		for (uint32_t resourceNodeIndex = 0; resourceNodeIndex < m_Graph.m_ResourceNodes.size(); ++resourceNodeIndex)
		{
			const auto& resourceNode = m_Graph.m_ResourceNodes[resourceNodeIndex];
			const RGResourceNodeIndex stableResourceNodeIndex{ resourceNodeIndex };
			for (const auto readerPassIndex : resourceNode.m_Readers)
			{
				addEdge(resourceNode.m_Writer,
					readerPassIndex,
					stableResourceNodeIndex,
					RGDependencyReason::WriterToReader);
			}

			if (!resourceNode.m_Previous.IsValid() || !resourceNode.m_Writer.IsValid())
			{
				continue;
			}

			const auto& previousNode = m_Graph.m_ResourceNodes[resourceNode.m_Previous.Value()];
			addEdge(previousNode.m_Writer,
				resourceNode.m_Writer,
				resourceNode.m_Previous,
				RGDependencyReason::PreviousWriterToWriter);
			for (const auto previousReader : previousNode.m_Readers)
			{
				addEdge(previousReader,
					resourceNode.m_Writer,
					resourceNode.m_Previous,
					RGDependencyReason::PreviousReaderToWriter);
			}
		}

		for (const auto& resource : m_Plan->m_Resources)
		{
			if (!resource.m_ExportPass.IsValid())
			{
				continue;
			}
			if (!resource.m_ExportResourceNode.IsValid() ||
				resource.m_ExportResourceNode.Value() >= m_Graph.m_ResourceNodes.size())
			{
				AddDiagnostic(
					RGCompileDiagnosticCode::InvalidExecutionPlan,
					"RenderGraph export references an invalid resource node.",
					resource.m_ExportPass,
					resource.m_Declaration);
				continue;
			}

			const auto& resourceNode = m_Graph.m_ResourceNodes[resource.m_ExportResourceNode.Value()];
			addEdge(resourceNode.m_Writer,
				resource.m_ExportPass,
				resource.m_ExportResourceNode,
				RGDependencyReason::ExportWriterToExport);
			for (const auto readerPassIndex : resourceNode.m_Readers)
			{
				addEdge(readerPassIndex,
					resource.m_ExportPass,
					resource.m_ExportResourceNode,
					RGDependencyReason::ExportReaderToExport);
			}
		}
	}

	void RGCompiler::CullPasses() noexcept
	{
		std::vector<RGPassNodeIndex> stack;
		std::unordered_set<RGPassNodeIndex> reachable;
		std::vector<std::vector<RGPassNodeIndex>> livenessDependencies(m_Plan->m_Passes.size());
		for (const auto& edge : m_Plan->m_DependencyEdges)
		{
			if (IsRGLivenessDependency(edge.m_Reason))
			{
				livenessDependencies[edge.m_To.Value()].push_back(edge.m_From);
			}
		}

		auto addPass = [&stack, &reachable](RGPassNodeIndex passNodeIndex)
			{
				if (passNodeIndex.IsValid() && reachable.insert(passNodeIndex).second)
				{
					stack.push_back(passNodeIndex);
				}
			};

		for (uint32_t passIndex = 0; passIndex < m_Plan->m_Passes.size(); ++passIndex)
		{
			if (m_Plan->m_Passes[passIndex].m_SideEffect)
			{
				addPass(RGPassNodeIndex{ passIndex });
			}
		}

		while (!stack.empty())
		{
			const RGPassNodeIndex passIndex = stack.back();
			stack.pop_back();
			for (const auto dependency : livenessDependencies[passIndex.Value()])
			{
				addPass(dependency);
			}
		}

		for (uint32_t passIndex = 0; passIndex < m_Plan->m_Passes.size(); ++passIndex)
		{
			m_Plan->m_Passes[passIndex].m_Culled =
				reachable.find(RGPassNodeIndex{ passIndex }) == reachable.end();
		}
	}

	bool RGCompiler::TopologicalSortPasses() noexcept
	{
		std::vector<uint32_t> indegrees(m_Plan->m_Passes.size(), 0);
		std::vector<RGPassNodeIndex> ready;
		for (uint32_t passIndex = 0; passIndex < m_Plan->m_Passes.size(); ++passIndex)
		{
			const auto& pass = m_Plan->m_Passes[passIndex];
			if (pass.m_Culled)
			{
				continue;
			}
			for (const auto dependency : pass.m_Dependencies)
			{
				if (!m_Plan->m_Passes[dependency.Value()].m_Culled)
				{
					++indegrees[passIndex];
				}
			}
			if (indegrees[passIndex] == 0)
			{
				ready.push_back(RGPassNodeIndex{ passIndex });
			}
		}

		while (!ready.empty())
		{
			std::ranges::sort(ready);
			const RGPassNodeIndex passIndex = ready.front();
			ready.erase(ready.begin());
			m_Plan->m_Passes[passIndex.Value()].m_ExecutionOrder =
				static_cast<int32_t>(m_Plan->m_ExecutionOrder.size());
			m_Plan->m_ExecutionOrder.push_back(passIndex);

			for (const auto dependent : m_Plan->m_Passes[passIndex.Value()].m_Dependents)
			{
				if (m_Plan->m_Passes[dependent.Value()].m_Culled)
				{
					continue;
				}
				GGLAB_ASSERT_MSG(indegrees[dependent.Value()] > 0,
					"RenderGraph topological sort indegree underflow.");
				if (--indegrees[dependent.Value()] == 0)
				{
					ready.push_back(dependent);
				}
			}
		}

		const uint32_t reachablePassCount = static_cast<uint32_t>(std::ranges::count_if(
			m_Plan->m_Passes,
			[](const RGCompiledPass& pass) { return !pass.m_Culled; }));
		if (m_Plan->m_ExecutionOrder.size() == reachablePassCount)
		{
			return true;
		}

		AddDiagnostic(
			RGCompileDiagnosticCode::DependencyCycle,
			"RenderGraph dependency graph contains a cycle; execution is disabled.");
		m_Plan->m_ExecutionOrder.clear();
		for (auto& pass : m_Plan->m_Passes)
		{
			pass.m_ExecutionOrder = -1;
		}
		return false;
	}

	void RGCompiler::BuildResourceLifetimes() noexcept
	{
		for (const auto passIndex : m_Plan->m_ExecutionOrder)
		{
			const auto& pass = m_Plan->m_Passes[passIndex.Value()];
			for (const auto& access : pass.m_Accesses)
			{
				auto& resource = m_Plan->m_Resources[access.m_Resource.Value()];
				++resource.m_RefCount;
				resource.m_UsageBits |= access.m_ResourceType == RGResourceType::RGTexture ?
					static_cast<uint64_t>(ToRHIUsage(static_cast<RGTextureAccess>(access.m_AccessValue))) :
					static_cast<uint64_t>(ToRHIUsage(static_cast<RGBufferAccess>(access.m_AccessValue)));
				if (!resource.m_FirstUser.IsValid())
				{
					resource.m_FirstUser = passIndex;
				}
				resource.m_LastUser = passIndex;
			}
		}

		for (const auto& resource : m_Plan->m_Resources)
		{
			if (resource.m_RefCount == 0)
			{
				continue;
			}
			m_Plan->m_Passes[resource.m_FirstUser.Value()].m_AcquireResources.push_back(resource.m_Declaration);
			m_Plan->m_Passes[resource.m_LastUser.Value()].m_ReleaseResources.push_back(resource.m_Declaration);
		}
	}

	void RGCompiler::AddDiagnostic(
		RGCompileDiagnosticCode code,
		const char* message,
		RGPassNodeIndex pass,
		RGVirtualResourceIndex resource) noexcept
	{
		m_Diagnostics.push_back(
			{
				.m_Severity = RGCompileDiagnosticSeverity::Error,
				.m_Code = code,
				.m_Message = message ? message : "RenderGraph compilation failed.",
				.m_Pass = pass,
				.m_Resource = resource,
			});
	}

	bool RGCompiler::HasErrors() const noexcept
	{
		return std::ranges::any_of(
			m_Diagnostics,
			[](const RGCompileDiagnostic& diagnostic)
			{
				return diagnostic.m_Severity == RGCompileDiagnosticSeverity::Error;
			});
	}
}
