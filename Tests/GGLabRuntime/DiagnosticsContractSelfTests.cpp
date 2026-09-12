#include "DiagnosticsContractSelfTests.h"
#include "Diagnostics/WorldTooling.h"
#include "Diagnostics/CameraTooling.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/Asset/AssetToolingControlBase.h"
#include "Diagnostics/DirectionalLightTooling.h"
#include "Graphics/RenderWorldExtractor.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderViewSnapshot.h"
#include "Diagnostics/Builders/RenderQueueSnapshotBuilder.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include <limits>

#include "Diagnostics/Builders/BuiltinSnapshotProviders.h"
#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/Builders/ShadowDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsControl.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/PersistentSceneBufferSnapshot.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	struct DiagnosticsViewContractSnapshot
	{
		uint32_t m_CaptureSerial = 0;
	};

	template <> struct SnapshotTraits<DiagnosticsViewContractSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.DiagnosticsViewContractSnapshot");
	};

	struct UnregisteredDiagnosticsViewContractSnapshot
	{
	};

	template <> struct SnapshotTraits<UnregisteredDiagnosticsViewContractSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.UnregisteredDiagnosticsViewContractSnapshot");
	};

	template <typename T>
	concept DiagnosticsSnapshotQuery = requires(T& value) {
		{ value.template GetSnapshot<DiagnosticsViewContractSnapshot>() } ->
			std::same_as<const DiagnosticsViewContractSnapshot*>;
	};

	template <typename T>
	concept DiagnosticsRefreshControl = requires(T& value) {
		value.template RequestRefresh<DiagnosticsViewContractSnapshot>();
	};

	static_assert(DiagnosticsSnapshotQuery<DiagnosticsView>);
	static_assert(!DiagnosticsRefreshControl<DiagnosticsView>);
	static_assert(!DiagnosticsSnapshotQuery<DiagnosticsControl>);
	static_assert(DiagnosticsRefreshControl<DiagnosticsControl>);

	template <typename T>
	concept GpuProfilingQuery = requires(const T& value) {
		{ value.IsEnabled() } -> std::same_as<bool>;
		{ value.GetLatestFrame() } -> std::same_as<GpuProfileFrameSnapshot>;
	};

	template <typename T>
	concept GpuProfilingControl = requires(T& value) {
		value.RequestEnabled(true);
	};

	static_assert(GpuProfilingQuery<GpuProfilingViewBase>);
	static_assert(!GpuProfilingControl<GpuProfilingViewBase>);
	static_assert(!GpuProfilingQuery<GpuProfilingControlBase>);
	static_assert(GpuProfilingControl<GpuProfilingControlBase>);

	template <typename T>
	concept DirectionalLightQuery = requires(const T& value) {
		{ value.GetLight() } -> std::same_as<std::optional<DirectionalLightObservation>>;
	};

	template <typename T>
	concept DirectionalLightControl = requires(T& value) {
		value.SetRadiance(0, Color::White, 1.0f);
	};

	static_assert(DirectionalLightQuery<DirectionalLightViewBase>);
	static_assert(!DirectionalLightControl<DirectionalLightViewBase>);
	static_assert(!DirectionalLightQuery<DirectionalLightControlBase>);
	static_assert(DirectionalLightControl<DirectionalLightControlBase>);

	template <typename T>
	concept AssetToolingSubmission = requires(T& value, const std::filesystem::path& path) {
		{ value.LoadModelAsync(path) } -> std::same_as<ModelLoadReceipt>;
		{ value.LoadTextureAsync(path, TextureSemantic::Normal) } -> std::same_as<TextureLoadReceipt>;
		{ value.ClearTextureDerivedDataCache() } -> std::same_as<bool>;
	};

	template <typename T>
	concept LiveModelAccess = requires(const T& value) { value.GetModel(ModelID{}); };

	static_assert(AssetToolingSubmission<AssetToolingControlBase>);
	static_assert(!LiveModelAccess<AssetToolingControlBase>);

	template <typename T>
	concept CameraToolingQuery = requires(const T& value) {
		{ value.GetCameras() } -> std::same_as<CameraToolingSnapshot>;
	};
	template <typename T>
	concept CameraToolingMutation = requires(T& value) { value.RemoveCamera(0); };
	static_assert(CameraToolingQuery<CameraToolingViewBase>);
	static_assert(!CameraToolingMutation<CameraToolingViewBase>);
	static_assert(!CameraToolingQuery<CameraToolingControlBase>);
	static_assert(CameraToolingMutation<CameraToolingControlBase>);

	template <typename T>
	concept WorldToolingQuery = requires(const T& value) {
		{ value.GetEntities() } -> std::same_as<WorldToolingSnapshot>;
	};
	template <typename T>
	concept WorldToolingMutation = requires(T& value) { value.DestroyEntity(EntityToolingTarget{}); };
	static_assert(WorldToolingQuery<WorldToolingViewBase>);
	static_assert(!WorldToolingMutation<WorldToolingViewBase>);
	static_assert(!WorldToolingQuery<WorldToolingControlBase>);
	static_assert(WorldToolingMutation<WorldToolingControlBase>);

	namespace
	{
		class ContractGpuProfiler final : public GpuProfiler
		{
		public:
			void RequestEnabled(bool enabled) noexcept override { m_Enabled = enabled; }
			bool IsEnabled() const noexcept override { return m_Enabled; }
			GpuProfileFrameSnapshot GetLatestFrame() const override { return m_LatestFrame; }

			bool m_Enabled = true;
			GpuProfileFrameSnapshot m_LatestFrame;
		};

		void RunGpuProfilingContractSelfTests(SelfTestContext& context) noexcept
		{
			GpuProfileFrameSnapshot retainedFrame;
			{
				ContractGpuProfiler profiler;
				const GpuProfilingViewBase& view = profiler;
				GpuProfilingControlBase& control = profiler;
				context.Check(view.IsEnabled() && !view.GetLatestFrame().IsValid(),
					"GPU profiling query distinguishes enabled collection from completed data");

				profiler.m_LatestFrame = {
					.m_FrameIndex = 17,
					.m_FrameMilliseconds = 2.5,
					.m_Samples = { { "Opaque", 1.5, 2 } },
				};
				retainedFrame = view.GetLatestFrame();
				control.RequestEnabled(false);
				context.Check(!view.IsEnabled() && view.GetLatestFrame().m_FrameIndex == 17,
					"GPU profiling control reaches its owner without discarding completed timings");
				control.RequestEnabled(true);
				context.Check(view.IsEnabled(),
					"GPU profiling queries observe the latest accepted enable request");

				profiler.m_LatestFrame.m_FrameIndex = 18;
				profiler.m_LatestFrame.m_Samples.front().m_Name = "Transparent";
				context.Check(view.GetLatestFrame().m_FrameIndex == 18 &&
					retainedFrame.m_FrameIndex == 17 &&
					retainedFrame.m_Samples.front().m_Name == "Opaque",
					"GPU timing value copies remain independent of later backend publication");
			}
			context.Check(retainedFrame.IsValid() && retainedFrame.m_FrameMilliseconds == 2.5 &&
				retainedFrame.m_Samples.size() == 1 &&
				retainedFrame.m_Samples.front().m_Milliseconds == 1.5 &&
				retainedFrame.m_Samples.front().m_CallCount == 2,
				"A retained GPU timing snapshot outlives the profiler owner");
		}

		struct ShadowDiagnosticsFixturePassData
		{
		};

		class DiagnosticsViewContractProvider final : public SnapshotProviderBase
		{
		public:
			[[nodiscard]] SnapshotId GetId() const noexcept override
			{
				return SnapshotIdOf<DiagnosticsViewContractSnapshot>;
			}

			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Diagnostics View Contract";
			}

			void Capture(const DiagnosticsFrameContext& snapshotContext, SnapshotStore& store) noexcept override
			{
				m_LastWorld = snapshotContext.m_World;
				store.GetOrCreate<DiagnosticsViewContractSnapshot>().m_CaptureSerial =
					++m_CaptureCount;
			}

			uint32_t m_CaptureCount = 0;
			World* m_LastWorld = nullptr;
		};

		class DiagnosticsLabSnapshotSource final : public LabSnapshotSourceBase
		{
		public:
			LabSnapshot GetLabSnapshot() const noexcept override
			{
				LabSnapshot snapshot{};
				snapshot.m_ActiveLabName = m_ActiveLabName;
				return snapshot;
			}

			std::string m_ActiveLabName;
		};
	}

	void RunDiagnosticsContractSelfTests(SelfTestContext& context) noexcept
	{
		{
			World world;
			WorldTooling tooling(world);
			const WorldToolingViewBase& view = tooling;
			WorldToolingControlBase& control = tooling;
			const auto empty = view.GetEntities();
			context.Check(empty.m_WorldId != 0 && empty.m_Entities.empty() &&
				!control.DestroyEntity({}), "World tooling identifies empty worlds and rejects empty targets");
			const auto target = control.CreateEntity();
			const auto original = view.GetEntities();
			context.Check(original.FindEntity(target) && original.FindEntity(target)->m_Transform &&
				!original.FindEntity(target)->m_Light && !control.AddTransform(target),
				"Entity creation supplies one transform and duplicate additions are rejected");
			context.Check(control.AddLight(target) && !control.AddLight(target) &&
				control.AddModel(target, ModelID{ 42 }) && !control.AddModel(target, ModelID{ 43 }),
				"Light/model additions retain one component and do not replace model assignment");
			const auto defaults = view.GetEntities();
			const auto* entity = defaults.FindEntity(target);
			context.Check(entity && entity->m_Light && entity->m_Light->m_Type == LightType::Point &&
				entity->m_Light->m_Intensity == 3.0f && entity->m_Light->m_Range == 15.0f &&
				entity->m_Light->m_SpotAngle == 45.0f && entity->m_Model->m_ModelId == ModelID{ 42 } &&
				defaults.m_Entities.size() == 1 && !original.FindEntity(target)->m_Light,
				"Snapshots deduplicate multi-component entities and preserve authoring defaults as owned values");
			components::TransformComponent transform;
			transform.m_Position = Vector3(2.0f, 3.0f, 4.0f);
			context.Check(control.SetTransform(target, transform) &&
				view.GetEntities().FindEntity(target)->m_Transform->m_Position.m_X == 2.0f &&
				original.FindEntity(target)->m_Transform->m_Position.m_X == 0.0f,
				"Transform commands update the entity without mutating retained observations");
			components::LightComponent light;
			light.m_DirectionalShadowSettings.emplace();
			light.m_Intensity = -2.0f;
			light.m_Range = -1.0f;
			light.m_SpotAngle = 400.0f;
			control.SetLight(target, light);
			const auto directional = view.GetEntities();
			context.Check(directional.FindEntity(target)->m_Light->m_DirectionalShadowSettings &&
				directional.FindEntity(target)->m_Light->m_Intensity == 0.0f &&
				directional.FindEntity(target)->m_Light->m_Range == 0.001f &&
				directional.FindEntity(target)->m_Light->m_SpotAngle == 179.0f,
				"Light authoring retains value normalization and optional shadow settings");
			light.m_Type = LightType::Spot;
			control.SetLight(target, light);
			context.Check(!view.GetEntities().FindEntity(target)->m_Light->m_DirectionalShadowSettings &&
				directional.FindEntity(target)->m_Light->m_DirectionalShadowSettings,
				"Changing away from directional lighting clears authoring shadows without changing copied values");
			World otherWorld;
			WorldTooling otherTooling(otherWorld);
			const auto otherTarget = otherTooling.CreateEntity();
			context.Check(otherTarget.m_EntityId == target.m_EntityId &&
				otherTarget.m_WorldId != target.m_WorldId &&
				!otherTooling.DestroyEntity(target) && !otherTooling.SetTransform(target, transform) &&
				!otherTooling.GetEntities().FindEntity(target),
				"Matching entity IDs in different Worlds cannot alias a selected or pending-delete target");
			context.Check(control.DestroyEntity(target), "Entity deletion reaches the owning registry");
			const auto replacement = control.CreateEntity();
			context.Check(entt::to_entity(static_cast<entt::entity>(target.m_EntityId)) ==
				entt::to_entity(static_cast<entt::entity>(replacement.m_EntityId)) &&
				target.m_EntityId != replacement.m_EntityId &&
				!control.DestroyEntity(target) && !control.AddTransform(target) &&
				!control.AddLight(target) && !control.AddModel(target, ModelID{ 42 }) &&
				!control.SetTransform(target, transform) && !control.SetLight(target, light) &&
				view.GetEntities().FindEntity(replacement) && original.FindEntity(target),
				"Every command rejects a recycled entity's old version and retained snapshots stay independent");
			const auto rawEntity = world.GetRegistry().create();
			const EntityToolingTarget rawTarget{ empty.m_WorldId, entt::to_integral(rawEntity) };
			context.Check(!view.GetEntities().FindEntity(rawTarget) &&
				!control.SetTransform(rawTarget, transform) && !control.SetLight(rawTarget, light) &&
				!control.AddModel(rawTarget, {}) && control.AddModel(rawTarget, ModelID{ 7 }) &&
				view.GetEntities().FindEntity(rawTarget)->m_Transform,
				"Unsupported empty entities stay out of the list; model addition repairs a missing transform");
			world.GetRegistry().remove<components::TransformComponent>(rawEntity);
			context.Check(control.AddLight(rawTarget) && view.GetEntities().FindEntity(rawTarget)->m_Transform,
				"Adding a light also repairs a missing transform");
			world.GetRegistry().remove<components::TransformComponent>(rawEntity);
			context.Check(control.AddTransform(rawTarget) && !control.AddTransform(rawTarget),
				"Explicit transform repair preserves existing components");
			const auto sorted = view.GetEntities();
			context.Check(sorted.m_Entities.size() == 2 &&
				sorted.m_Entities[0].m_Target.m_EntityId < sorted.m_Entities[1].m_Target.m_EntityId,
				"Entity observations retain the panel's numeric versioned-ID ordering");
			WorldTooling nextDraw(world);
			context.Check(nextDraw.GetEntities().m_WorldId == empty.m_WorldId,
				"Reconstructing a draw-scoped adapter preserves World identity");
			World movedWorld(std::move(world));
			WorldTooling movedTooling(movedWorld);
			context.Check(movedTooling.GetEntities().FindEntity(replacement) &&
				tooling.GetEntities().m_WorldId == 0 && !tooling.DestroyEntity(replacement),
				"World identity follows registry moves and invalidates adapters bound to the moved-from World");
			WorldTooling reusedSource(world);
			context.Check(reusedSource.GetEntities().m_WorldId != empty.m_WorldId &&
				!reusedSource.DestroyEntity(replacement),
				"A reused moved-from World receives a fresh identity");
			otherWorld = std::move(movedWorld);
			WorldTooling assignedTooling(otherWorld);
			context.Check(assignedTooling.GetEntities().FindEntity(replacement) &&
				!otherTooling.DestroyEntity(otherTarget) && !movedTooling.DestroyEntity(replacement),
				"World move assignment preserves incoming identity and invalidates previously bound adapters");
		}
		{
			CameraRig rig;
			CameraTooling tooling(rig);
			const CameraToolingViewBase& view = tooling;
			CameraToolingControlBase& control = tooling;
			context.Check(view.GetCameras().m_Cameras.empty() && control.AddDebugCamera() == 0 &&
				!control.RemoveCamera(0) && !control.SetActiveCamera(0),
				"Camera tooling handles an empty rig without creating or targeting a camera");
			Camera mainCamera(Camera::CreateInfo{});
			CameraController controller(CameraController::CreateInfo{});
			rig.AttachMainCamera(mainCamera, controller);
			const auto original = view.GetCameras();
			const auto mainId = original.m_ActiveCameraId;
			rig.GetMainCameraSlot()->m_EnableRenderView = false;
			context.Check(control.SetDisplayCamera(mainId),
				"Main display selection preserves the rig's requested-view fallback policy");
			rig.GetMainCameraSlot()->m_EnableRenderView = true;
			context.Check(mainId != 0 && original.m_Cameras.size() == 1 &&
				original.FindCamera(mainId) && !control.RemoveCamera(mainId) &&
				!control.SetRenderViewEnabled(mainId, false),
				"Main camera has a value identity and cannot be removed or disabled as a debug view");
			CameraEditSettings edit;
			edit.m_Position = Vector3(2.0f, 3.0f, 4.0f);
			edit.m_Fov = 200.0f;
			edit.m_Near = -1.0f;
			edit.m_Far = -2.0f;
			edit.m_ExposureCompensationEV = 20.0f;
			const auto resetSerial = mainCamera.GetTemporalResetSerial();
			context.Check(control.SetCamera(mainId, edit) &&
				mainCamera.GetPosition().m_X == 2.0f &&
				mainCamera.GetFov() == Camera::ClampFov(edit.m_Fov) &&
				mainCamera.GetNear() > 0.0f && mainCamera.GetFar() > mainCamera.GetNear() &&
				mainCamera.GetExposureCompensationEV() == Camera::ClampExposureCompensationEV(20.0f) &&
				mainCamera.GetTemporalResetSerial() == resetSerial &&
				original.m_Cameras.front().m_Settings.m_Position.m_X == 0.0f,
				"Camera edits preserve owner clamping and temporal reset policy while observations remain independent");
			const auto debugId = control.AddDebugCamera();
			const auto otherId = control.AddDebugCamera();
			context.Check(debugId != 0 && otherId != debugId &&
				control.SetActiveCamera(debugId) && view.GetCameras().m_ActiveCameraId == debugId &&
				control.SetDisplayCamera(debugId),
				"Tooling adds independent cameras and selects active and display cameras separately");
			context.Check(control.SetFrustum(debugId, false, Color::Red) &&
				control.SetVisibilityMode(debugId, RenderViewVisibilityMode::MainCamera) &&
				!view.GetCameras().FindCamera(debugId)->m_ShowFrustum &&
				view.GetCameras().FindCamera(debugId)->m_VisibilityMode == RenderViewVisibilityMode::MainCamera,
				"Frustum and visibility edits reach only the identified camera");
			const auto retained = view.GetCameras();
			context.Check(control.RemoveCamera(debugId) &&
				view.GetCameras().m_DisplayViewId == RenderViewID::Main &&
				!control.SetCamera(debugId, edit) && !control.SetActiveCamera(debugId) &&
				!control.SetDisplayCamera(debugId) && !control.SetFrustum(debugId, true, Color::White) &&
				!control.SetRenderViewEnabled(debugId, true) &&
				!control.SetVisibilityMode(debugId, RenderViewVisibilityMode::Self) &&
				!control.SetController(debugId, {}) && !control.ResetVelocity(debugId) &&
				!control.RemoveCamera(debugId) && retained.FindCamera(debugId) &&
				control.SetActiveCamera(otherId),
				"Removed IDs reject every command while surviving IDs remain valid after slot compaction");
			const auto replacementId = control.AddDebugCamera();
			context.Check(replacementId != debugId && control.SetDisplayCamera(replacementId) &&
				control.SetRenderViewEnabled(replacementId, false) &&
				view.GetCameras().m_DisplayViewId == RenderViewID::Main &&
				!control.SetDisplayCamera(replacementId) && control.SetRenderViewEnabled(replacementId, true),
				"Reused render-view slots do not reuse camera IDs and disabling display restores Main");
			GGLAB_UNUSED(control.AddDebugCamera());
			const auto overflowId = control.AddDebugCamera();
			context.Check(!view.GetCameras().FindCamera(overflowId)->m_EnableRenderView &&
				!control.SetRenderViewEnabled(overflowId, true),
				"Tooling preserves the finite debug render-view capacity");
			CameraControllerSettings params;
			params.m_MovementSpeed = -1.0f;
			params.m_SmoothStepT = 2.0f;
			context.Check(control.SetController(mainId, params) &&
				controller.GetMovementSpeed() >= 0.0f && controller.GetSmoothStepT() <= 1.0f,
				"Controller edits retain runtime sanitization");
			params.m_MovementSpeed = 10.0f;
			params.m_SmoothStepT = 0.5f;
			control.SetController(mainId, params);
			controller.Update(mainCamera, CameraInput{ .m_Front = true }, 0.01f);
			const auto position = mainCamera.GetPosition();
			context.Check(control.ResetVelocity(mainId), "Velocity reset reaches the selected controller");
			controller.Update(mainCamera, CameraInput{}, 0.01f);
			context.Check((mainCamera.GetPosition() - position).LengthSquared() == 0.0f,
				"Velocity reset prevents residual controller movement on the next update");
			rig.GetMainCameraSlot()->m_Controller = nullptr;
			context.Check(!view.GetCameras().FindCamera(mainId)->m_Controller &&
				!control.ResetVelocity(mainId) && !control.SetController(mainId, params) &&
				control.SetCamera(mainId, edit),
				"Cameras without controllers remain editable and reject controller-only commands");
			rig.AttachMainCamera(mainCamera, controller);
			const auto reboundId = rig.GetMainCameraSlot()->m_Id;
			context.Check(reboundId != mainId && !control.SetCamera(mainId, edit) &&
				control.SetCamera(reboundId, edit),
				"Reattaching even the same main camera invalidates the previous identity");
			CameraRig otherRig;
			otherRig.AttachMainCamera(mainCamera, controller);
			CameraTooling otherTooling(otherRig);
			context.Check(!otherTooling.SetCamera(reboundId, edit),
				"Camera identities cannot cross rig or session boundaries");
			CameraRig movedRig(std::move(rig));
			CameraTooling movedTooling(movedRig);
			context.Check(movedTooling.SetActiveCamera(otherId) &&
				movedTooling.GetCameras().m_ActiveCameraId == otherId,
				"Moving a camera rig preserves the identities of its owned slots");
		}
		{
			World world;
			auto& registry = world.GetRegistry();
			DirectionalLightTooling tooling(world);
			const DirectionalLightViewBase& view = tooling;
			DirectionalLightControlBase& control = tooling;
			context.Check(!view.GetLight(), "Empty worlds have no directional light observation");
			const auto entity = registry.create();
			registry.emplace<components::LightComponent>(entity);
			context.Check(!view.GetLight(), "Lights without transforms are not tool targets");
			registry.emplace<components::TransformComponent>(entity);
			const auto original = view.GetLight();
			context.Check(original.has_value(), "Directional lights produce value observations");
			if (original)
			{
				const auto id = original->m_Id;
				control.SetDirection(id, Vector3(0.0f, -2.0f, 0.0f));
				control.SetRadiance(id, Color::Red, 3.0f);
				DirectionalShadowSettings settings{};
				settings.m_ShadowMapSize = 2048;
				control.SetShadowSettings(id, settings);
				const auto edited = view.GetLight();
				context.Check(edited && edited->m_Direction.m_Y < -0.999f &&
					edited->m_Intensity == 3.0f && edited->m_Color.m_G == 0.0f &&
					edited->m_ShadowSettings && edited->m_ShadowSettings->m_ShadowMapSize == 2048 &&
					original->m_Intensity == 1.0f && !original->m_ShadowSettings,
					"Typed edits update authoring without changing retained observations");
				control.SetDirection(id, Vector3::Zero);
				control.SetDirection(id, Vector3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f));
				context.Check(view.GetLight()->m_Direction.m_Y < -0.999f,
					"Zero and non-finite directions leave the light orientation unchanged");
				RenderFrameBuildResult builtFrame{};
				builtFrame.m_DirectionalShadowSettings =
					RenderWorldExtractor().Extract(world).GetMainDirectionalShadowSettings();
				const auto renderContext = builtFrame.MakeRenderFrameContext();
				control.SetShadowSettings(id, std::nullopt);
				context.Check(renderContext.m_DirectionalShadowSettings.m_ShadowMapSize == 2048 &&
					!RenderWorldExtractor().Extract(world).m_MainDirectionalLight.m_ShadowSettings,
					"Tooling shadow edits leave the constructed render context intact until the next extraction");
				context.Check(!view.GetLight()->m_ShadowSettings &&
					edited && edited->m_ShadowSettings,
					"Disabling shadows preserves previously copied settings");
				registry.get<components::LightComponent>(entity).m_Type = LightType::Point;
				control.SetRadiance(id, Color::White, 9.0f);
				context.Check(!view.GetLight() &&
					registry.get<components::LightComponent>(entity).m_Intensity == 3.0f,
					"Commands reject targets that are no longer directional lights");
				registry.destroy(entity);
				const auto replacement = registry.create();
				registry.emplace<components::TransformComponent>(replacement);
				registry.emplace<components::LightComponent>(replacement);
				control.SetRadiance(id, Color::Red, 9.0f);
				control.SetDirection(id, -Vector3::UnitY);
				control.SetShadowSettings(id, settings);
				const auto fresh = view.GetLight();
				context.Check(entt::to_entity(replacement) == entt::to_entity(entity) &&
					fresh && fresh->m_Id != id && fresh->m_Intensity == 1.0f &&
					fresh->m_Direction.m_Y == 0.0f && !fresh->m_ShadowSettings,
					"Stale versioned IDs cannot edit a replacement light in a recycled entity slot");
				registry.remove<components::TransformComponent>(replacement);
				control.SetRadiance(entt::to_integral(replacement), Color::Red, 9.0f);
				context.Check(registry.get<components::LightComponent>(replacement).m_Intensity == 1.0f,
					"Commands reject lights whose transform was removed");
			}
		}
		{
			RenderView source;
			source.m_ViewId = RenderViewID::DirectionalShadow;
			source.m_Width = 2048;
			source.m_CameraPosition = Vector3(1.0f, 2.0f, 3.0f);
			source.m_IsValid = true;
			DiagnosticsRuntime viewDiagnostics;
			RegisterBuiltinSnapshotProviders(viewDiagnostics);
			context.Check(viewDiagnostics.GetSnapshot<RenderViewSnapshot>() == nullptr,
				"Render view diagnostics require an open frame for initial capture");
			viewDiagnostics.BeginFrame({ .m_RenderViews = std::span<RenderView>(&source, 1) });
			const auto* publication = viewDiagnostics.GetSnapshot<RenderViewSnapshot>();
			const auto* shadow = publication ? publication->FindView(RenderViewID::DirectionalShadow) : nullptr;
			context.Check(shadow && shadow != &source && shadow->m_Width == 2048 &&
				shadow->m_CameraPosition.m_Y == 2.0f && shadow->m_IsValid &&
				!publication->FindView(RenderViewID::Main),
				"Render view publication owns values and resolves sparse views by identity");
			source.m_Width = 1024;
			source.m_CameraPosition = Vector3::Zero;
			viewDiagnostics.EndFrame();
			const auto* retained = viewDiagnostics.GetSnapshot<RenderViewSnapshot>();
			context.Check(retained && retained->FindView(RenderViewID::DirectionalShadow) &&
				retained->FindView(RenderViewID::DirectionalShadow)->m_Width == 2048 &&
				retained->FindView(RenderViewID::DirectionalShadow)->m_CameraPosition.m_Y == 2.0f,
				"Closed render view publication does not observe later source mutations");
			viewDiagnostics.BeginFrame({ .m_RenderViews = std::span<RenderView>(&source, 1) });
			const auto* updated = viewDiagnostics.GetSnapshot<RenderViewSnapshot>();
			context.Check(updated && updated->FindView(RenderViewID::DirectionalShadow) &&
				updated->FindView(RenderViewID::DirectionalShadow)->m_Width == 1024,
				"Render view diagnostics refresh from the next borrowed frame");
			viewDiagnostics.EndFrame();
			viewDiagnostics.BeginFrame({});
			const auto* empty = viewDiagnostics.GetSnapshot<RenderViewSnapshot>();
			context.Check(empty && empty->m_Views.empty(),
				"An empty frame removes obsolete render view observations");
			viewDiagnostics.EndFrame();
		}
		{
			RenderQueue queue;
			queue.m_ViewId = RenderViewID::Main;
			queue.m_Statistics.m_TotalInstanceCount = 7;
			queue.m_DrawItems.resize(3);
			queue.m_BucketDrawRanges[0] = { 1, std::numeric_limits<uint32_t>::max() };
			queue.m_BucketDrawRanges[1] = { 99, 4 };
			DiagnosticsRuntime queueDiagnostics;
			RegisterBuiltinSnapshotProviders(queueDiagnostics);
			context.Check(queueDiagnostics.GetSnapshot<RenderQueueSnapshot>() == nullptr,
				"Render queue diagnostics do not capture outside a live frame");
			queueDiagnostics.BeginFrame({ .m_RenderQueues = std::span<const RenderQueue>(&queue, 1) });
			const auto* captured = queueDiagnostics.GetSnapshot<RenderQueueSnapshot>();
			context.Check(captured && captured->m_Queues.size() == 1 &&
				captured->m_Queues[0].m_ViewId == RenderViewID::Main &&
				captured->m_Queues[0].m_Statistics.m_TotalInstanceCount == 7 &&
				captured->m_Queues[0].m_Buckets[0].m_UniqueMeshes == 1 &&
				captured->m_Queues[0].m_Buckets[0].m_UniqueMaterials == 1 &&
				captured->m_Queues[0].m_Buckets[1].m_UniqueMeshes == 0,
				"Render queue capture copies counters and clamps bucket ranges without overflow");
			queue.m_DrawItems.clear();
			queue.m_Statistics.m_TotalInstanceCount = 0;
			queueDiagnostics.EndFrame();
			const auto* retained = queueDiagnostics.GetSnapshot<RenderQueueSnapshot>();
			context.Check(retained && retained->m_Queues[0].m_Statistics.m_TotalInstanceCount == 7 &&
				retained->m_Queues[0].m_Buckets[0].m_UniqueMeshes == 1,
				"Published queue diagnostics retain values after source mutation and frame closure");
			queueDiagnostics.BeginFrame({});
			const auto* empty = queueDiagnostics.GetSnapshot<RenderQueueSnapshot>();
			context.Check(empty && empty->m_Queues.empty(),
				"A new empty frame clears prior queue observations");
			queueDiagnostics.EndFrame();
		}
		RunGpuProfilingContractSelfTests(context);

		DiagnosticsRuntime runtime;
		auto provider = std::make_unique<DiagnosticsViewContractProvider>();
		DiagnosticsViewContractProvider* providerObserver = provider.get();
		runtime.RegisterProvider(std::move(provider), SnapshotUpdatePolicy::OnDemand);
		DiagnosticsView& view = runtime;
		DiagnosticsControl& control = runtime;
		World firstWorld;
		World secondWorld;

		context.Check(view.GetSnapshot<UnregisteredDiagnosticsViewContractSnapshot>() == nullptr,
			"Diagnostics view returns no value for an unregistered snapshot contract");

		runtime.BeginFrame({ .m_World = &firstWorld });
		const DiagnosticsViewContractSnapshot* initial =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(initial && initial->m_CaptureSerial == 1 &&
				providerObserver->m_CaptureCount == 1 &&
				providerObserver->m_LastWorld == &firstWorld,
			"Diagnostics view lazily captures a registered immutable snapshot");

		const DiagnosticsViewContractSnapshot* cached =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(cached == initial && cached && cached->m_CaptureSerial == 1 &&
				providerObserver->m_CaptureCount == 1,
			"Diagnostics view reuses the published snapshot until refresh is requested");

		control.RequestRefresh<DiagnosticsViewContractSnapshot>();
		const DiagnosticsViewContractSnapshot* refreshed =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(refreshed == initial && refreshed && refreshed->m_CaptureSerial == 2 &&
				providerObserver->m_CaptureCount == 2,
			"Diagnostics control refresh requests recapture through the Runtime-owned provider");

		const auto profiles = view.GetProfiles();
		context.Check(profiles.size() == 1 && profiles.front().m_HasSnapshot &&
				profiles.front().m_CaptureCount == 2 &&
				profiles.front().m_CacheHitCount == 1 &&
				!profiles.front().m_RefreshPending,
			"Diagnostics view exposes immutable capture profile observations");

		runtime.EndFrame();
		control.RequestRefresh<DiagnosticsViewContractSnapshot>();
		const DiagnosticsViewContractSnapshot* closedFrameSnapshot =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(closedFrameSnapshot && closedFrameSnapshot == refreshed &&
				closedFrameSnapshot->m_CaptureSerial == 2 &&
				providerObserver->m_CaptureCount == 2 &&
				providerObserver->m_LastWorld == &firstWorld,
			"Closed diagnostics frames cannot capture through a borrowed context");

		runtime.BeginFrame({ .m_World = &secondWorld });
		const DiagnosticsViewContractSnapshot* nextFrameSnapshot =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(nextFrameSnapshot && nextFrameSnapshot == refreshed &&
				nextFrameSnapshot->m_CaptureSerial == 3 &&
				providerObserver->m_CaptureCount == 3 &&
				providerObserver->m_LastWorld == &secondWorld,
			"Pending refresh captures against the next active frame context");

		runtime.EndFrame();
		runtime.EndFrame();
		control.RequestRefresh<DiagnosticsViewContractSnapshot>();
		const DiagnosticsViewContractSnapshot* repeatedlyClosedSnapshot =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(repeatedlyClosedSnapshot &&
				repeatedlyClosedSnapshot == nextFrameSnapshot &&
				repeatedlyClosedSnapshot->m_CaptureSerial == 3 &&
				providerObserver->m_CaptureCount == 3,
			"Diagnostics frame closure is idempotent and preserves immutable publication");

		runtime.Reset();
		context.Check(view.GetSnapshot<DiagnosticsViewContractSnapshot>() == nullptr &&
				providerObserver->m_CaptureCount == 3,
			"Diagnostics reset clears publication without capturing outside a frame");

		DiagnosticsRuntime labDiagnostics;
		labDiagnostics.RegisterProvider(
			std::make_unique<LabSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		DiagnosticsLabSnapshotSource firstLabSource;
		firstLabSource.m_ActiveLabName = "First Lab";
		labDiagnostics.BeginFrame({ .m_LabSnapshotSource = &firstLabSource });
		const LabSnapshot* firstLabSnapshot = labDiagnostics.GetSnapshot<LabSnapshot>();
		context.Check(firstLabSnapshot && firstLabSnapshot->m_ActiveLabName == "First Lab",
			"Lab diagnostics capture uses the Runtime-owned frame context source");

		labDiagnostics.EndFrame();
		DiagnosticsLabSnapshotSource secondLabSource;
		secondLabSource.m_ActiveLabName = "Second Lab";
		labDiagnostics.BeginFrame({ .m_LabSnapshotSource = &secondLabSource });
		const LabSnapshot* secondLabSnapshot = labDiagnostics.GetSnapshot<LabSnapshot>();
		context.Check(secondLabSnapshot && secondLabSnapshot == firstLabSnapshot &&
				secondLabSnapshot->m_ActiveLabName == "Second Lab",
			"Lab diagnostics recaptures from the current AppRuntime-supplied source");
		labDiagnostics.EndFrame();

		Renderer resourceSource;
		DiagnosticsRuntime resourceDiagnostics;
		RegisterBuiltinSnapshotProviders(resourceDiagnostics);
				resourceDiagnostics.BeginFrame({ .m_RenderHost = &resourceSource });
		const auto* persistentSnapshot =
			resourceDiagnostics.GetSnapshot<PersistentSceneBufferSnapshot>();
		const auto* transientSnapshot =
			resourceDiagnostics.GetSnapshot<TransientResourcePoolSnapshot>();
		context.Check(persistentSnapshot && persistentSnapshot->m_SourceAvailable &&
				persistentSnapshot->m_Objects.m_BufferVersions.empty(),
			"Persistent buffer diagnostics distinguish a bound source from empty tables");
		context.Check(transientSnapshot && !transientSnapshot->m_SourceAvailable,
			"Transient pool diagnostics report a missing pool even with a bound renderer");
		const PersistentSceneBufferSnapshot retainedPersistentSnapshot =
			persistentSnapshot ? *persistentSnapshot : PersistentSceneBufferSnapshot{};
		resourceDiagnostics.EndFrame();
		resourceDiagnostics.BeginFrame({});
		persistentSnapshot = resourceDiagnostics.GetSnapshot<PersistentSceneBufferSnapshot>();
		transientSnapshot = resourceDiagnostics.GetSnapshot<TransientResourcePoolSnapshot>();
		context.Check(persistentSnapshot && !persistentSnapshot->m_SourceAvailable &&
				transientSnapshot && !transientSnapshot->m_SourceAvailable &&
				retainedPersistentSnapshot.m_SourceAvailable,
			"Missing-source recapture clears availability without changing a retained value copy");
		resourceDiagnostics.EndFrame();

		RenderGraph shadowGraph({
			.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
			.m_TransientResourcePool =
				reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
		shadowGraph.GetBlackboard().Create<RGShadowResources>(ShadowResourcesName);
		shadowGraph.AddPass<ShadowDiagnosticsFixturePassData>("Diagnostics.ShadowFixture",
			[](RenderGraph::RGBuilder& builder, ShadowDiagnosticsFixturePassData&)
			{
				auto& resources =
					builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
				resources.m_DirectionalShadowMap = builder.CreateTexture("Shadow.Map", {
					.m_Format = RHIFormat::R32Typeless,
					.m_Extent = { 2048, 2048, 1 },
					});
				resources.m_DirectionalShadowMapPreview = builder.CreateTexture("Shadow.Preview", {
					.m_Format = RHIFormat::R32Float,
					.m_Extent = { 512, 512, 1 },
					});
				resources.m_ShadowMapSize = 2048;
				resources.m_ShadowMapPreviewSize = 512;
			});
		const ShadowDiagnosticsSnapshot shadowSnapshot =
			BuildShadowDiagnosticsSnapshot(shadowGraph);
		context.Check(shadowSnapshot.m_Available &&
				shadowSnapshot.m_DirectionalShadowMap.m_Available &&
				shadowSnapshot.m_DirectionalShadowMap.m_Extent.m_Width == 2048 &&
				shadowSnapshot.m_DirectionalShadowMap.m_Extent.m_Height == 2048 &&
				shadowSnapshot.m_DirectionalShadowMap.m_Format == RHIFormat::R32Typeless &&
				shadowSnapshot.m_DirectionalShadowMapPreviewSource.m_Available &&
				shadowSnapshot.m_DirectionalShadowMapPreviewSource.m_Extent.m_Width == 512 &&
				shadowSnapshot.m_DirectionalShadowMapPreviewSource.m_Format == RHIFormat::R32Float &&
				shadowSnapshot.m_ShadowMapSize == 2048 &&
				shadowSnapshot.m_ShadowMapPreviewSize == 512,
			"Shadow diagnostics copies RenderGraph resource state into an immutable snapshot");
	}
}
