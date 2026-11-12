#include "AssetTestServices.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Core/StringId.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetLifecycleTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/ModelTypes.h"
#include "GGLabRuntime/Graphics/Asset/ReservedTexture.h"
#include "GGLabRuntime/Graphics/Asset/TextureAssetViews.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelinePresets.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassInfo.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderScene.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderTextureAssetAccess.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/SamplerTypes.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabRuntime/Graphics/VertexData.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderMaterialFrameCache.h"
#include "Graphics/RenderPass/ForwardShadingCommon.h"
#include "Graphics/RenderPass/RenderPassClearViewTargets.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "Graphics/RenderSceneBuilder.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::rendering_test
{
	void RunMaterialDiagnosticPrewarmTests(SelfTestContext& context) noexcept
	{
		class ShaderAccess final : public RenderShaderProgramAccess
		{
		public:
			ShaderID LoadProgram(const ShaderProgramRef& program) noexcept override
			{
				const auto demand = shader_programs::GetForwardPBRMaterialDiagnosticsShaderProgramDemand();
				if (const auto entry = std::ranges::find(demand, program); entry != demand.end())
				{
					return ShaderID{ static_cast<uint32_t>(entry - demand.begin()) };
				}
				// Only the Lab-composed validation recipe may resolve HDR-diff programs.
				const std::array validation
				{
					shader_programs::ForwardPBRForwardPlusValidationPixel,
					shader_programs::ForwardPBRForwardPlusValidationGTAOPixel,
					shader_programs::ForwardPBRForwardPlusValidationMaterialDiagnosticsPixel,
					shader_programs::ForwardPBRForwardPlusValidationGTAOMaterialDiagnosticsPixel,
				};
				const auto entry = std::ranges::find(validation, program);
				return !m_AllowValidationPrograms || entry == validation.end() ?
					ShaderID{} :
					ShaderID{ static_cast<uint32_t>(demand.size() + (entry - validation.begin())) };
			}
			uint64_t GetGeneration(ShaderID) const noexcept override { return 1; }
			bool m_AllowValidationPrograms = false;
		} shaders;
		class BindingAccess final : public RenderBindingLayoutAccess
		{
		public:
			RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept override { return { 1, 1 }; }
			RHIBindingLayoutDesc GetCommonBindingLayoutDesc() const noexcept override { return {}; }
			const ComposedLayout* GetCommonLayoutContract() const noexcept override
			{
				return &scene_bindings::GetCommonLayoutContract();
			}
		} bindings;
		class PipelineSystem final : public RHIPipelineSystem
		{
		public:
			RHIBindingLayoutHandle CreateBindingLayout(const RHIBindingLayoutDesc& desc) noexcept override
			{
				m_OwnerOnly &= std::this_thread::get_id() == m_Owner;
				return { std::string_view(desc.m_DebugName).find("ForwardPlus") != std::string_view::npos ?
					2u :
					1u, 1 };
			}
			RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineCreateInfo&) noexcept override { return {}; }
			RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineCreateInfo&) noexcept override { return {}; }
			bool IsAlive(RHIBindingLayoutHandle handle) const noexcept override { return handle.IsValid(); }
			bool IsAlive(RHIPipelineHandle handle) const noexcept override { return handle.IsValid(); }
			uint64_t GetRevision() const noexcept override { return 1; }
			void Clear() noexcept override {}
			std::thread::id m_Owner = std::this_thread::get_id();
			bool m_OwnerOnly = true;
		} pipelineSystem;
		class PipelineAccess final : public RenderPipelineResolver
		{
		public:
			RHIPipelineHandle Resolve(GraphicsPipelineSlot&, const GraphicsPhysicalPipelineKey& key, const RenderPassInfo&) noexcept override
			{
				++m_ResolveCount;
				m_OwnerOnly &= std::this_thread::get_id() == m_Owner;
				if (m_FailResolve)
				{
					return {};
				}
				const auto found = std::ranges::find(m_Keys, key);
				if (found == m_Keys.end())
				{
					m_Keys.push_back(key);
				}
				const auto entry = std::ranges::find(m_Keys, key);
				return RHIPipelineHandle{ static_cast<uint32_t>(entry - m_Keys.begin()) + 1, 1 };
			}
			RHIPipelineHandle Resolve(ComputePipelineSlot&, const ComputePipelineRecipe&, const RenderPassInfo&) noexcept override
			{
				return {};
			}
			void GetPipelineUsages(RHIPipelineHandle, std::vector<RenderPassInfo>&) const noexcept override {}
			std::vector<GraphicsPhysicalPipelineKey> m_Keys;
			std::thread::id m_Owner = std::this_thread::get_id();
			uint32_t m_ResolveCount = 0;
			bool m_OwnerOnly = true;
			bool m_FailResolve = false;
		} pipelines;
		class Context final : public RHIContext
		{
		public:
			explicit Context(RHIPipelineSystem& pipelines) noexcept : m_Pipelines(pipelines) {}
			RHIDevice& GetDevice() noexcept override { std::terminate(); }
			const RHIDevice& GetDevice() const noexcept override { std::terminate(); }
			RHISwapChain& GetSwapChain() noexcept override { std::terminate(); }
			const RHISwapChain& GetSwapChain() const noexcept override { std::terminate(); }
			TransferManager& GetTransferManager() noexcept override { std::terminate(); }
			RHIPipelineSystem& GetPipelineSystem() noexcept override { return m_Pipelines; }
			GpuProfiler* GetGpuProfiler() noexcept override { return nullptr; }
			RHIFrameBeginResult BeginFrame() noexcept override { return RHIFrameBeginResult::Unavailable(); }
			RHIFrameEndResult EndFrame(RHIFrameContext&) noexcept override { return RHIFrameEndResult::Fatal(); }
			RHIFrameEndResult AbortFrame(RHIFrameContext&) noexcept override { return RHIFrameEndResult::Fatal(); }
			bool WaitForFence(RHIQueueType, const RHIFencePoint&) noexcept override { return true; }
			void Resize(uint32_t, uint32_t) noexcept override {}
			RHIGpuWaitResult WaitIdleFor(uint32_t) noexcept override { return RHIGpuWaitResult::Completed; }
			void RetireCompletedWork() noexcept override {}
			bool IsPoisoned() const noexcept override { return false; }
			bool IsDeviceLost() const noexcept override { return false; }
			uint32_t GetFrameSlotCount() const noexcept override { return 2; }
		private:
			RHIPipelineSystem& m_Pipelines;
		} rhi(pipelineSystem);
		class PresentationAccess final : public RenderPresentationAccess
		{
		public:
			explicit PresentationAccess(RHIContext& context) noexcept : m_Context(context) {}
			RHIContext* GetRHIContext() const noexcept override { return &m_Context; }
			RHIDevice* GetDevice() const noexcept override { return nullptr; }
			RHISwapChain* GetSwapChain() const noexcept override { return nullptr; }
			const std::array<float, 4>& GetBackBufferClearColor() const noexcept override { return m_Clear; }
			RHIFencePoint GetLastSubmittedFencePoint() const noexcept override { return {}; }
		private:
			RHIContext& m_Context;
			std::array<float, 4> m_Clear{};
		} presentation(rhi);
		const RenderServices services{
			.m_PipelineResolver = &pipelines, .m_ShaderPrograms = &shaders,
			.m_Presentation = &presentation, .m_BindingLayout = &bindings,
		};
		const std::array variants
		{
			RenderQueueBuilder::EncodeMaterialVariantBits(AlphaMode::Opaque, MaterialFlags::None),
			RenderQueueBuilder::EncodeMaterialVariantBits(AlphaMode::Mask, MaterialFlags::DoubleSided),
			RenderQueueBuilder::EncodeMaterialVariantBits(AlphaMode::Blend, MaterialFlags::DoubleSided),
		};
		context.Check(variants[0] == RenderQueueBuilder::EncodeVariantBits(RenderBucket::Opaque, false) &&
			variants[1] == RenderQueueBuilder::EncodeVariantBits(RenderBucket::AlphaTest, true) &&
			variants[2] == RenderQueueBuilder::EncodeVariantBits(RenderBucket::Transparent, true),
			"Material prewarm demand shares alpha and sidedness encoding with draw queue construction");

		auto pipeline = std::make_unique<RenderPipelineForwardPlus>();
		MaterialDiagnosticPrewarmProgress progress;
		bool onePerTick = true;
		for (uint32_t tick = 0; tick < 5; ++tick)
		{
			const uint32_t before = pipelines.m_ResolveCount;
			progress = pipeline->PrewarmMaterialDiagnostics(services, variants);
			onePerTick &= pipelines.m_ResolveCount == before + 1 &&
				progress.m_CompletedCount == tick + 1 && progress.m_TotalCount == 5 &&
				(progress.IsReady() == (tick == 4));
		}
		context.Check(onePerTick && pipelines.m_OwnerOnly && pipelineSystem.m_OwnerOnly,
			"Diagnostic prewarm advances one owner-thread PSO per tick and waits for every demanded variant");

		bool validAttachments = true;
		bool validBlend = true;
		bool separateLighting = true;
		constexpr RHIBindingLayoutHandle forwardPlusLayout{ 2, 1 };
		for (const auto& key : pipelines.m_Keys)
		{
			// Transparent recipes read depth without EQUAL and never bind Forward+ light lists.
			separateLighting &= (key.m_DepthPreset == DepthPreset::ReversedZReadOnly) == (key.m_BindingLayout != forwardPlusLayout);
			const uint32_t count = key.m_Formats.m_RenderTargetCount;
			validAttachments &= count >= 4 && count <= 6 &&
				key.m_Formats.m_RenderTargetFormats[count - 3] == RHIFormat::R16G16B16A16Float &&
				key.m_Formats.m_RenderTargetFormats[count - 2] == RHIFormat::R16Float &&
				key.m_Formats.m_RenderTargetFormats[count - 1] == RHIFormat::R16G16B16A16Float;
			validBlend &= key.m_DepthPreset == DepthPreset::ReversedZReadOnly ?
				key.m_BlendPreset == BlendPreset::AlphaBlendAllTargets :
				key.m_BlendPreset == BlendPreset::Default &&
					key.m_DepthPreset == DepthPreset::ReversedZEqualReadOnly;
		}
		context.Check(validAttachments && validBlend && pipelines.m_Keys.size() == 5,
			"Prewarmed production recipes preserve diagnostic MRT formats, GTAO variants, depth-equal opaque shading and transparent coverage blending");
		context.Check(separateLighting,
			"Opaque recipes consume Forward+ light lists while transparent recipes keep an independent all-lights layout");

		const uint32_t completedCalls = pipelines.m_ResolveCount;
		context.Check(pipeline->PrewarmMaterialDiagnostics(services, variants).IsReady() &&
			pipelines.m_ResolveCount == completedCalls,
			"Completed diagnostic prewarm performs no additional PSO resolutions");

		shaders.m_AllowValidationPrograms = true;
		RenderPipelineForwardPlus validationPipeline(RenderPipelineForwardPlus::CreateInfo{
			.m_ForwardPlusDebugReadback = std::make_shared<ForwardPlusDebugReadback>(),
		});
		MaterialDiagnosticPrewarmProgress validationProgress;
		for (uint32_t tick = 0; tick < 9; ++tick)
		{
			validationProgress = validationPipeline.PrewarmMaterialDiagnostics(services, variants);
		}
		context.Check(validationProgress.IsReady() && validationProgress.m_TotalCount == 9 &&
			std::ranges::any_of(pipelines.m_Keys, [](const GraphicsPhysicalPipelineKey& key)
				{
					return key.m_Formats.m_RenderTargetCount == 6;
				}),
			"The Lab-composed validation recipe prewarms HDR-diff variants beside the production variants");
		shaders.m_AllowValidationPrograms = false;

		pipelines.m_FailResolve = true;
		const std::array changedDemand{ variants[1] };
		const auto failed = pipeline->PrewarmMaterialDiagnostics(services, changedDemand);
		context.Check(failed.m_Failed && !failed.IsReady() && failed.m_CompletedCount == 0 &&
			failed.m_TotalCount == 2,
			"Changed draw demand restarts preparation and a failed PSO cannot report ready");
	}

	void RunMaterialDiagnosticDemandTests(SelfTestContext& context) noexcept
	{
		asset_test::AssetManagerHarness harness("material-diagnostic-demand");
		context.Check(harness.IsValid(), "Diagnostic demand composes headless asset services");
		if (!harness.IsValid())
		{
			return;
		}
		AssetManager& assets = harness.GetAssets();
		auto materialOwner = std::make_unique<Material>();
		materialOwner->m_Name = StringID("DiagnosticDemand.Material");
		Material* material = materialOwner.get();
		const MaterialID materialId = assets.AddProceduralMaterial(std::move(materialOwner));
		auto meshOwner = std::make_unique<Mesh>();
		meshOwner->m_Name = StringID("DiagnosticDemand.Mesh");
		Mesh* mesh = meshOwner.get();
		AssetManager::MeshUploadData upload{};
		upload.m_VerticesData = { Vertex{}, Vertex{}, Vertex{} };
		upload.m_IndicesData = { 0, 1, 2 };
		const MeshID meshId = assets.AddProceduralMesh(std::move(meshOwner), upload);
		auto modelOwner = std::make_unique<Model>();
		modelOwner->m_Name = StringID("DiagnosticDemand.Model");
		modelOwner->m_MeshInstance.push_back({ .m_MeshId = meshId, .m_MaterialId = materialId });
		Model* model = modelOwner.get();
		const ModelID modelId = assets.AddProceduralModel(std::move(modelOwner));
		const bool ready = modelId.IsValid() && harness.PumpUntil([&]()
			{
				const Model* current = assets.GetModel(modelId);
				return current && current->m_ContentState == AssetContentState::Ready &&
					current->m_ResidencyState == AssetResidencyState::Resident;
			});
		context.Check(ready, "Diagnostic demand fixture publishes a resident model and mesh");
		if (!ready)
		{
			return;
		}
		World world;
		auto& registry = world.GetRegistry();
		const auto entity = registry.create();
		registry.emplace<components::TransformComponent>(entity);
		registry.emplace<components::ModelComponent>(entity, modelId);
		context.Check(!HasMaterialDiagnosticDemand(world, assets),
			"An ordinary material does not require diagnostic attachments");
		material->m_DebugView = MaterialDebugView::Normal;
		context.Check(HasMaterialDiagnosticDemand(world, assets),
			"Current asset material diagnostics are visible before scene upload or graph construction");
		auto& instance = registry.emplace<components::MaterialInstanceComponent>(entity);
		instance.m_Key = RuntimeMaterialKey("DiagnosticDemand.Override");
		context.Check(!HasMaterialDiagnosticDemand(world, assets),
			"A valid lit runtime override suppresses its asset material's diagnostic demand");
		instance.m_Key = {};
		context.Check(HasMaterialDiagnosticDemand(world, assets),
			"An invalid runtime material key falls back to the asset's diagnostic material");
		material->m_DebugView = MaterialDebugView::Lit;
		instance.m_Key = RuntimeMaterialKey("DiagnosticDemand.Override");
		instance.m_Properties.m_DebugView = MaterialDebugView::Normal;
		bool allBuckets = true;
		for (AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
		{
			instance.m_Properties.m_AlphaMode = mode;
			allBuckets &= HasMaterialDiagnosticDemand(world, assets);
		}
		context.Check(allBuckets,
			"Opaque, alpha-tested and transparent runtime diagnostic materials all require Native composition");
		bool unavailableExcluded = true;
		mesh->m_IsUploaded = false;
		unavailableExcluded &= !HasMaterialDiagnosticDemand(world, assets);
		mesh->m_IsUploaded = true;
		mesh->m_ResidencyState = AssetResidencyState::NonResident;
		unavailableExcluded &= !HasMaterialDiagnosticDemand(world, assets);
		mesh->m_ResidencyState = AssetResidencyState::Resident;
		const uint32_t indexCount = mesh->m_IndexCount;
		mesh->m_IndexCount = 0;
		unavailableExcluded &= !HasMaterialDiagnosticDemand(world, assets);
		mesh->m_IndexCount = indexCount;
		model->m_ContentState = AssetContentState::Loading;
		unavailableExcluded &= !HasMaterialDiagnosticDemand(world, assets);
		model->m_ContentState = AssetContentState::Ready;
		model->m_ResidencyState = AssetResidencyState::NonResident;
		unavailableExcluded &= !HasMaterialDiagnosticDemand(world, assets);
		model->m_ResidencyState = AssetResidencyState::Resident;
		context.Check(unavailableExcluded && HasMaterialDiagnosticDemand(world, assets),
			"Diagnostic demand follows scene-build readiness and ignores unavailable or empty geometry");
		instance.m_Properties.m_DebugView = MaterialDebugView::UnfilteredLit;
		context.Check(!HasMaterialDiagnosticDemand(world, assets),
			"Leaving diagnostics removes demand immediately; UnfilteredLit requires no diagnostic MRTs");
		registry.clear();
		context.Check(!HasMaterialDiagnosticDemand(world, assets),
			"Removing diagnostic entities leaves no demand from a previous frame");
	}

	void RunMaterialFrameCacheContractTests(SelfTestContext& context) noexcept
	{
		class TextureAccess final : public RenderTextureAssetAccess
		{
		public:
			TextureContentRef GetTextureContentRef(TextureID) const noexcept override { return {}; }
			std::optional<ResidentTextureResource> GetResidentTextureResource(TextureContentRef) const noexcept override
			{
				return std::nullopt;
			}
			void MarkTextureUsed(TextureID) noexcept override { ++m_UsageCount; }
			uint32_t ResolveSrvIndex(TextureID id, ReservedTextureIDIndex fallback) const noexcept override
			{
				++m_ResolveCount;
				return id.IsValid() && m_Resident ?
					m_DescriptorBase + id.Value() :
					static_cast<uint32_t>(fallback);
			}

			uint32_t m_UsageCount = 0;
			mutable uint32_t m_ResolveCount = 0;
			uint32_t m_DescriptorBase = 100;
			bool m_Resident = true;
		} textures;
		class SamplerAccess final : public RenderSamplerAccess
		{
		public:
			SamplerID GetOrCreateSampler(const SamplerKey&) noexcept override { return {}; }
			SamplerID GetPresetSamplerId(SamplerPreset) const noexcept override { return {}; }
			uint32_t GetSamplerIndex(SamplerPreset) const noexcept override { return 1; }
			uint32_t GetSamplerIndex(const SamplerID&) const noexcept override { return 1; }
			uint32_t ResolveSamplerIndex(SamplerID, SamplerPreset) const noexcept override
			{
				++m_ResolveCount;
				return 1;
			}

			mutable uint32_t m_ResolveCount = 0;
		} samplers;

		using MaterialTable = RenderMaterialFrameCache::MaterialTable;
		constexpr uint32_t TextureSlotCount = 9;
		constexpr uint32_t InstanceCount = 2048;
		MaterialTable table(4, 3);
		MaterialProperties material;
		material.m_BaseColorBinding.m_TextureId = TextureID{ 70 };
		material.m_NormalBinding.m_TextureId = TextureID{ 71 };
		material.m_RoughnessFactor = 0.4f;
		const auto key = RenderMaterialKey::FromAsset(MaterialID{ 100 });
		const auto runtimeKey = RenderMaterialKey::FromRuntime(RuntimeMaterialKey{ 100 });
		uint32_t materialIndex = MaterialTable::InvalidSlot;
		table.BeginUpdate();
		{
			RenderMaterialFrameCache cache(table, textures, samplers);
			const auto first = cache.Resolve(key, material);
			materialIndex = first.m_Index;
			bool shared = first.m_Index != MaterialTable::InvalidSlot && !first.m_KeyCollision;
			for (uint32_t instance = 0; instance < InstanceCount; ++instance)
			{
				const auto resolved = cache.Resolve(key, material);
				shared &= resolved.m_Index == first.m_Index && !resolved.m_KeyCollision &&
					resolved.m_Flags == first.m_Flags && resolved.m_AlphaMode == first.m_AlphaMode;
			}
			context.Check(shared && table.GetLiveCount() == 1 &&
				textures.m_UsageCount == TextureSlotCount &&
				textures.m_ResolveCount == TextureSlotCount && samplers.m_ResolveCount == TextureSlotCount,
				"Thousands of shared mesh instances encode and mark material textures once per scene build");
			const auto original = table.GetData({ materialIndex, 1 })[0];
			context.Check(original.BaseColorBinding.TextureSamplerBinding.TextureIndex == 170 &&
				original.NormalBinding.TextureSamplerBinding.TextureIndex == 171 &&
				original.NormalBinding.TextureSamplerBinding.SamplerIndex == 1 &&
				original.RoughnessFactor == 0.4f,
				"Frame material preparation uses the production encoder and published binding indices");

			MaterialProperties runtimeMaterial = material;
			runtimeMaterial.m_RoughnessFactor = 0.7f;
			const auto runtime = cache.Resolve(runtimeKey, runtimeMaterial);
			context.Check(runtime.m_Index != first.m_Index && !runtime.m_KeyCollision &&
				table.GetLiveCount() == 2 && textures.m_ResolveCount == 2 * TextureSlotCount,
				"Asset and runtime material keys with the same value retain distinct GPU slots");

			MaterialProperties identical = material;
			bool equivalent = true;
			for (uint32_t instance = 0; instance < InstanceCount; ++instance)
			{
				const auto resolved = cache.Resolve(key, identical);
				equivalent &= resolved.m_Index == first.m_Index && !resolved.m_KeyCollision;
			}
			context.Check(equivalent && textures.m_UsageCount == 3 * TextureSlotCount &&
				textures.m_ResolveCount == 3 * TextureSlotCount && samplers.m_ResolveCount == 3 * TextureSlotCount,
				"Distinct equivalent material objects sharing a key are checked once and reuse the first slot");

			MaterialProperties conflicting = material;
			conflicting.m_RoughnessFactor = 0.9f;
			conflicting.m_Flags = MaterialFlags::DoubleSided;
			conflicting.m_AlphaMode = AlphaMode::Blend;
			const auto collision = cache.Resolve(key, conflicting);
			const auto preserved = table.GetData({ materialIndex, 1 })[0];
			context.Check(collision.m_KeyCollision && collision.m_Index == first.m_Index &&
				collision.m_Flags == first.m_Flags && collision.m_AlphaMode == first.m_AlphaMode &&
				preserved.RoughnessFactor == original.RoughnessFactor && preserved.Flags == original.Flags &&
				preserved.AlphaMode == original.AlphaMode,
				"A conflicting material key reports the mismatch and preserves the first GPU data and draw metadata");
			bool repeatedCollision = false;
			for (uint32_t instance = 0; instance < InstanceCount; ++instance)
			{
				const auto resolved = cache.Resolve(key, conflicting);
				repeatedCollision |= resolved.m_KeyCollision || resolved.m_Index != first.m_Index;
			}
			context.Check(!repeatedCollision && textures.m_UsageCount == 4 * TextureSlotCount &&
				textures.m_ResolveCount == 4 * TextureSlotCount && samplers.m_ResolveCount == 4 * TextureSlotCount,
				"Repeated references to a conflicting source do not re-encode or repeat its collision diagnostic");
			const auto runtimeCollision = cache.Resolve(runtimeKey, conflicting);
			context.Check(runtimeCollision.m_KeyCollision && runtimeCollision.m_Index == runtime.m_Index &&
				textures.m_ResolveCount == 5 * TextureSlotCount,
				"Conflict deduplication includes the render key when one source appears under multiple keys");

			constexpr uint32_t AliasCount = 128;
			std::array<MaterialProperties, AliasCount> aliases;
			aliases.fill(material);
			bool aliasCollisions = false;
			for (uint32_t iteration = 0; iteration < 16; ++iteration)
			{
				for (const auto& alias : aliases)
				{
					const auto resolved = cache.Resolve(key, alias);
					aliasCollisions |= resolved.m_KeyCollision || resolved.m_Index != first.m_Index;
				}
			}
			context.Check(!aliasCollisions && table.GetLiveCount() == 2 &&
				textures.m_UsageCount == (5 + AliasCount) * TextureSlotCount &&
				textures.m_ResolveCount == (5 + AliasCount) * TextureSlotCount &&
				samplers.m_ResolveCount == (5 + AliasCount) * TextureSlotCount,
				"Many equivalent sources retain deduplication across cache growth and interleaved references");
		}
		table.EndUpdate();
		for (uint32_t buffer = 0; buffer < table.GetBufferCount(); ++buffer)
		{
			const auto ranges = table.BuildDirtyRanges(buffer);
			table.Commit(buffer, ranges);
		}

		material.m_RoughnessFactor = 0.2f;
		material.m_ClearcoatFactor = 0.8f;
		material.m_NormalBinding.m_UVOffset = Vector2(0.25f, 0.5f);
		material.m_Flags = MaterialFlags::DoubleSided;
		material.m_AlphaMode = AlphaMode::Mask;
		textures.m_Resident = false;
		const uint32_t previousUsage = textures.m_UsageCount;
		const uint32_t previousResolves = textures.m_ResolveCount;
		table.BeginUpdate();
		{
			RenderMaterialFrameCache cache(table, textures, samplers);
			const auto edited = cache.Resolve(key, material);
			for (uint32_t instance = 0; instance < InstanceCount; ++instance)
			{
				GGLAB_UNUSED(cache.Resolve(key, material));
			}
			const auto gpu = table.GetData({ edited.m_Index, 1 })[0];
			context.Check(edited.m_Index == materialIndex && !edited.m_KeyCollision &&
				edited.m_Flags == MaterialFlags::DoubleSided && edited.m_AlphaMode == AlphaMode::Mask &&
				gpu.RoughnessFactor == 0.2f && gpu.ClearcoatFactor == 0.8f &&
				gpu.NormalBinding.UVTransformU.m_Z == 0.25f && gpu.NormalBinding.UVTransformV.m_Z == 0.5f &&
				gpu.NormalBinding.TextureSamplerBinding.TextureIndex == static_cast<uint32_t>(ReservedTextureIDIndex::NormalFlat) &&
				textures.m_UsageCount == previousUsage + TextureSlotCount &&
				textures.m_ResolveCount == previousResolves + TextureSlotCount,
				"The next scene build refreshes edited factors, transforms, draw metadata and evicted texture bindings");
		}
		table.EndUpdate();
		bool allBuffersDirty = true;
		for (uint32_t buffer = 0; buffer < table.GetBufferCount(); ++buffer)
		{
			const auto ranges = table.BuildDirtyRanges(buffer);
			allBuffersDirty &= !ranges.empty();
			table.Commit(buffer, ranges);
		}
		context.Check(allBuffersDirty && table.GetLiveCount() == 1,
			"Changed prepared data reaches every frame buffer while disappeared material keys retire");

		textures.m_Resident = true;
		textures.m_DescriptorBase = 200;
		table.BeginUpdate();
		{
			RenderMaterialFrameCache cache(table, textures, samplers);
			const auto restored = cache.Resolve(key, material);
			const auto gpu = table.GetData({ restored.m_Index, 1 })[0];
			context.Check(restored.m_Index == materialIndex &&
				gpu.BaseColorBinding.TextureSamplerBinding.TextureIndex == 270 &&
				gpu.NormalBinding.TextureSamplerBinding.TextureIndex == 271 &&
				!table.BuildDirtyRanges(0).empty(),
				"A re-resident texture publishes its new descriptor on the next build without a material edit");
		}
		table.EndUpdate();
		const auto restoredRanges = table.BuildDirtyRanges(0);
		table.Commit(0, restoredRanges);
		const uint64_t restoredRevision = table.GetRevision(materialIndex);
		table.BeginUpdate();
		{
			RenderMaterialFrameCache cache(table, textures, samplers);
			GGLAB_UNUSED(cache.Resolve(key, material));
		}
		table.EndUpdate();
		context.Check(table.GetRevision(materialIndex) == restoredRevision && table.BuildDirtyRanges(0).empty(),
			"Refreshing an unchanged material preserves persistent table revisions and avoids redundant uploads");

		MaterialTable fullTable(1, 1);
		fullTable.BeginUpdate();
		const uint32_t beforeExhaustion = textures.m_ResolveCount;
		{
			RenderMaterialFrameCache cache(fullTable, textures, samplers);
			GGLAB_UNUSED(cache.Resolve(key, material));
			bool exhausted = true;
			for (uint32_t instance = 0; instance < InstanceCount; ++instance)
			{
				exhausted &= cache.Resolve(runtimeKey, material).m_Index == MaterialTable::InvalidSlot;
			}
			context.Check(exhausted && fullTable.GetLiveCount() == 1 &&
				textures.m_ResolveCount == beforeExhaustion + 2 * TextureSlotCount,
				"An exhausted material table retains the invalid slot and avoids repeated preparation attempts");
		}
		fullTable.EndUpdate();

		bool diagnosticIds = true;
		for (uint32_t id = 0; id < 25; ++id)
		{
			diagnosticIds &= IsMaterialDiagnosticView(static_cast<MaterialDebugView>(id)) == ((id >= 1 && id <= 15) || (id >= 19 && id <= 22));
		}
		context.Check(diagnosticIds && !IsMaterialDiagnosticView(static_cast<MaterialDebugView>(255)),
			"Only parameter diagnostic IDs request attachments; Lit, UnfilteredLit and reserved IDs do not");
		MaterialTable diagnosticTable(2, 1);
		material.m_DebugView = MaterialDebugView::Normal;
		diagnosticTable.BeginUpdate();
		{
			RenderMaterialFrameCache cache(diagnosticTable, textures, samplers);
			const auto first = cache.Resolve(key, material);
			MaterialProperties conflicting = material;
			conflicting.m_DebugView = MaterialDebugView::UnfilteredLit;
			const auto collision = cache.Resolve(key, conflicting);
			context.Check(first.m_HasDiagnosticView && collision.m_HasDiagnosticView && collision.m_KeyCollision,
				"Diagnostic demand follows the first uploaded material when an alternate source collides");
		}
		diagnosticTable.EndUpdate();
		material.m_DebugView = MaterialDebugView::UnfilteredLit;
		diagnosticTable.BeginUpdate();
		{
			RenderMaterialFrameCache cache(diagnosticTable, textures, samplers);
			context.Check(!cache.Resolve(key, material).m_HasDiagnosticView,
				"Returning to UnfilteredLit removes diagnostic demand on the next frame");
		}
		diagnosticTable.EndUpdate();
		GraphicsPhysicalPipelineKey diagnosticRecipe{};
		diagnosticRecipe.m_BlendPreset = BlendPreset::AlphaBlendAllTargets;
		const auto diagnosticBlend = BuildRHIGraphicsPipelineDesc(diagnosticRecipe).m_Blend;
		bool opacityBlending = true;
		for (const auto& target : diagnosticBlend.m_RenderTargets)
		{
			opacityBlending &= target.m_BlendEnable && target.m_SrcColor == RHIBlendFactor::SrcAlpha &&
				target.m_DstColor == RHIBlendFactor::OneMinusSrcAlpha && target.m_SrcAlpha == RHIBlendFactor::One &&
				target.m_DstAlpha == RHIBlendFactor::OneMinusSrcAlpha;
		}
		context.Check(opacityBlending,
			"Transparent diagnostic color and coverage use surface opacity on every render target");
		for (const bool diagnostics : {false, true})
		{
			struct FixturePassData {};
			RenderGraph graph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			graph.AddPass<FixturePassData>("MaterialDiagnostics.Setup",
				[diagnostics](RenderGraph::RGBuilder& builder, FixturePassData&)
				{
					auto& targets = builder.GetBlackboard().GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
					RHITextureDesc desc{};
					desc.m_Extent = { 16, 16, 1 };
					desc.m_Format = RHIFormat::R16G16B16A16Float;
					targets.m_SceneColor = builder.CreateTexture("MaterialDiagnostics.Scene", desc);
					if (diagnostics)
					{
						targets.m_MaterialDiagnosticColor = builder.CreateTexture("MaterialDiagnostics.Color", desc);
						targets.m_MaterialDiagnosticLighting = builder.CreateTexture("MaterialDiagnostics.Lighting", desc);
						desc.m_Format = RHIFormat::R16Float;
						targets.m_MaterialDiagnosticCoverage = builder.CreateTexture("MaterialDiagnostics.Coverage", desc);
						builder.WriteInPlace(targets.m_MaterialDiagnosticColor, RGTextureAccess::RenderTarget);
						builder.WriteInPlace(targets.m_MaterialDiagnosticCoverage, RGTextureAccess::RenderTarget);
						builder.WriteInPlace(targets.m_MaterialDiagnosticLighting, RGTextureAccess::RenderTarget);
					}
					// Seed a prior writer so the production clear must publish a new version.
					builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				});
			const auto original = graph.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
			const RenderScene scene{};
			const RenderFrameContext frame{ .m_RenderScene = scene };
			RenderPassClearViewTargets{}.AddPass(graph, frame, RenderServices{});
			const auto cleared = graph.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
			graph.AddPass<FixturePassData>("MaterialDiagnostics.DisplayRead",
				[diagnostics](RenderGraph::RGBuilder& builder, FixturePassData&)
				{
					const auto& targets = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
					builder.Read(targets.m_SceneColor, RGTextureAccess::Sample);
					if (diagnostics)
					{
						builder.Read(targets.m_MaterialDiagnosticColor, RGTextureAccess::Sample);
						builder.Read(targets.m_MaterialDiagnosticCoverage, RGTextureAccess::Sample);
						builder.Read(targets.m_MaterialDiagnosticLighting, RGTextureAccess::Sample);
					}
					builder.SideEffect();
				});
			const bool versionsPublished = cleared.m_SceneColor.GetVersion() > original.m_SceneColor.GetVersion() &&
				(!diagnostics || (cleared.m_MaterialDiagnosticColor.GetVersion() > original.m_MaterialDiagnosticColor.GetVersion() &&
					cleared.m_MaterialDiagnosticCoverage.GetVersion() > original.m_MaterialDiagnosticCoverage.GetVersion() &&
					cleared.m_MaterialDiagnosticLighting.GetVersion() > original.m_MaterialDiagnosticLighting.GetVersion()));
			context.Check(versionsPublished && graph.Compile(), diagnostics ?
				"Production clear publishes defined diagnostic color, coverage and lighting versions to display consumers" :
				"Production clear preserves the original scene-only graph when diagnostics are absent");
		}
	}

	void RunSceneTableUploadContractTests(SelfTestContext& context) noexcept
	{
		PersistentStructuredBufferTable<uint64_t, ObjectGPU> objectTable(2, 2);
		PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU> materialTable(2, 2);
		PersistentStructuredBufferTable<uint64_t, LightGPU> lightTable(2, 2);
		objectTable.BeginUpdate();
		materialTable.BeginUpdate();
		lightTable.BeginUpdate();
		const uint32_t objectSlot = objectTable.Upsert(1, {});
		GGLAB_UNUSED(materialTable.Upsert(RenderMaterialKey::FromAsset(MaterialID{ 1 }), {}));
		GGLAB_UNUSED(lightTable.Upsert(1, {}));
		objectTable.EndUpdate();
		materialTable.EndUpdate();
		lightTable.EndUpdate();

		auto ownedTransferContext = std::make_unique<RecordingTransferContext>();
		RecordingTransferContext& transferContext = *ownedTransferContext;
		TransferManager transferManager(std::move(ownedTransferContext));
		const RenderSceneBuilder::SceneTableUploadInfo uploadInfo{
			.m_TransferManager = transferManager,
			.m_ObjectTable = objectTable,
			.m_MaterialTable = materialTable,
			.m_LightTable = lightTable,
			.m_ObjectBuffer = RHIBufferHandle{ 1, 1 },
			.m_MaterialBuffer = RHIBufferHandle{ 2, 1 },
			.m_LightBuffer = RHIBufferHandle{ 3, 1 },
			.m_FrameSlotIndex = 0,
		};
		const auto allTablesDirty = [&](uint32_t frameSlot) noexcept
			{
				return !objectTable.BuildDirtyRanges(frameSlot).empty() &&
					!materialTable.BuildDirtyRanges(frameSlot).empty() &&
					!lightTable.BuildDirtyRangesIncludingFreeSlots(frameSlot).empty();
			};

		// The light upload is recorded last; failing it poisons the shared batch.
		transferContext.m_SucceedingUploadBufferCount = 2;
		const auto lightFailure = RenderSceneBuilder::UploadSceneTables(uploadInfo);
		context.Check(!lightFailure.IsCommitted() && lightFailure.m_ObjectsRecorded &&
			lightFailure.m_MaterialsRecorded && !lightFailure.m_LightsRecorded &&
			!lightFailure.m_Completion.IsValid() && transferContext.m_SubmitCallCount == 0 &&
			transferContext.m_AbortCallCount == 1 && allTablesDirty(0) &&
			objectTable.GetUploadedRevision(0, objectSlot) == 0,
			"A poisoned scene-table batch advances no table, so every dirty range is reported next build");

		transferContext.m_SucceedingUploadBufferCount = std::numeric_limits<uint32_t>::max();
		transferContext.m_FailSubmit = true;
		const auto submitFailure = RenderSceneBuilder::UploadSceneTables(uploadInfo);
		context.Check(!submitFailure.IsCommitted() && submitFailure.m_ObjectsRecorded &&
			submitFailure.m_MaterialsRecorded && submitFailure.m_LightsRecorded &&
			!submitFailure.m_Submitted && !submitFailure.m_Completion.IsValid() &&
			transferContext.m_SubmitCallCount == 1 && allTablesDirty(0),
			"A failed scene-table submission advances no table, so every dirty range is reported next build");

		transferContext.m_FailSubmit = false;
		const auto success = RenderSceneBuilder::UploadSceneTables(uploadInfo);
		context.Check(success.IsCommitted() && success.m_Completion.IsValid() &&
			objectTable.BuildDirtyRanges(0).empty() && materialTable.BuildDirtyRanges(0).empty() &&
			lightTable.BuildDirtyRangesIncludingFreeSlots(0).empty() && allTablesDirty(1),
			"A submitted scene-table batch commits every table for its frame slot only");
	}

	void RunMaterialBaselineContractTests(SelfTestContext& context) noexcept
	{
		constexpr double Pi = std::numbers::pi_v<double>;
		constexpr uint32_t PolarSamples = 512;
		// Independent midpoint integration of the current direct GGX BRDF under
		// uniform unit radiance. The result is scene-linear directional albedo;
		// no exposure, IBL prefilter or tone map participates.
		auto integrate = [=](double perceptualRoughness, double f0) noexcept
		{
			const double alpha = perceptualRoughness * perceptualRoughness;
			const double alphaSquared = alpha * alpha;
			double total = 0.0;
			for (uint32_t polar = 0; polar < PolarSamples; ++polar)
			{
				const double theta = (static_cast<double>(polar) + 0.5) * (0.5 * Pi / PolarSamples);
				const double noL = std::cos(theta);
				const double noH = std::sqrt((1.0 + noL) * 0.5);
				const double voH = noH;
				const double denominator = noH * noH * (alphaSquared - 1.0) + 1.0;
				const double distribution = alphaSquared / (Pi * denominator * denominator);
				const double ggxL = std::sqrt(noL * noL * (1.0 - alphaSquared) + alphaSquared);
				const double ggxV = noL;
				const double visibility = 0.5 / (ggxL + ggxV);
				const double fresnelEdge = std::pow(1.0 - voH, 5.0);
				const double fresnel = f0 * (1.0 - fresnelEdge) + fresnelEdge;
				const double brdf = distribution * visibility * fresnel;
				const double solidAngle = std::sin(theta) * (0.5 * Pi / PolarSamples) *
					(2.0 * Pi); // isotropic azimuth integrates analytically
				total += brdf * noL * solidAngle;
			}
			return total;
		};

		const double defaultIorF0 = std::pow((1.5 - 1.0) / (1.5 + 1.0), 2.0);
		context.Check(std::abs(defaultIorF0 - 0.04) < 1e-12 &&
			std::abs(std::pow((2.0 - 1.0) / (2.0 + 1.0), 2.0) - 1.0 / 9.0) < 1e-12,
			"IOR 1.5 preserves the legacy dielectric F0 = 0.04 reference");
		context.Check(std::abs(0.045 * 0.045 - 0.002025) < 1e-12,
			"Minimum perceptual roughness converts to GGX alpha before evaluation");

		const double smooth = integrate(0.25, 1.0);
		const double medium = integrate(0.5, 1.0);
		const double rough = integrate(1.0, 1.0);
		context.Check(std::isfinite(smooth) && std::isfinite(medium) && std::isfinite(rough) &&
			smooth > medium && medium > rough && smooth < 1.01 && smooth > 0.9 &&
			std::abs(rough - (1.0 - std::log(2.0))) < 0.001,
			"Single-scattering GGX white furnace loses energy with roughness (512 polar midpoints)");

		const double lutB = integrate(1.0, 0.0);
		const double lutA = rough - lutB;
		const double dielectric = integrate(1.0, defaultIorF0);
		const double iorOneF0 = 0.0;
		const double iorTwoF0 = 1.0 / 9.0;
		context.Check(integrate(1.0, iorOneF0) < dielectric &&
			dielectric < integrate(1.0, iorTwoF0) &&
			std::abs(iorOneF0 * lutA + lutB - integrate(1.0, iorOneF0)) < 1e-6 &&
			std::abs(iorTwoF0 * lutA + lutB - integrate(1.0, iorTwoF0)) < 1e-6 &&
			std::lerp(iorOneF0, 0.8, 1.0) == std::lerp(iorTwoF0, 0.8, 1.0),
			"IOR changes direct and split-sum dielectric response while metallic F0 remains base color");
		context.Check(std::abs(lutA - 0.30682) < 0.001 &&
			std::abs(lutB - 0.000033615) < 0.000002 &&
			std::abs(defaultIorF0 * lutA + lutB - dielectric) < 1e-6,
			"BRDF LUT A integrates (1-Fc), B integrates Fc, and F0*A+B reconstructs the direct reference");
	}

	void RunGGXEnergyCompensationContractTests(SelfTestContext& context) noexcept
	{
		constexpr double Pi = std::numbers::pi_v<double>;
		constexpr uint32_t PolarSamples = 192;
		constexpr uint32_t AzimuthSamples = 256;
		// Integrate the direct BRDF independently of the importance-sampled GPU LUT.
		auto directionalAlbedo = [=](double noV, double roughness, double f0) noexcept
		{
			const double alpha = roughness * roughness;
			const double alphaSquared = alpha * alpha;
			const double viewX = std::sqrt(1.0 - noV * noV);
			double total = 0.0;
			for (uint32_t polar = 0; polar < PolarSamples; ++polar)
			{
				const double theta = (static_cast<double>(polar) + 0.5) * (0.5 * Pi / PolarSamples);
				const double noL = std::cos(theta);
				const double sinTheta = std::sin(theta);
				const double ggxL = noV * std::sqrt(noL * noL * (1.0 - alphaSquared) + alphaSquared);
				const double ggxV = noL * std::sqrt(noV * noV * (1.0 - alphaSquared) + alphaSquared);
				const double visibility = 0.5 / (ggxL + ggxV);
				for (uint32_t azimuth = 0; azimuth < AzimuthSamples; ++azimuth)
				{
					const double phi = (static_cast<double>(azimuth) + 0.5) * (2.0 * Pi / AzimuthSamples);
					const double lightX = sinTheta * std::cos(phi);
					const double halfX = lightX + viewX;
					const double halfY = sinTheta * std::sin(phi);
					const double halfZ = noL + noV;
					const double halfLength = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
					const double noH = halfZ / halfLength;
					const double voH = (viewX * halfX + noV * halfZ) / halfLength;
					const double denominator = noH * noH * (alphaSquared - 1.0) + 1.0;
					const double distribution = alphaSquared / (Pi * denominator * denominator);
					const double fresnelEdge = std::pow(1.0 - voH, 5.0);
					const double fresnel = f0 * (1.0 - fresnelEdge) + fresnelEdge;
					total += distribution * visibility * fresnel * noL * sinTheta;
				}
			}
			return total * (0.5 * Pi / PolarSamples) * (2.0 * Pi / AzimuthSamples);
		};

		bool whiteFurnace = true;
		bool dielectricAndConductor = true;
		bool grazingBounded = true;
		for (const double noV : { 0.05, 0.5, 1.0 })
		{
			for (const double roughness : { 0.25, 0.5, 1.0 })
			{
				const double f0Zero = directionalAlbedo(noV, roughness, 0.0);
				const double f0One = directionalAlbedo(noV, roughness, 1.0);
				const double lutA = f0One - f0Zero;
				const double lutB = f0Zero;
				const double gainAtOne = 1.0 / std::clamp(lutA + lutB, 1.0e-4, 1.0);
				whiteFurnace &= std::isfinite(gainAtOne) && f0One > 0.1 && f0One <= 1.01 &&
					std::abs(f0One * gainAtOne - 1.0) < 0.01;
				for (const double f0 : { 0.04, 0.08, 0.7 })
				{
					const double direct = directionalAlbedo(noV, roughness, f0);
					const double gain = 1.0 + f0 * (gainAtOne - 1.0);
					const double specular = std::min(1.0, (f0 * lutA + lutB) * gain);
					const double diffuse = 1.0 - specular;
					dielectricAndConductor &= std::abs(direct - (f0 * lutA + lutB)) < 1.0e-6 &&
						gain >= 1.0 && specular >= 0.0 && diffuse >= 0.0 &&
						std::abs(specular + diffuse - 1.0) < 1.0e-12;
					grazingBounded &= std::isfinite(gain) && std::isfinite(specular) &&
						direct * gain <= 1.01;
				}
			}
		}
		context.Check(whiteFurnace,
			"Compensated F0=1 GGX returns unit directional energy across view and roughness sweeps");
		context.Check(dielectricAndConductor,
			"Dielectric and conductor Fresnel terms retain bounded diffuse/specular partition");
		context.Check(grazingBounded,
			"GGX energy gain stays finite without grazing-angle energy explosion");
		const double roughNormalGain = 1.0 / directionalAlbedo(1.0, 1.0, 1.0);
		context.Check(roughNormalGain > 3.2 && roughNormalGain < 3.3,
			"Rough F0=1 GGX receives the expected measurable white-furnace recovery");
		// A uniform unit environment gives a white Lambert base one unit of
		// directional energy. The coat returns its integrated GGX reflectance;
		// the remainder crosses the interface in both directions.
		constexpr double meanIncidentCoatFresnel = 0.04 + 0.96 / 21.0;
		bool coatLayerBounded = true;
		bool coatOffPreservesBase = true;
		for (const double noV : { 0.05, 0.5, 1.0 })
		{
			for (const double roughness : { 0.25, 0.5, 1.0 })
			{
				const double singleScatter = directionalAlbedo(noV, roughness, 0.04);
				const double unityScatter = directionalAlbedo(noV, roughness, 1.0);
				const double gain = 1.0 + 0.04 *
					(1.0 / std::clamp(unityScatter, 1.0e-4, 1.0) - 1.0);
				const double coatReflectance = std::clamp(singleScatter * gain, 0.0, 1.0);
				for (const double factor : { 0.0, 0.5, 1.0 })
				{
					const double base = (1.0 - factor * coatReflectance) *
						(1.0 - factor * meanIncidentCoatFresnel);
					const double layered = base + factor * coatReflectance;
					coatLayerBounded &= std::isfinite(layered) && layered >= 0.0 &&
						layered <= 1.0001 && base <= 1.0;
					if (factor == 0.0)
					{
						coatOffPreservesBase &= layered == 1.0;
					}
				}
			}
		}
		context.Check(coatLayerBounded,
			"Clearcoat white-furnace sweep keeps the reflected coat and transmitted base within unit energy");
		context.Check(coatOffPreservesBase,
			"Zero clearcoat factor reproduces the white Lambert base reference");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingMaterialsSelfTests(SelfTestContext& context) noexcept
	{
		RunMaterialDiagnosticPrewarmTests(context);
		RunMaterialBaselineContractTests(context);
		RunMaterialFrameCacheContractTests(context);
		RunSceneTableUploadContractTests(context);
		RunMaterialDiagnosticDemandTests(context);
		RunGGXEnergyCompensationContractTests(context);
	}
}
