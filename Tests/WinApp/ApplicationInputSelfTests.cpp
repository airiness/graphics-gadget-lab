#include "ApplicationInputSelfTests.h"

#include "Demo/DemoTypes.h"
#include "ApplicationCameraInput.h"
#include "ApplicationInput.h"
#include "Application/Platform/Windows/Input/Keyboard.h"
#include "Application/Platform/Windows/Input/WindowsInputMapping.h"

#include <GameInput.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

namespace gglab
{
	namespace
	{
		// Exercise the production enumeration and polling path without physical devices.
		template<typename Interface>
		class GameInputTestObjectBase : public Interface
		{
		public:
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
			{
				if (!object)
				{
					return E_POINTER;
				}
				*object = nullptr;
				if (id != __uuidof(IUnknown) && id != __uuidof(Interface))
				{
					return E_NOINTERFACE;
				}
				*object = static_cast<Interface*>(this);
				AddRef();
				return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++m_References; }
			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG remaining = --m_References;
				if (remaining == 0)
				{
					delete this;
				}
				return remaining;
			}
			ULONG GetReferenceCount() const noexcept { return m_References.load(); }

		protected:
			virtual ~GameInputTestObjectBase() = default;

		private:
			std::atomic<ULONG> m_References{ 1 };
		};

		class TestGameInputDevice final : public GameInputTestObjectBase<IGameInputDevice>
		{
		public:
			void SetKeys(std::initializer_list<uint8_t> keys)
			{
				m_Keys.clear();
				for (const uint8_t key : keys)
				{
					m_Keys.push_back(GameInputKeyState{ .virtualKey = key });
				}
				++m_Timestamp;
			}
			const GameInputDeviceInfo* STDMETHODCALLTYPE GetDeviceInfo() override { return &m_Info; }
			GameInputDeviceStatus STDMETHODCALLTYPE GetDeviceStatus() override
			{
				return m_Connected ? GameInputDeviceConnected : static_cast<GameInputDeviceStatus>(0);
			}
			void STDMETHODCALLTYPE GetBatteryState(GameInputBatteryState*) override {}
			HRESULT STDMETHODCALLTYPE CreateForceFeedbackEffect(
				uint32_t, GameInputForceFeedbackParams const*, IGameInputForceFeedbackEffect**) override
			{
				return E_NOTIMPL;
			}
			bool STDMETHODCALLTYPE IsForceFeedbackMotorPoweredOn(uint32_t) override { return {}; }
			void STDMETHODCALLTYPE SetForceFeedbackMotorGain(uint32_t, float) override {}
			HRESULT STDMETHODCALLTYPE SetHapticMotorState(
				uint32_t, GameInputHapticFeedbackParams const*) override { return E_NOTIMPL; }
			void STDMETHODCALLTYPE SetRumbleState(GameInputRumbleParams const*) override {}
			void STDMETHODCALLTYPE SetInputSynchronizationState(bool) override {}
			void STDMETHODCALLTYPE SendInputSynchronizationHint() override {}
			void STDMETHODCALLTYPE PowerOff() override {}
			HRESULT STDMETHODCALLTYPE CreateRawDeviceReport(
				uint32_t, GameInputRawDeviceReportKind, IGameInputRawDeviceReport**) override
			{
				return E_NOTIMPL;
			}
			HRESULT STDMETHODCALLTYPE GetRawDeviceFeature(
				uint32_t, IGameInputRawDeviceReport**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE SetRawDeviceFeature(
				IGameInputRawDeviceReport*) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE SendRawDeviceOutput(
				IGameInputRawDeviceReport*) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE SendRawDeviceOutputWithResponse(
				IGameInputRawDeviceReport*, IGameInputRawDeviceReport**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE ExecuteRawDeviceIoControl(
				uint32_t, size_t, void const*, size_t, void*, size_t*) override { return E_NOTIMPL; }
			bool STDMETHODCALLTYPE AcquireExclusiveRawDeviceAccess(uint64_t) override { return {}; }
			void STDMETHODCALLTYPE ReleaseExclusiveRawDeviceAccess() override {}

			GameInputDeviceInfo m_Info{};
			std::vector<GameInputKeyState> m_Keys;
			uint64_t m_Timestamp = 1;
			HRESULT m_ReadResult = S_OK;
			bool m_Connected = true;
		};

		class TestGameInputReading final : public GameInputTestObjectBase<IGameInputReading>
		{
		public:
			explicit TestGameInputReading(TestGameInputDevice* device) :
				m_Device(device), m_Keys(device->m_Keys), m_Timestamp(device->m_Timestamp)
			{
			}
			GameInputKind STDMETHODCALLTYPE GetInputKind() override
			{
				return m_Device->m_Info.supportedInput;
			}
			uint64_t STDMETHODCALLTYPE GetTimestamp() override { return m_Timestamp; }
			void STDMETHODCALLTYPE GetDevice(IGameInputDevice** device) override
			{
				*device = m_Device.Get();
				m_Device->AddRef();
			}
			uint32_t STDMETHODCALLTYPE GetKeyCount() override
			{
				return static_cast<uint32_t>(m_Keys.size());
			}
			uint32_t STDMETHODCALLTYPE GetKeyState(uint32_t capacity, GameInputKeyState* keys) override
			{
				const uint32_t count = std::min(capacity, GetKeyCount());
				if (count != 0)
				{
					std::copy_n(m_Keys.begin(), count, keys);
				}
				return count;
			}
			uint64_t STDMETHODCALLTYPE GetSequenceNumber(GameInputKind) override { return {}; }
			bool STDMETHODCALLTYPE GetRawReport(IGameInputRawDeviceReport**) override { return {}; }
			uint32_t STDMETHODCALLTYPE GetControllerAxisCount() override { return {}; }
			uint32_t STDMETHODCALLTYPE GetControllerAxisState(uint32_t, float*) override { return {}; }
			uint32_t STDMETHODCALLTYPE GetControllerButtonCount() override { return {}; }
			uint32_t STDMETHODCALLTYPE GetControllerButtonState(uint32_t, bool*) override { return {}; }
			uint32_t STDMETHODCALLTYPE GetControllerSwitchCount() override { return {}; }
			uint32_t STDMETHODCALLTYPE GetControllerSwitchState(
				uint32_t, GameInputSwitchPosition*) override { return {}; }
			bool STDMETHODCALLTYPE GetMouseState(GameInputMouseState*) override { return {}; }
			uint32_t STDMETHODCALLTYPE GetTouchCount() override { return {}; }
			uint32_t STDMETHODCALLTYPE GetTouchState(uint32_t, GameInputTouchState*) override { return {}; }
			bool STDMETHODCALLTYPE GetMotionState(GameInputMotionState*) override { return {}; }
			bool STDMETHODCALLTYPE GetArcadeStickState(GameInputArcadeStickState*) override { return {}; }
			bool STDMETHODCALLTYPE GetFlightStickState(GameInputFlightStickState*) override { return {}; }
			bool STDMETHODCALLTYPE GetGamepadState(GameInputGamepadState*) override { return {}; }
			bool STDMETHODCALLTYPE GetRacingWheelState(GameInputRacingWheelState*) override { return {}; }
			bool STDMETHODCALLTYPE GetUiNavigationState(GameInputUiNavigationState*) override { return {}; }

		private:
			ComPtr<TestGameInputDevice> m_Device;
			std::vector<GameInputKeyState> m_Keys;
			uint64_t m_Timestamp;
		};

		class TestGameInput final : public GameInputTestObjectBase<IGameInput>
		{
		public:
			ComPtr<TestGameInputDevice> AddDevice(GameInputKind kind, uint16_t vendor, uint16_t product)
			{
				ComPtr<TestGameInputDevice> device;
				device.Attach(new TestGameInputDevice());
				device->m_Info.supportedInput = kind;
				device->m_Info.vendorId = vendor;
				device->m_Info.productId = product;
				m_Devices.push_back(device);
				return device;
			}
			void SetConnected(TestGameInputDevice& device, bool connected)
			{
				const GameInputDeviceStatus previous = device.GetDeviceStatus();
				device.m_Connected = connected;
				for (const Subscription& subscription : m_Subscriptions)
				{
					if ((!subscription.m_Device || subscription.m_Device == &device) &&
						(device.m_Info.supportedInput & subscription.m_Kind) != 0)
					{
						subscription.m_Callback(subscription.m_Token, subscription.m_Context,
							&device, device.m_Timestamp, device.GetDeviceStatus(), previous);
					}
				}
			}
			HRESULT STDMETHODCALLTYPE GetCurrentReading(
				GameInputKind kind, IGameInputDevice* device, IGameInputReading** reading) override
			{
				*reading = nullptr;
				m_ReadDevices.push_back(device);
				if (m_OnRead)
				{
					m_OnRead(device);
				}
				m_UnfilteredReads += device == nullptr ? 1u : 0u;
				TestGameInputDevice* selected = nullptr;
				for (const auto& candidate : m_Devices)
				{
					if (candidate->m_Connected && (candidate->m_Info.supportedInput & kind) != 0 &&
						(!device || candidate.Get() == device) &&
						(!selected || candidate->m_Timestamp >= selected->m_Timestamp))
					{
						selected = candidate.Get();
					}
				}
				if (!selected)
				{
					++m_ReadFailures;
					return E_FAIL;
				}
				if (FAILED(selected->m_ReadResult))
				{
					++m_ReadFailures;
					return selected->m_ReadResult;
				}
				*reading = new TestGameInputReading(selected);
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE RegisterDeviceCallback(IGameInputDevice* device, GameInputKind kind,
				GameInputDeviceStatus, GameInputEnumerationKind enumeration, void* context,
				GameInputDeviceCallback callback, GameInputCallbackToken* token) override
			{
				*token = 0;
				if (FAILED(m_RegisterResult))
				{
					return m_RegisterResult;
				}
				*token = ++m_NextToken;
				const Subscription subscription{ device, kind, context, callback, *token };
				m_Subscriptions.push_back(subscription);
				m_BlockingEnumeration = enumeration == GameInputBlockingEnumeration;
				if (enumeration != GameInputNoEnumeration)
				{
					for (const auto& candidate : m_Devices)
					{
						if (candidate->m_Connected && (candidate->m_Info.supportedInput & kind) != 0 &&
							(!device || device == candidate.Get()))
						{
							callback(*token, context, candidate.Get(), candidate->m_Timestamp,
								candidate->GetDeviceStatus(), static_cast<GameInputDeviceStatus>(0));
						}
					}
				}
				return S_OK;
			}
			bool STDMETHODCALLTYPE UnregisterCallback(GameInputCallbackToken token, uint64_t timeout) override
			{
				++m_UnregisterCalls;
				m_UnregisterTimeout = timeout;
				return std::erase_if(m_Subscriptions, [token](const Subscription& subscription)
					{
						return subscription.m_Token == token;
					}) != 0;
			}
			uint64_t STDMETHODCALLTYPE GetCurrentTimestamp() override { return {}; }
			HRESULT STDMETHODCALLTYPE GetNextReading(
				IGameInputReading*, GameInputKind, IGameInputDevice*, IGameInputReading**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE GetPreviousReading(
				IGameInputReading*, GameInputKind, IGameInputDevice*, IGameInputReading**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE GetTemporalReading(
				uint64_t, IGameInputDevice*, IGameInputReading**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE RegisterReadingCallback(
				IGameInputDevice*, GameInputKind, float, void*,
				GameInputReadingCallback, GameInputCallbackToken*) override
			{
				return E_NOTIMPL;
			}
			HRESULT STDMETHODCALLTYPE RegisterSystemButtonCallback(
				IGameInputDevice*, GameInputSystemButtons, void*,
				GameInputSystemButtonCallback, GameInputCallbackToken*) override
			{
				return E_NOTIMPL;
			}
			HRESULT STDMETHODCALLTYPE RegisterKeyboardLayoutCallback(
				IGameInputDevice*, void*, GameInputKeyboardLayoutCallback, GameInputCallbackToken*) override
			{
				return E_NOTIMPL;
			}
			void STDMETHODCALLTYPE StopCallback(GameInputCallbackToken) override {}
			HRESULT STDMETHODCALLTYPE CreateDispatcher(IGameInputDispatcher**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE CreateAggregateDevice(
				GameInputKind, IGameInputDevice**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE FindDeviceFromId(
				APP_LOCAL_DEVICE_ID const*, IGameInputDevice**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE FindDeviceFromObject(
				IUnknown*, IGameInputDevice**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE FindDeviceFromPlatformHandle(
				HANDLE, IGameInputDevice**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE FindDeviceFromPlatformString(
				LPCWSTR, IGameInputDevice**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE EnableOemDeviceSupport(
				uint16_t, uint16_t, uint8_t, uint8_t) override { return E_NOTIMPL; }
			void STDMETHODCALLTYPE SetFocusPolicy(GameInputFocusPolicy) override {}

			std::function<void(IGameInputDevice*)> m_OnRead;
			std::vector<IGameInputDevice*> m_ReadDevices;
			HRESULT m_RegisterResult = S_OK;
			uint32_t m_UnfilteredReads = 0;
			uint32_t m_ReadFailures = 0;
			uint32_t m_UnregisterCalls = 0;
			uint64_t m_UnregisterTimeout = 0;
			bool m_BlockingEnumeration = false;
			bool HasCallbacks() const noexcept { return !m_Subscriptions.empty(); }

		private:
			struct Subscription
			{
				IGameInputDevice* m_Device;
				GameInputKind m_Kind;
				void* m_Context;
				GameInputDeviceCallback m_Callback;
				GameInputCallbackToken m_Token;
			};
			std::vector<ComPtr<TestGameInputDevice>> m_Devices;
			std::vector<Subscription> m_Subscriptions;
			GameInputCallbackToken m_NextToken = 0;
		};

		void RunKeyboardDeviceSelfTests(SelfTestContext& context)
		{
			ComPtr<TestGameInput> source;
			source.Attach(new TestGameInput());
			auto physical = source->AddDevice(GameInputKindKeyboard, 0x2efd, 0x7812);
			auto receiver = source->AddDevice(
				static_cast<GameInputKind>(GameInputKindKeyboard | GameInputKindMouse), 0x046d, 0xc548);
			physical->SetKeys({ 'W' });
			receiver->m_Timestamp = 200;
			{
				Keyboard keyboard(source.Get());
				keyboard.Update();
				context.Check(keyboard.IsAvailable() && keyboard.IsConnected() &&
					source->m_BlockingEnumeration && keyboard.IsKeyHeld(KeyCode::W) &&
					keyboard.IsKeyPressed(KeyCode::W) && source->m_UnfilteredReads == 0,
					"Per-device polling preserves held W when the latest global reading is an empty receiver");
				context.Check(source->m_ReadDevices.size() == 2 &&
					std::ranges::count(source->m_ReadDevices, physical.Get()) == 1 &&
					std::ranges::count(source->m_ReadDevices, receiver.Get()) == 1,
					"Each connected keyboard is polled once with its own device filter");

				++receiver->m_Timestamp;
				keyboard.Update();
				context.Check(keyboard.IsKeyHeld(KeyCode::W) && !keyboard.IsKeyPressed(KeyCode::W) &&
					!keyboard.IsKeyReleased(KeyCode::W),
					"New empty receiver reports neither release nor retrigger a key held on another keyboard");

				receiver->SetKeys({ 'D' });
				keyboard.Update();
				context.Check(keyboard.IsKeyHeld(KeyCode::W) && keyboard.IsKeyHeld(KeyCode::D) &&
					keyboard.IsKeyPressed(KeyCode::D),
					"Different keys from independent keyboards are available in the same frame");
				receiver->SetKeys({ 'W' });
				keyboard.Update();
				physical->SetKeys({});
				keyboard.Update();
				context.Check(keyboard.IsKeyHeld(KeyCode::W) && !keyboard.IsKeyReleased(KeyCode::W),
					"Releasing one keyboard does not release W while another keyboard still holds it");
				receiver->SetKeys({});
				keyboard.Update();
				context.Check(!keyboard.IsKeyHeld(KeyCode::W) && keyboard.IsKeyReleased(KeyCode::W),
					"Releasing the final contributor emits a merged key release");
				keyboard.Update();
				context.Check(!keyboard.IsKeyReleased(KeyCode::W),
					"Merged key release edges last for exactly one update");

				physical->SetKeys({ 'W' });
				receiver->m_ReadResult = E_FAIL;
				source->m_ReadFailures = 0;
				keyboard.Update();
				context.Check(keyboard.IsKeyHeld(KeyCode::W) && source->m_ReadFailures == 1,
					"A failed receiver reading does not discard a healthy keyboard's held keys");
				physical->m_ReadResult = E_FAIL;
				keyboard.Update();
				context.Check(!keyboard.IsKeyHeld(KeyCode::W) && keyboard.IsKeyReleased(KeyCode::W),
					"Failed devices contribute no stale held keys");
				physical->m_ReadResult = S_OK;
				receiver->m_ReadResult = S_OK;
				keyboard.Update();
				context.Check(keyboard.IsKeyHeld(KeyCode::W) && keyboard.IsKeyPressed(KeyCode::W),
					"Recovered device readings republish their current held state");

				keyboard.Reset();
				context.Check(!keyboard.IsKeyHeld(KeyCode::W) && !keyboard.IsKeyPressed(KeyCode::W) &&
					!keyboard.IsKeyReleased(KeyCode::W) && physical->GetReferenceCount() == 3 &&
					receiver->GetReferenceCount() == 3,
					"Focus reset clears held state and edges and releases polling snapshot references");
				physical->SetKeys({});
				keyboard.Update();
				context.Check(!keyboard.IsKeyHeld(KeyCode::W) && !keyboard.IsKeyReleased(KeyCode::W),
					"The next poll after focus reset rebuilds state without cached key contributions");

				source->SetConnected(*physical.Get(), true);
				source->m_ReadDevices.clear();
				keyboard.Update();
				context.Check(source->m_ReadDevices.size() == 2,
					"Repeated connected notifications do not duplicate keyboard devices");
				physical->SetKeys({ 'W' });
				receiver->SetKeys({ 'D' });
				keyboard.Update();
				source->SetConnected(*physical.Get(), false);
				keyboard.Update();
				context.Check(keyboard.IsKeyReleased(KeyCode::W) && keyboard.IsKeyHeld(KeyCode::D),
					"Disconnect removes only the departed keyboard's contribution");
				source->SetConnected(*receiver.Get(), false);
				keyboard.Update();
				context.Check(!keyboard.IsConnected() && keyboard.IsKeyReleased(KeyCode::D),
					"Disconnecting the final keyboard emits releases instead of leaving stuck keys");
				source->SetConnected(*physical.Get(), true);
				source->SetConnected(*receiver.Get(), true);
				keyboard.Update();
				context.Check(keyboard.IsKeyPressed(KeyCode::W) && keyboard.IsKeyHeld(KeyCode::D),
					"Reconnected keyboards contribute their current readings");

				bool disconnectedDuringRead = false;
				source->m_ReadFailures = 0;
				source->m_OnRead = [&](IGameInputDevice* device)
					{
						if (device == physical.Get() && !disconnectedDuringRead)
						{
							disconnectedDuringRead = true;
							source->SetConnected(*receiver.Get(), false);
						}
					};
				keyboard.Update();
				context.Check(disconnectedDuringRead && keyboard.IsKeyHeld(KeyCode::W) &&
					keyboard.IsKeyReleased(KeyCode::D) && source->m_ReadFailures == 1,
					"A disconnect during polling is safe and does not clear another device's state");
				source->m_OnRead = {};
				source->m_ReadDevices.clear();
				source->m_ReadFailures = 0;
				keyboard.Update();
				context.Check(source->m_ReadDevices.size() == 1 &&
					source->m_ReadDevices.front() == physical.Get() && source->m_ReadFailures == 0,
					"The next snapshot excludes the disconnected device");

				physical->m_Keys.clear();
				for (uint8_t key = '0'; key < '0' + 20; ++key)
				{
					physical->m_Keys.push_back(GameInputKeyState{ .virtualKey = key });
				}
				physical->m_Keys.push_back(GameInputKeyState{ .virtualKey = 'W' });
				keyboard.Update();
				context.Check(keyboard.IsKeyHeld(KeyCode::W),
					"Keyboard polling preserves the last reported key of a long simultaneous chord");
			}
			context.Check(!source->HasCallbacks() && source->m_UnregisterCalls == 1 &&
				source->m_UnregisterTimeout == UINT64_MAX && physical->GetReferenceCount() == 2 &&
				receiver->GetReferenceCount() == 2,
				"Keyboard destruction unregisters callbacks and releases device-list and polling references");

			source->m_RegisterResult = E_FAIL;
			Keyboard unavailable(source.Get());
			unavailable.Update();
			context.Check(!unavailable.IsAvailable() && !unavailable.IsKeyHeld(KeyCode::W),
				"Unavailable GameInput registration leaves keyboard state empty");
		}
	}

	void RunApplicationInputSelfTests(SelfTestContext& context) noexcept
	{
		RunKeyboardDeviceSelfTests(context);

		const std::span<const WindowsKeyMapping> mappings = GetWindowsKeyMappings();
		std::array<bool, AppInputKeyCount> mappedKeys{};
		bool mappingTableIsUnique = mappings.size() == AppInputKeyCount;
		for (const WindowsKeyMapping& mapping : mappings)
		{
			const size_t index = static_cast<size_t>(mapping.m_Key);
			if (index >= mappedKeys.size() || mappedKeys[index])
			{
				mappingTableIsUnique = false;
				continue;
			}
			mappedKeys[index] = true;
		}
		context.Check(mappingTableIsUnique && MapWindowsVirtualKey('W') == AppInputKey::W &&
			MapWindowsVirtualKey(0x1b) == AppInputKey::Escape &&
			MapWindowsVirtualKey(0x78) == AppInputKey::F9 &&
			MapWindowsVirtualKey(0xffff) == AppInputKey::Count,
			"Windows virtual keys map explicitly and uniquely to the neutral key contract");

		context.Check(!IsValidKeyCodeValue(-1) && IsValidKeyCodeValue(0) &&
			IsValidKeyCodeValue(0xfe) && !IsValidKeyCodeValue(0xff) &&
			!IsValidKeyCodeValue(0x100),
			"Windows keyboard storage rejects negative and out-of-range virtual keys");

		ApplicationInput input;
		ApplicationInputSnapshot snapshot{};
		snapshot.m_KeysHeld[static_cast<size_t>(AppInputKey::W)] = true;
		snapshot.m_KeysHeld[static_cast<size_t>(AppInputKey::LeftShift)] = true;
		snapshot.m_PointerDelta = { 3.0f, -4.0f };
		input.Publish(snapshot);
		const CameraInput cameraInput = BuildCameraInput(input);
		context.Check(cameraInput.m_Front && cameraInput.m_Accelerate &&
			cameraInput.m_IsMouseRelative && cameraInput.m_MouseDelta.m_X == 3.0f &&
			cameraInput.m_MouseDelta.m_Y == -4.0f,
			"Camera input is derived once from the neutral application state");

		DemoServices services{};
		services.m_AssetManager = reinterpret_cast<AssetManager*>(1);
		services.m_ShaderManager = reinterpret_cast<ShaderManager*>(1);
		services.m_TaskSystem = reinterpret_cast<TaskSystem*>(1);
		services.m_Input = &input;
		services.m_Time = reinterpret_cast<Time*>(1);
		services.m_DebugDraw = reinterpret_cast<DebugDrawContext*>(1);
		services.m_EnvironmentAssetController = reinterpret_cast<EnvironmentAssetController*>(1);
		services.m_EnvironmentLighting = reinterpret_cast<EnvironmentLightingViewBase*>(1);
		services.m_EnvironmentLightingControl = reinterpret_cast<EnvironmentLightingControlBase*>(1);
		services.m_IBLCacheControl = reinterpret_cast<IBLCacheControlBase*>(1);
		services.m_RHIContext = reinterpret_cast<RHIContext*>(1);
		context.Check(!services.IsValid(),
			"DemoServices rejects an empty required render service bundle");
		services.m_RenderServices.m_PipelineResolver = reinterpret_cast<RenderPipelineResolver*>(1);
		services.m_RenderServices.m_ShaderPrograms = reinterpret_cast<RenderShaderProgramAccess*>(1);
		services.m_RenderServices.m_Samplers = reinterpret_cast<RenderSamplerAccess*>(1);
		services.m_RenderServices.m_Resources = reinterpret_cast<RenderResourceRegistryAccess*>(1);
		services.m_RenderServices.m_FrameBuffers = reinterpret_cast<RenderFrameBufferAccess*>(1);
		services.m_RenderServices.m_Environment = reinterpret_cast<RenderEnvironmentAccess*>(1);
		services.m_RenderServices.m_Presentation = reinterpret_cast<RenderPresentationAccess*>(1);
		services.m_RenderServices.m_BindingLayout = reinterpret_cast<RenderBindingLayoutAccess*>(1);
		services.m_RenderServices.m_Temporal = reinterpret_cast<RenderTemporalAccess*>(1);
		services.m_RenderServices.m_AssetUpload = reinterpret_cast<AssetUploadScheduling*>(1);
		context.Check(services.IsValid(),
			"DemoServices accepts the neutral application input contract");
		services.m_Input = nullptr;
		context.Check(!services.IsValid(),
			"DemoServices still rejects a missing required input service");
		services.m_Input = &input;
		services.m_RHIContext = nullptr;
		context.Check(!services.IsValid(), "DemoServices rejects a missing required RHI context");
		services.m_RHIContext = reinterpret_cast<RHIContext*>(1);
		services.m_GpuProfiling = nullptr;
		context.Check(services.IsValid(),
			"DemoServices keeps GPU profiling and asset upload control optional");
	}
}
