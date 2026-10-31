#include "Graphics/RenderPass/RenderPassGISpecularVisibility.h"
#include "Graphics/Pipeline/GISpecularVisibilityObservation.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace gglab
{
	namespace
	{
		const RenderPassInfo PassInfo{ .m_TypeName = "GI.SpecularVisibility",
			.m_DisplayName = "GGX environment visibility", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		constexpr std::array OutputNames
		{
			"GI.SpecularVisibility.Visibility", "GI.SpecularVisibility.Base",
			"GI.SpecularVisibility.Coat", "GI.SpecularVisibility.Weights",
			"GI.SpecularVisibility.Position", "GI.SpecularVisibility.Diagnostics",
			"GI.SpecularVisibility.F0", "GI.SpecularVisibility.LayerEnergy",
		};
		struct Parameters
		{
			std::array<uint32_t, 4> m_Receivers{};
			std::array<uint32_t, 2> m_Reflections{};
			uint32_t m_Depth = 0;
			uint32_t m_BrdfLut = 0;
			std::array<uint32_t, 8> m_Outputs{};
			std::array<uint32_t, 4> m_Previous{};
			std::array<uint32_t, 4> m_Next{};
			std::array<uint32_t, 2> m_Size{};
			std::array<uint32_t, 2> m_TileOrigin{};
			std::array<uint32_t, 2> m_TileExtent{};
			uint32_t m_View = 0;
			uint32_t m_Seed = 0;
			uint32_t m_SampleBegin = 0;
			uint32_t m_SampleCount = 0;
			uint32_t m_SubmittedSamples = 0;
			uint32_t m_BeginSweep = 0;
			uint32_t m_EndSweep = 0;
			uint32_t m_RowOffset = 0;
			float m_MaximumDistance = 0;
			uint32_t m_BrdfSampler = 0;
			uint32_t m_InstanceCount = 0;
			uint32_t m_GeometryCount = 0;
			uint32_t m_MaterialCount = 0;
			uint32_t m_RasterMaterialCapacity = 0;
			uint32_t m_RasterPosition = 0;
			std::array<uint32_t, 3> m_Padding{};
		};
		static_assert(sizeof(Parameters) == 192 && offsetof(Parameters, m_Outputs) == 32 &&
			offsetof(Parameters, m_Previous) == 64 && offsetof(Parameters, m_Size) == 96 &&
			offsetof(Parameters, m_SampleBegin) == 128 && offsetof(Parameters, m_MaximumDistance) == 152 &&
			offsetof(Parameters, m_InstanceCount) == 160 && offsetof(Parameters, m_RasterPosition) == 176);
	}

	RenderFrameValidationResult ValidateGISpecularVisibilityInputs(
		const GISpecularVisibilityCreateInfo& info, const RaySceneDiagnostics& scene) noexcept
	{
		if (info.m_SampleCount == 0 || info.m_SampleCount > 1'048'576 ||
			info.m_SamplesPerBatch == 0 || info.m_SamplesPerBatch > 4096 ||
			info.m_QueryWorkPerSubmission < 2 || info.m_QueryWorkPerSubmission > 16'777'216 ||
			info.m_MaximumTilePixels > 8'388'608 || !std::isfinite(info.m_MaximumDistance) ||
			info.m_MaximumDistance <= 0)
		{
			return RenderFrameValidationResult::ContractFailure("Invalid GGX visibility inputs");
		}
		if (!scene.m_TracksPrimaryMaterials)
		{
			return RenderFrameValidationResult::ContractFailure("Primary material identity is required");
		}
		if (scene.m_Status != RaySceneStatus::Prepared || !scene.m_HasCompleteBounds ||
			!std::isfinite(scene.m_BoundsDiagonal) || scene.m_BoundsDiagonal <= 0 ||
			info.m_MaximumDistance < scene.m_BoundsDiagonal)
		{
			return RenderFrameValidationResult::ContractFailure("IncompleteScene");
		}
		return RenderFrameValidationResult::Ready();
	}

	struct RenderPassGISpecularVisibility::State
	{
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		ComputePipelineRecipe m_Recipe;
		ComputePipelineSlot m_Slot;
		ResolvedRenderPipeline m_Pipeline;
		std::array<uint32_t, 6> m_RayBindings{};
		bool m_ObservationPrepared = false;
	};

	RenderPassGISpecularVisibility::RenderPassGISpecularVisibility() noexcept :
		m_State(std::make_unique<State>()) {}
	RenderPassGISpecularVisibility::~RenderPassGISpecularVisibility() = default;

	RenderFrameValidationResult RenderPassGISpecularVisibility::Prepare(
		const RenderFrameContext& frame, const RenderServices& services,
		const GISpecularVisibilityCreateInfo& info, ShaderID coverageVertex) noexcept
	{
		auto& state = *m_State;
		state.m_ObservationPrepared = false;
		if (!info.m_RayScene || !services.m_GISpecularVisibilityObservation)
		{
			return RenderFrameValidationResult::ContractFailure("GGX visibility owners are unavailable");
		}
		const auto validation = ValidateGISpecularVisibilityInputs(info, info.m_RayScene->GetDiagnostics());
		if (!validation.IsReady())
		{
			return validation;
		}
		if (!state.m_Layout.IsValid())
		{
			const RHITextureDesc desc{ .m_Format = RHIFormat::R32G32B32A32Float,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess | RHITextureUsage::CopySource };
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			if (!common || !services.m_Presentation->GetDevice()->QueryTextureSupport(desc).IsSupported())
			{
				return RenderFrameValidationResult::ContractFailure("GGX visibility layout or format unavailable");
			}
			std::vector<LayoutBindingRequirement> bindings(common->GetBindings().begin(), common->GetBindings().end());
			const auto firstParameter = static_cast<uint32_t>(bindings.size());
			for (uint32_t index = 0; index < state.m_RayBindings.size(); ++index)
			{
				bindings.push_back({ .m_Key = LayoutBindingKey{ 0x800u + index },
					.m_Slot = { .m_Type = index == 0 ?
						RHIBindingType::AccelerationStructure : RHIBindingType::ReadOnlyStorageBuffer,
						.m_Visibility = RHIShaderStage::Compute, .m_Binding = 5 + index, .m_Count = 1 },
					.m_ParameterOrder = firstParameter + index });
			}
			const std::array fragments{ LayoutFragment{ "GISpecularVisibility", bindings } };
			const auto composed = ComposeLayoutFragments(fragments);
			if (!composed.IsSuccess())
			{
				return RenderFrameValidationResult::ContractFailure("GGX visibility layout composition failed");
			}
			state.m_Layout = ResolvedRenderLayout::Create(
				services.m_Presentation->GetRHIContext()->GetPipelineSystem(), *composed.m_Layout, "GI.SpecularVisibility");
			for (uint32_t index = 0; index < state.m_RayBindings.size(); ++index)
			{
				const auto parameter = composed.m_Layout->FindParameterIndex(LayoutBindingKey{ 0x800u + index });
				if (!parameter)
				{
					return RenderFrameValidationResult::ContractFailure("GGX visibility binding unavailable");
				}
				state.m_RayBindings[index] = *parameter;
			}
			state.m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(state.m_Layout, scene_bindings::SceneConstants);
			state.m_Recipe.m_BindingLayout = state.m_Layout.GetNativeLayout();
			state.m_Recipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GISpecularVisibilityCompute);
		}
		state.m_Pipeline = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_Slot, state.m_Recipe, PassInfo);
		if (!state.m_Pipeline.IsValid() || !state.m_Scene.IsValid())
		{
			return RenderFrameValidationResult::ContractFailure("GGX visibility pipeline unavailable");
		}
		if (!services.m_Resources->HasInitializedActiveIBL() || !services.m_Environment->GetBakingStatus().m_HasActiveBake)
		{
			// Allow the existing bake graph to run. No observation or layer weights
			// may consume the uninitialized LUT; ordinary IBL readiness still waits.
			return RenderFrameValidationResult::Ready();
		}
		const auto& view = frame.GetDisplayRenderView();
		GISpecularVisibilityKey key{ .m_Scene = info.m_RayScene,
			.m_SceneRevision = info.m_RayScene->GetDiagnostics().m_Revision,
			.m_BrdfLut = services.m_Resources->GetTextureHandle(RenderTextureIndex::IBL_BrdfLut),
			.m_ShaderGeneration = services.m_ShaderPrograms->GetGeneration(state.m_Recipe.m_CSId),
			.m_PositionShaderGeneration = services.m_ShaderPrograms->GetGeneration(
				services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverPositionPixel)),
			.m_ReceiverShaderGeneration = services.m_ShaderPrograms->GetGeneration(
				services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverPixel)),
			.m_CoverageShaderGeneration = services.m_ShaderPrograms->GetGeneration(coverageVertex),
			.m_CameraPosition = { view.m_CameraPosition.m_X, view.m_CameraPosition.m_Y, view.m_CameraPosition.m_Z },
			.m_MaximumDistance = info.m_MaximumDistance, .m_TextureLodBias = view.m_TextureLodBias,
			.m_Width = view.m_Width, .m_Height = view.m_Height,
			.m_Samples = info.m_SampleCount, .m_Seed = info.m_Seed };
		key.m_BrdfLutRevision = services.m_Resources->GetTextureContentRevision(RenderTextureIndex::IBL_BrdfLut);
		static_assert(sizeof(key.m_ViewProjection) == sizeof(view.m_UnjitteredViewProj));
		std::memcpy(key.m_ViewProjection.data(), &view.m_UnjitteredViewProj, sizeof(key.m_ViewProjection));
		state.m_ObservationPrepared = services.m_GISpecularVisibilityObservation->Prepare(key,
			info.m_SamplesPerBatch, info.m_QueryWorkPerSubmission, info.m_MaximumTilePixels);
		return state.m_ObservationPrepared ?
			RenderFrameValidationResult::Ready() :
			RenderFrameValidationResult::ContractFailure("GGX visibility observation preparation failed");
	}

	void RenderPassGISpecularVisibility::AddPass(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene,
		const GISpecularVisibilityCreateInfo& info) noexcept
	{
		if (!m_State->m_ObservationPrepared)
		{
			return;
		}
		struct PassData
		{
			std::array<RGTextureViewId, 6> m_Receivers{};
			RGTextureViewId m_Depth;
			RGTextureViewId m_RasterPosition;
			RGTextureViewId m_BrdfLut;
			std::array<RGTextureViewId, 8> m_Outputs{};
			std::array<RGTextureViewId, 4> m_Previous{};
			std::array<RGTextureViewId, 4> m_Next{};
			Parameters m_Parameters;
		};
		graph.AddPass<PassData>(PassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[&frame, &services, scene, info](RenderGraph::RGBuilder& builder, PassData& data)
			{
				const auto read = [&builder](RGTextureId texture)
					{
						return builder.CreateView<RHITextureViewType::ShaderResource>(
							builder.Read(texture, RGTextureAccess::Sample, RHIStage::ComputeShader));
					};
				const auto& receivers = builder.GetBlackboard().Get<RGGIReceiverResources>(GIReceiverResourcesName);
				data.m_RasterPosition = read(receivers.m_Position);
				constexpr std::array indices{ 0u, 2u, 3u, 4u, 5u, 6u };
				for (size_t index = 0; index < indices.size(); ++index)
				{
					data.m_Receivers[index] = read(receivers.m_Textures[indices[index]]);
				}
				const auto& depth = builder.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto extent = builder.GetTextureDesc(depth.m_Texture).m_Extent;
				data.m_Depth = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(depth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader), depth.m_SrvDesc);
				data.m_BrdfLut = read(builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName).m_BrdfLut);
				for (size_t index = 0; index < data.m_Outputs.size(); ++index)
				{
					auto texture = builder.CreateTexture(OutputNames[index],
						{ .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
					builder.WriteInPlace(texture, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Outputs[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(texture);
				}
				GGLAB_UNUSED(builder.Read(scene.m_TopLevel, RGAccelerationStructureAccess::QueryRead));
				for (const auto blas : scene.m_BottomLevels)
				{
					GGLAB_UNUSED(builder.Read(blas, RGAccelerationStructureAccess::QueryRead));
				}
				const std::array rayBuffers{ scene.m_Vertices, scene.m_Indices,
					scene.m_Geometries, scene.m_Instances, scene.m_Materials };
				for (const auto buffer : rayBuffers)
				{
					GGLAB_UNUSED(builder.Read(buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader));
				}
				auto* observation = services.m_GISpecularVisibilityObservation;
				const auto progress = observation->GetDiagnostics();
				const auto tile = observation->GetTile();
				data.m_Parameters = { .m_Size = { extent.m_Width, extent.m_Height },
					.m_TileOrigin = { tile.m_X, tile.m_Y }, .m_TileExtent = { tile.m_Width, tile.m_Height },
					.m_View = frame.GetRenderQueue(frame.GetDisplayViewId()).m_CoverageRasterDomain.m_ViewBindingId,
					.m_Seed = info.m_Seed, .m_SampleBegin = progress.m_SubmittedSamples,
					.m_SampleCount = progress.m_BatchSamples, .m_SubmittedSamples = progress.m_SubmittedSamples,
					.m_BeginSweep = tile.m_BeginSweep ? 1u : 0u, .m_EndSweep = tile.m_EndSweep ? 1u : 0u,
					.m_MaximumDistance = info.m_MaximumDistance,
					.m_BrdfSampler = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp),
					.m_InstanceCount = scene.m_InstanceCount, .m_GeometryCount = scene.m_GeometryCount,
					.m_MaterialCount = scene.m_MaterialCount,
					.m_RasterMaterialCapacity = services.m_FrameBuffers->GetMaterialStructuredBuffer()->GetElementCapacity() };
				auto surfaces = observation->Import(builder);
				for (size_t index = 0; index < surfaces.size(); ++index)
				{
					if (progress.m_SubmittedSamples != 0)
					{
						data.m_Previous[index] = read(surfaces[index].m_Previous);
					}
					if (progress.m_BatchSamples != 0)
					{
						auto& next = surfaces[index].m_Next;
						if (tile.m_BeginSweep)
						{
							builder.WriteInPlace(next, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
						}
						else
						{
							builder.ReadWriteInPlace(next, RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
						}
						data.m_Next[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(next);
					}
				}
				observation->Export(builder, surfaces);
				builder.SideEffect();
			},
			[this, &frame, &services, scene](RGExecuteContext& execute, PassData& data)
			{
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands, m_State->m_Layout, m_State->m_Scene);
				if (!commands->RetainForSubmission(scene.m_Lifetime) || !constants.SetPipeline(m_State->m_Pipeline) ||
					!constants.Bind(m_State->m_Scene, frame.m_FrameConstants.m_SceneSnapshot) ||
					!commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB), frame.m_FrameConstants.m_Views) ||
					!commands->SetAccelerationStructure(m_State->m_RayBindings[0], execute.GetAccelerationStructureHandle(scene.m_TopLevel)))
				{
					return;
				}
				commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
					services.m_FrameBuffers->GetMaterialStructuredBuffer()->GetBufferHandle(frame.m_FrameSlotIndex));
				const std::array buffers{ scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances, scene.m_Materials };
				for (uint32_t index = 0; index < buffers.size(); ++index)
				{
					commands->SetReadOnlyBuffer(m_State->m_RayBindings[index + 1], execute.GetBufferHandle(buffers[index]));
				}
				auto parameters = data.m_Parameters;
				parameters.m_Depth = execute.GetViewDescriptor(data.m_Depth).m_Index;
				parameters.m_RasterPosition = execute.GetViewDescriptor(data.m_RasterPosition).m_Index;
				parameters.m_BrdfLut = execute.GetViewDescriptor(data.m_BrdfLut).m_Index;
				for (size_t index = 0; index < data.m_Receivers.size(); ++index)
				{
					const auto descriptor = execute.GetViewDescriptor(data.m_Receivers[index]).m_Index;
					if (index < 4)
					{
						parameters.m_Receivers[index] = descriptor;
					}
					else
					{
						parameters.m_Reflections[index - 4] = descriptor;
					}
				}
				for (size_t index = 0; index < data.m_Outputs.size(); ++index)
				{
					parameters.m_Outputs[index] = execute.GetViewDescriptor(data.m_Outputs[index]).m_Index;
				}
				for (size_t index = 0; index < data.m_Previous.size(); ++index)
				{
					if (parameters.m_SubmittedSamples != 0)
					{
						parameters.m_Previous[index] = execute.GetViewDescriptor(data.m_Previous[index]).m_Index;
					}
					if (parameters.m_SampleCount != 0)
					{
						parameters.m_Next[index] = execute.GetViewDescriptor(data.m_Next[index]).m_Index;
					}
				}
				const uint64_t rowWork = static_cast<uint64_t>(parameters.m_Size[0]) *
					std::max(1ull, static_cast<uint64_t>(parameters.m_SampleCount) * 2);
				const uint32_t rows = std::max(8u, static_cast<uint32_t>(8'388'608 / rowWork) / 8 * 8);
				for (uint32_t row = 0; row < parameters.m_Size[1]; row += rows)
				{
					parameters.m_RowOffset = row;
					if (!commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
					{
						return;
					}
					commands->Dispatch((parameters.m_Size[0] + 7) / 8,
						(std::min(rows, parameters.m_Size[1] - row) + 7) / 8, 1);
				}
				services.m_GISpecularVisibilityObservation->MarkRecorded();
			});
	}
}
