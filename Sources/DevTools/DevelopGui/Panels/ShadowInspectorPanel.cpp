#include "Core/Precompiled.h"
#include "Core/Components.h"
#include "Core/World.h"
#include "DevTools/EnumText/EnumTextDXGI.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/Panels/ShadowInspectorPanel.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGShadowResources.h"
#include "Graphics/RenderResourceRegistry.h"
#include "Graphics/RenderView.h"

namespace gglab
{
	namespace
	{
		struct ShadowInspectorPanelState
		{
			float m_PreviewSize = 384.0f;
			bool m_FlipPreviewY = false;
			bool m_ShowMatrices = false;
		};

		struct DirectionalLightBinding
		{
			components::TransformComponent* m_Transform = nullptr;
			components::LightComponent* m_Light = nullptr;
			DirectionalShadowSettings* m_ShadowSettings = nullptr;
			Vector3 m_Direction = -Vector3::UnitY;
		};

		static ImTextureID ToImGuiTextureID(D3D12_GPU_DESCRIPTOR_HANDLE handle) noexcept
		{
			return static_cast<ImTextureID>(handle.ptr);
		}

		static D3D12_GPU_DESCRIPTOR_HANDLE ToGpuDescriptorHandle(
			Renderer* renderer,
			RHIDescriptorHandle descriptor) noexcept
		{
			if (!renderer ||
				!descriptor.IsValid() ||
				descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
			{
				return {};
			}

			auto* descriptorManager = renderer->GetDescriptorManager();
			if (!descriptorManager)
			{
				return {};
			}

			auto* heap = descriptorManager->GetHeap(DX12DescriptorManager::HeapType::CbvSrvUav);
			return heap ? heap->GpuHandleAt(descriptor.m_Index) : D3D12_GPU_DESCRIPTOR_HANDLE{};
		}

		static void DrawMatrix4x4(const char* label, const Matrix& mat) noexcept
		{
			if (!ImGui::TreeNode(label))
			{
				return;
			}

			Matrix fmat{};
			DirectX::XMStoreFloat4x4(&fmat, mat);

			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._11, fmat._12, fmat._13, fmat._14);
			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._21, fmat._22, fmat._23, fmat._24);
			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._31, fmat._32, fmat._33, fmat._34);
			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._41, fmat._42, fmat._43, fmat._44);

			ImGui::TreePop();
		}

		static Vector3 DirectionFromTransform(const components::TransformComponent& transform) noexcept
		{
			Vector3 direction = Vector3::Transform(-Vector3::UnitZ, Matrix::CreateFromQuaternion(transform.m_Rotation));
			if (direction.LengthSquared() <= 1.0e-8f)
			{
				return -Vector3::UnitY;
			}
			direction.Normalize();
			return direction;
		}

		static DirectionalLightBinding FindDirectionalLight(World* world) noexcept
		{
			DirectionalLightBinding binding{};
			if (!world)
			{
				return binding;
			}

			auto& registry = world->GetRegistry();
			auto lightView = registry.view<components::TransformComponent, components::LightComponent>();
			for (auto [entity, transform, light] : lightView.each())
			{
				GGLAB_UNUSED(entity);
				if (light.m_Type != LightType::Directional)
				{
					continue;
				}

				binding.m_Transform = &transform;
				binding.m_Light = &light;
				binding.m_Direction = DirectionFromTransform(transform);
				if (light.m_DirectionalShadowSettings)
				{
					binding.m_ShadowSettings = &*light.m_DirectionalShadowSettings;
				}
				return binding;
			}

			return binding;
		}

		static const RenderView* FindRenderView(std::span<RenderView> views, RenderViewID viewId) noexcept
		{
			const auto index = utils::ToIndex(viewId);
			if (index >= views.size())
			{
				return nullptr;
			}
			return &views[index];
		}

		static void DrawLightControl(DevelopGuiContext& context) noexcept
		{
			ImGui::SeparatorText("Light Control");

			DirectionalLightBinding lightBinding = FindDirectionalLight(context.m_World);
			if (!lightBinding.m_Transform || !lightBinding.m_Light)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Directional light is not found.");
				return;
			}

			float direction[3] = {
				lightBinding.m_Direction.x,
				lightBinding.m_Direction.y,
				lightBinding.m_Direction.z,
			};

			if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f"))
			{
				Vector3 newDirection(direction[0], direction[1], direction[2]);
				if (newDirection.LengthSquared() > 1.0e-8f)
				{
					newDirection.Normalize();
					Quaternion::FromToRotation(-Vector3::UnitZ, newDirection, lightBinding.m_Transform->m_Rotation);
				}
			}

			float color[3] = {
				lightBinding.m_Light->m_Color.x,
				lightBinding.m_Light->m_Color.y,
				lightBinding.m_Light->m_Color.z,
			};
			if (ImGui::ColorEdit3("Color", color))
			{
				lightBinding.m_Light->m_Color.x = color[0];
				lightBinding.m_Light->m_Color.y = color[1];
				lightBinding.m_Light->m_Color.z = color[2];
			}

			ImGui::DragFloat("Intensity", &lightBinding.m_Light->m_Intensity, 0.01f, 0.0f, 100.0f, "%.3f");
		}

		static void DrawDirectionalShadowSettings(DirectionalShadowSettings& settings) noexcept
		{
			ImGui::SeparatorText("General");
			ImGui::Checkbox("Enable", &settings.m_Enable);
			ImGui::SameLine();
			ImGui::Checkbox("3x3 PCF", &settings.m_EnablePCF);

			int shadowMapSize = static_cast<int>(settings.m_ShadowMapSize);
			if (ImGui::SliderInt("Shadow Map Size", &shadowMapSize, 256, 8192))
			{
				settings.m_ShadowMapSize = static_cast<uint32_t>(std::max(shadowMapSize, 1));
			}

			ImGui::SeparatorText("Projection");
			ImGui::DragFloat("Max Shadow Distance", &settings.m_MaxShadowDistance, 1.0f, 1.0f, 10000.0f, "%.1f");
			ImGui::DragFloat("Caster Extrusion Distance", &settings.m_CasterExtrusionDistance, 1.0f, 0.0f, 10000.0f, "%.1f");
			ImGui::DragFloat("Ortho Padding", &settings.m_OrthoPadding, 0.1f, 0.0f, 1000.0f, "%.2f");
			ImGui::DragFloat("Depth Padding", &settings.m_DepthPadding, 0.5f, 0.0f, 10000.0f, "%.1f");

			ImGui::SeparatorText("Bias / Filtering");
			ImGui::DragFloat("Receiver Depth Bias", &settings.m_ReceiverDepthBias, 0.0001f, 0.0f, 0.1f, "%.5f");
			ImGui::DragInt("Rasterizer Depth Bias", &settings.m_RasterizerDepthBias, 1.0f, -100000, 100000);
			ImGui::DragFloat("Slope Scaled Depth Bias", &settings.m_RasterizerSlopeScaledDepthBias, 0.01f, -100.0f, 100.0f, "%.3f");
		}

		static void DrawVisualizationSettings(ShadowVisualizationSettings& settings) noexcept
		{
			ImGui::SeparatorText("Preview");
			ImGui::DragFloat("Preview Min Depth", &settings.m_PreviewMinDepth, 0.001f, 0.0f, 1.0f, "%.4f");
			ImGui::DragFloat("Preview Max Depth", &settings.m_PreviewMaxDepth, 0.001f, 0.0f, 1.0f, "%.4f");
			settings.m_PreviewMinDepth = std::clamp(settings.m_PreviewMinDepth, 0.0f, 1.0f);
			settings.m_PreviewMaxDepth = std::clamp(settings.m_PreviewMaxDepth, 0.0f, 1.0f);
			if (settings.m_PreviewMaxDepth <= settings.m_PreviewMinDepth)
			{
				settings.m_PreviewMaxDepth = std::min(settings.m_PreviewMinDepth + 0.001f, 1.0f);
			}
			ImGui::Checkbox("Invert Preview", &settings.m_PreviewInvert);
		}

		static void DrawShadowCapability(DevelopGuiContext& context) noexcept
		{
			ImGui::SeparatorText("Shadow");

			DirectionalLightBinding lightBinding = FindDirectionalLight(context.m_World);
			if (!lightBinding.m_Light)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Directional light is not found.");
				return;
			}

			bool castShadows = lightBinding.m_Light->m_DirectionalShadowSettings.has_value();
			if (ImGui::Checkbox("Cast Shadows", &castShadows))
			{
				if (castShadows)
				{
					lightBinding.m_Light->m_DirectionalShadowSettings.emplace();
				}
				else
				{
					lightBinding.m_Light->m_DirectionalShadowSettings.reset();
				}
			}

			if (!lightBinding.m_Light->m_DirectionalShadowSettings)
			{
				ImGui::TextUnformatted("Directional light has no shadow settings.");
				return;
			}

			context.m_DirectionalShadowSettings = &*lightBinding.m_Light->m_DirectionalShadowSettings;
			DrawDirectionalShadowSettings(*context.m_DirectionalShadowSettings);
		}

		static void DrawShadowCamera(DevelopGuiContext& context, ShadowInspectorPanelState& state) noexcept
		{
			ImGui::SeparatorText("Shadow Camera / Frustum");

			const RenderView* shadowView = FindRenderView(context.m_RenderViews, RenderViewID::DirectionalShadow);
			if (!shadowView)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Directional shadow render view is not available.");
				return;
			}

			ImGui::Text("Position: %.3f, %.3f, %.3f",
				shadowView->m_CameraPosition.x,
				shadowView->m_CameraPosition.y,
				shadowView->m_CameraPosition.z);
			ImGui::Text("Near/Far: %.3f / %.3f", shadowView->m_Near, shadowView->m_Far);
			ImGui::Text("Viewport: %u x %u", shadowView->m_Width, shadowView->m_Height);
			ImGui::Checkbox("Show Matrices", &state.m_ShowMatrices);

			if (state.m_ShowMatrices)
			{
				DrawMatrix4x4("View", shadowView->m_View);
				DrawMatrix4x4("Projection", shadowView->m_Proj);
				DrawMatrix4x4("ViewProjection", shadowView->m_ViewProj);
			}
		}

		static void DrawShadowMapResource(DevelopGuiContext& context, ShadowInspectorPanelState& state) noexcept
		{
			ImGui::SeparatorText("ShadowMap Resource");

			if (!context.m_RenderGraph)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "RenderGraph is null.");
				return;
			}

			auto* shadowRes = context.m_RenderGraph->GetBlackboard().TryGet<RGShadowResources>(ShadowResourcesName);
			if (!shadowRes || !shadowRes->m_DirectionalShadowMap.IsValid())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ShadowMap resource is not available.");
				return;
			}

			ImGui::TextUnformatted("ShadowMap is a transient RenderGraph texture.");
			ImGui::Text("RG Size: %u", shadowRes->m_ShadowMapSize);
			ImGui::Text("Texture Size: %u x %u", shadowRes->m_ShadowMapSize, shadowRes->m_ShadowMapSize);
			ImGui::Text("Format: %s", devtools::EnumText(DXGI_FORMAT_R32_TYPELESS).data());
			ImGui::Text("Preview SRV Format: %s", devtools::EnumText(DXGI_FORMAT_R32_FLOAT).data());

			if (!shadowRes->m_DirectionalShadowMapPreview.IsValid())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ShadowMap preview resource is not available.");
				return;
			}

			auto* renderResourceRegistry = context.m_Renderer ? context.m_Renderer->GetRenderResourceRegistry() : nullptr;
			if (!renderResourceRegistry)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "RenderResourceRegistry is null.");
				return;
			}

			using TextureIndex = RenderResourceRegistry::TextureIndex;
			constexpr TextureIndex ShadowMapPreviewIndex = TextureIndex::Preview_Shadow_DirectionalShadowMap;

			renderResourceRegistry->EnsureShadowPreviewResources(shadowRes->m_ShadowMapPreviewSize);
			DX12Texture* previewTexture = renderResourceRegistry->GetTexture(ShadowMapPreviewIndex);
			if (!previewTexture)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ShadowMap preview texture is not allocated.");
				return;
			}

			const auto previewDesc = previewTexture->GetDesc();
			const uint32_t previewSrvIndex = renderResourceRegistry->GetShaderVisibleSrvIndex(ShadowMapPreviewIndex);
			const D3D12_GPU_DESCRIPTOR_HANDLE previewSrvGpuHandle =
				ToGpuDescriptorHandle(
					context.m_Renderer,
					renderResourceRegistry->GetSrvDescriptor(ShadowMapPreviewIndex));

			ImGui::Text("Preview RG Size: %u", shadowRes->m_ShadowMapPreviewSize);
			ImGui::Text("Preview Texture Size: %llu x %u",
				static_cast<unsigned long long>(previewDesc.Width),
				static_cast<uint32_t>(previewDesc.Height));
			ImGui::Text("Preview Format: %s", devtools::EnumText(previewDesc.Format).data());
			ImGui::Text("Preview Shader Visible SRV Index: %u", previewSrvIndex);

			if (previewSrvGpuHandle.ptr == 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ShadowMap preview SRV GPU handle is invalid.");
				return;
			}

			ImGui::SeparatorText("Preview");
			ImGui::SliderFloat("Preview Size", &state.m_PreviewSize, 128.0f, 768.0f, "%.0f");
			ImGui::Checkbox("Flip Preview Y", &state.m_FlipPreviewY);

			const float previewSize = std::clamp(state.m_PreviewSize, 16.0f, 2048.0f);
			const ImVec2 uv0 = state.m_FlipPreviewY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
			const ImVec2 uv1 = state.m_FlipPreviewY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
			ImGui::Image(ToImGuiTextureID(previewSrvGpuHandle), ImVec2(previewSize, previewSize), uv0, uv1);
		}
	}

	void ShadowInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<ShadowInspectorPanelState>();

		ImGui::TextUnformatted("Shadow Inspector");
		ImGui::Separator();

		if (!context.m_Renderer)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Renderer is null.");
			return;
		}

		DrawShadowCapability(context);
		if (context.m_ShadowVisualizationSettings)
		{
			DrawVisualizationSettings(*context.m_ShadowVisualizationSettings);
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Shadow visualization settings are not available.");
		}
		DrawLightControl(context);
		DrawShadowCamera(context, state);
		DrawShadowMapResource(context, state);
	}
}
