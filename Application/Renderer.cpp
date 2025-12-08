#include "Precompiled.h"
#include "Renderer.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12RootSignature.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12SwapChain.h"
#include "DX12CommandAllocator.h"
#include "DX12ConstantBuffer.h"
#include "RenderGraph.h"
#include "RGGpuResourceAllocator.h"
#include "AssetManager.h"
#include "Components.h"
#include "Camera.h"
#include "RenderPassForwardPBR.h"
#include "MathUtils.h"

namespace gglab
{
	Renderer::Renderer() noexcept
	{
		m_Device = std::make_unique<DX12Device>();
		m_Device->Initialize();
	}

	void Renderer::Initialize() noexcept
	{
		m_RGGpuAllocator = std::make_unique<RGGpuResourceAllocator>(m_Device.get());

		DX12ViewCache::DescriptorsAllocatorArray descriptorAllocators =
		{
			m_Device->GetRtvDescriptorAllocator(),
			m_Device->GetDsvDescriptorAllocator(),
			m_Device->GetCbvSrvUavDescriptorAllocator()
		};
		m_ViewCache = std::make_unique<DX12ViewCache>(m_Device.get(), descriptorAllocators);
		m_PSOCache = std::make_unique<DX12PSOCache>(m_Device.get(), std::make_unique<StreamPSOCreator>());
		m_RootSignatureCache = std::make_unique<DX12RootSignatureCache>(m_Device.get());
		m_RenderPassRecipeRegistry = std::make_unique<RenderPassRecipeRegistry>(Application::GetInstance()->GetShaderManager());

		CreateCamera();
		CreateCommonRootSignature();
		InitializeGpuBuffers();

		m_IsInitialized = true;
	}

	void Renderer::Update() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_Camera->Update();

		UpdateGpuBuffers();
	}

	void Renderer::Render() noexcept
	{
		if (m_IsInitialized == false)
		{
			return;
		}

		auto swapChain = m_Device->GetSwapChain();
		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto commandAllocatorPool = m_Device->GetGraphicsCommandAllocatorPool();
		auto commandList = m_Device->GetGraphicsCommandList(backBufferIndex);
		auto commandQueue = m_Device->GetGraphicsCommandQueue();

		// Wait Structured Buffer upload
		if (m_UploadFencePoint.IsValid())
		{
			if (!m_UploadFencePoint.IsCompleted())
			{
				commandQueue->Wait(m_UploadFencePoint);
			}
			m_UploadFencePoint = {};
		}

		swapChain->WaitFrameCompletion();

		auto commandAllocator = commandAllocatorPool->RequestCommandAllocator();
		commandList->Begin(commandAllocator);

		// Render Graph
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_GpuResourceAllocator = m_RGGpuAllocator.get();
		rgCreateInfo.m_ViewCache = m_ViewCache.get();
		RenderGraph rg(rgCreateInfo);

		RenderPassForwardPBR forwardPbrPass{};
		forwardPbrPass.AddPass(rg);

		rg.Compile();

		RGExecuteContext executeContext = {};
		executeContext.m_GraphicsCommandList = commandList;
		rg.Execute(executeContext);

		commandList->End();

		const DX12CommandList* commandLists[] = { commandList };
		auto fencePoint = commandQueue->Execute(commandLists);

		rg.Retire(fencePoint);

		m_RGGpuAllocator->Tick();

		commandAllocatorPool->RecycleCommandAllocator(commandAllocator, fencePoint);

		swapChain->UpdateFrameSyncObject(std::move(fencePoint));
		swapChain->Present();

	}

	void Renderer::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		m_IsInitialized = false;

		m_Device->FlushGPU();
	}

	DX12RootSignature* Renderer::GetCommonRootSignature() const noexcept
	{
		return m_RootSignatureCache->GetDX12RootSignature(m_CommonRootSignatureId);
	}

	void Renderer::CreateCommonRootSignature() noexcept
	{
		CD3DX12_DESCRIPTOR_RANGE1 range = {};
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			1, // numDescriptors: t0
			0, // baseShaderRegister
			0, // registerSpace
			D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount)] = {};

		// b0: FrameCB
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::FrameCB)].InitAsConstantBufferView(0);

		// b1: ObjectCB, num32BitValues: 1, shaderRegister: b1
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ObjectCB)].InitAsConstants(1, 1);

		// t1: ObjectSB
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB)].InitAsShaderResourceView(1);

		// t2: materialSB
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB)].InitAsShaderResourceView(2);

		// t0: BaseColorTex descriptor table
		rootParameters[static_cast<uint32_t>(CommonRSRootParamIndex::TextureDescriptorTable)]
			.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

		constexpr uint32_t StaticSamplerCount = 1;
		CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSamplerCount] = {};
		staticSamplers[0].Init(
			0,	// register s0
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Init_1_1(
			static_cast<uint32_t>(CommonRSRootParamIndex::RootParamCount), rootParameters,
			StaticSamplerCount, staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

		auto [id, rootSig] = m_RootSignatureCache->GetOrCreate(rootSignatureDesc);
		m_CommonRootSignatureId = id;
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Initialize global constant buffer
		{
			const auto constantBufferFrames = m_Device->GetBufferCount();
			m_FrameCB = std::make_unique<DX12ConstantBuffer<FrameCBData>>(m_Device.get(), constantBufferFrames);
		}

		// Initialize structured buffers objects and materials
		{
			m_ObjectSB.m_StructuredBuffer = std::make_unique<DX12RingStructuredBuffer<ObjectGPU>>(m_Device.get(), MaxObjectCapacity); // max element count
			m_MaterialSB.m_StructuredBuffer = std::make_unique<DX12RingStructuredBuffer<MaterialGPU>>(m_Device.get(), MaxMaterialCapacity); // max element count
		}
	}

	void Renderer::CreateCamera() noexcept
	{
		auto app = Application::GetInstance();

		Camera::Info info = {};
		info.m_Position = Vector3(-100.0f, 128.0f, 30.0f);
		info.m_Width = app->GetWindowWidth();
		info.m_Height = app->GetWindowHeight();
		info.m_Near = 0.1f;
		info.m_Far = 10000.0f;
		info.m_Fov = 60.0f;

		m_Camera = std::make_unique<Camera>(info);
	}

	void Renderer::UpdateGpuBuffers() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		GGLAB_ASSERT(m_ObjectSB.m_StructuredBuffer);
		GGLAB_ASSERT(m_MaterialSB.m_StructuredBuffer);

		auto* app = Application::GetInstance();
		auto& registry = app->GetEnttRegistry();
		auto* transferManager = app->GetTransferManager();
		GGLAB_ASSERT(transferManager);

		// Reclaim RingBuffer
		{
			const DX12FencePoint completedFence = transferManager->Reclaim();
			if (completedFence.IsValid())
			{
				m_ObjectSB.m_StructuredBuffer->ReclaimCompleted(completedFence);
				m_MaterialSB.m_StructuredBuffer->ReclaimCompleted(completedFence);
			}
		}

		m_DrawItems.clear();

		// Update Object and Material Structured Buffer
		std::vector<ObjectGPU> objectData;
		std::vector<MaterialGPU> materialData;
		objectData.reserve(MaxObjectCapacity);
		materialData.reserve(MaxMaterialCapacity);

		std::unordered_map<MaterialId, uint32_t> materialIndexMap;

		registry.view<components::TransformComponent, components::ModelComponent>().each(
			[this, &objectData, &materialData, &materialIndexMap](auto entity,
				const components::TransformComponent& transformComp,
				const components::ModelComponent& modelComp)
			{
				auto* assetManager = Application::GetInstance()->GetAssetManager();

				const auto* model = assetManager->GetModel(modelComp.m_ModelId);
				if (!model)
				{
					GGLAB_LOG_GRAPHICS_WARN("Entity has no model.");
					return;
				}

				Matrix world =
					Matrix::CreateScale(transformComp.m_Scale) *
					Matrix::CreateFromQuaternion(transformComp.m_Rotation) *
					Matrix::CreateTranslation(transformComp.m_Position);

				Matrix normalMat = world;
				normalMat.Translation(Vector3::Zero);
				normalMat = normalMat.Invert().Transpose();

				for (const ModelMesh& modelMesh : model->m_MeshInstance)
				{
					const Mesh* mesh = assetManager->GetMesh(modelMesh.m_MeshId);
					if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
					{
						continue;
					}

					uint32_t materialIndex = 0;
					auto iter = materialIndexMap.find(modelMesh.m_MaterialId);
					if (iter == materialIndexMap.end())
					{
						const Material* material = assetManager->GetMaterial(modelMesh.m_MaterialId);
						if (!material)
						{
							GGLAB_LOG_GRAPHICS_WARN("Mesh has invalid MaterialId.");
							continue;
						}

						materialIndex = static_cast<uint32_t>(materialData.size());
						materialIndexMap.emplace(modelMesh.m_MaterialId, materialIndex);

						MaterialGPU materialGpu{};
						materialGpu.BaseColorFactor = material->m_BaseColor;
						materialGpu.MetallicFactor = material->m_MetallicFactor;
						materialGpu.RoughnessFactor = material->m_RoughnessFactor;
						materialGpu.NormalScale = material->m_NormalScale;
						materialGpu.OcclusionStrength = material->m_OcclusionStrength;
						materialGpu.EmissiveColorFactor = material->m_EmissiveColor;

						materialGpu.BaseColorTexIndex = assetManager->GetTextureDescriptorIndex(material->m_BaseColorTex);
						materialGpu.MetallicRoughnessTexIndex = assetManager->GetTextureDescriptorIndex(material->m_MetallicRoughnessTex);
						materialGpu.NormalTexIndex = assetManager->GetTextureDescriptorIndex(material->m_NormalTex);
						materialGpu.OcclusionTexIndex = assetManager->GetTextureDescriptorIndex(material->m_OcclusionTex);
						materialGpu.EmissiveTexIndex = assetManager->GetTextureDescriptorIndex(material->m_EmissiveTex);

						materialData.push_back(materialGpu);

					}
					else
					{
						materialIndex = iter->second;
					}

					uint32_t objectOffset = static_cast<uint32_t>(objectData.size());

					ObjectGPU objectGpu{};
					objectGpu.ModelMat = world;
					objectGpu.NormalMat = normalMat;
					objectGpu.MaterialIndex = materialIndex;
					objectData.push_back(objectGpu);

					DrawItem drawItem{};
					drawItem.m_MeshId = modelMesh.m_MeshId;
					drawItem.m_MaterialId = modelMesh.m_MaterialId;
					drawItem.m_ObjectOffset = objectOffset;
					m_DrawItems.push_back(drawItem);

				}
			});

		DX12FencePoint uploadFencePoint{};

		// Upload structured buffer to GPU
		if (!objectData.empty() || !materialData.empty())
		{
			auto batch = transferManager->BeginBatch();

			if (!objectData.empty())
			{
				std::span<const ObjectGPU> objectSpan(objectData.data(), objectData.size());
				m_ObjectSB.m_BufferRange = batch.StageStructuredBufferWrite<ObjectGPU>(*m_ObjectSB.m_StructuredBuffer, objectSpan);
			}
			else
			{
				m_ObjectSB.m_BufferRange = {};
			}

			if (!materialData.empty())
			{
				std::span<const MaterialGPU> materialSpan(materialData.data(), materialData.size());
				m_MaterialSB.m_BufferRange = batch.StageStructuredBufferWrite<MaterialGPU>(*m_MaterialSB.m_StructuredBuffer, materialSpan);
			}
			else
			{
				m_MaterialSB.m_BufferRange = {};
			}

			uploadFencePoint = batch.Submit(false);
		}
		else
		{
			m_ObjectSB.m_BufferRange = {};
			m_MaterialSB.m_BufferRange = {};
		}

		m_UploadFencePoint = uploadFencePoint;

		// Update FrameCB
		FrameCBData frameCB{};

		Matrix viewMatrix = m_Camera->GetViewMatrix();
		Matrix projMatrix = m_Camera->GetProjMatrix();

		frameCB.ViewMat = viewMatrix;
		frameCB.ProjMat = projMatrix;
		frameCB.CameraPos = utils::ToVector4(m_Camera->GetPosition(), 1.0f);
		frameCB.Exposure = 1.0f;	// TODO: camera exposure

		frameCB.ObjectBaseIndex = m_ObjectSB.m_BufferRange.m_FirstElementIndex;
		frameCB.MaterialBaseIndex = m_MaterialSB.m_BufferRange.m_FirstElementIndex;

		// MainLight data
		{
			LightGPU lightGpu{};
			auto lightView = registry.view<components::TransformComponent, components::LightComponent>();

			bool found = false;
			for (auto [entity, transComp, lightComp] : lightView.each())
			{
				lightGpu.Position = utils::ToVector4(transComp.m_Position, 1.0f);

				Matrix rotation = Matrix::CreateFromQuaternion(transComp.m_Rotation);
				Vector3 forward = Vector3::Transform(-Vector3::UnitZ, rotation);
				forward.Normalize();
				lightGpu.Direction = utils::ToVector4(forward, 1.0f);

				lightGpu.Color = lightComp.m_Color;
				lightGpu.Intensity = lightComp.m_Intensity;
				lightGpu.Range = lightComp.m_Range;
				lightGpu.SpotAngle = lightComp.m_SpotAngle;
				lightGpu.LightType = static_cast<uint32_t>(lightComp.m_Type);

				found = true;
				break;
			}

			// Dummy main light
			if (!found)
			{
				lightGpu.Direction = -Vector4::UnitY;
				lightGpu.Color = color::White;
				lightGpu.Intensity = 1.0f;
				lightGpu.LightType = static_cast<uint32_t>(LightType::Directional);
			}

			frameCB.MainLight = lightGpu;
		}

		const auto currentIndex = m_Device->GetSwapChain()->GetCurrentBackBufferIndex();
		m_FrameCB->Update(frameCB, currentIndex);
	}
}