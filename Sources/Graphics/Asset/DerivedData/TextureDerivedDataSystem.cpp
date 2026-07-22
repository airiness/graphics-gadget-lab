#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/TextureDerivedDataSystem.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"

#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace gglab
{
	struct TextureDerivedDataRequestState
	{
		DerivedDataKey m_Key{};
		uint64_t m_BuildSerial = 0;
		uint32_t m_ParticipantCount = 0;
		TextureDerivedDataArtifact m_Artifact;
		std::string m_Error;
		bool m_Complete = false;
		bool m_Published = false;
		std::condition_variable_any m_Completion;
	};

	struct TextureDerivedDataCoordinatorCore
	{
		std::mutex m_Mutex;
		std::unordered_map<
			DerivedDataKey,
			std::shared_ptr<TextureDerivedDataRequestState>,
			DerivedDataKeyHash> m_Requests;
		TextureDerivedDataCoordinatorStatistics m_Statistics{};
		uint64_t m_NextBuildSerial = 1;
	};

	namespace
	{
		void EraseCompletedRequestLocked(
			TextureDerivedDataCoordinatorCore& core,
			const std::shared_ptr<TextureDerivedDataRequestState>& state) noexcept
		{
			if (!state->m_Complete || state->m_ParticipantCount != 0)
			{
				return;
			}
			const auto request = core.m_Requests.find(state->m_Key);
			if (request != core.m_Requests.end() && request->second == state)
			{
				core.m_Requests.erase(request);
			}
		}
	}

	bool FinishTextureDerivedDataBuild(
		TextureArtifactBuildClaim& claim,
		TextureDerivedDataArtifact artifact,
		std::string error) noexcept
	{
		if (!claim.m_Core || !claim.m_State || claim.m_Serial == 0)
		{
			return false;
		}
		const auto core = claim.m_Core;
		const auto state = claim.m_State;
		{
			std::scoped_lock lock(core->m_Mutex);
			if (state->m_Complete || state->m_BuildSerial != claim.m_Serial)
			{
				return false;
			}
			state->m_Artifact = std::move(artifact);
			state->m_Error = std::move(error);
			state->m_Complete = true;
			state->m_Published = state->m_Artifact.IsValid();
			if (state->m_Published)
			{
				++core->m_Statistics.m_PublishCount;
				core->m_Statistics.m_FanoutDeliveryCount += state->m_ParticipantCount;
			}
			else
			{
				++core->m_Statistics.m_BuildFailureCount;
				if (state->m_Error.empty())
				{
					state->m_Error = "Texture artifact producer abandoned its build claim.";
				}
			}
			GGLAB_ASSERT(core->m_Statistics.m_ActiveBuildCount > 0);
			--core->m_Statistics.m_ActiveBuildCount;
			EraseCompletedRequestLocked(*core, state);
		}
		state->m_Completion.notify_all();
		claim.m_Core.reset();
		claim.m_State.reset();
		claim.m_Serial = 0;
		return true;
	}

	TextureArtifactWaiterHandle::TextureArtifactWaiterHandle(
		std::shared_ptr<TextureDerivedDataCoordinatorCore> core,
		std::shared_ptr<TextureDerivedDataRequestState> state) noexcept :
		m_Core(std::move(core)),
		m_State(std::move(state))
	{}

	TextureArtifactWaiterHandle::TextureArtifactWaiterHandle(
		TextureArtifactWaiterHandle&& other) noexcept = default;

	TextureArtifactWaiterHandle& TextureArtifactWaiterHandle::operator=(
		TextureArtifactWaiterHandle&& other) noexcept
	{
		if (this != &other)
		{
			Release(false);
			m_Core = std::move(other.m_Core);
			m_State = std::move(other.m_State);
		}
		return *this;
	}

	TextureArtifactWaiterHandle::~TextureArtifactWaiterHandle()
	{
		Release(false);
	}

	bool TextureArtifactWaiterHandle::IsValid() const noexcept
	{
		return m_Core && m_State;
	}

	bool TextureArtifactWaiterHandle::Cancel() noexcept
	{
		if (!IsValid())
		{
			return false;
		}
		Release(true);
		return true;
	}

	void TextureArtifactWaiterHandle::Release(bool cancelled) noexcept
	{
		if (!m_Core || !m_State)
		{
			return;
		}
		const auto core = std::move(m_Core);
		const auto state = std::move(m_State);
		{
			std::scoped_lock lock(core->m_Mutex);
			GGLAB_ASSERT(state->m_ParticipantCount > 0);
			if (state->m_ParticipantCount > 0)
			{
				--state->m_ParticipantCount;
				GGLAB_ASSERT(core->m_Statistics.m_ActiveWaiterCount > 0);
				--core->m_Statistics.m_ActiveWaiterCount;
			}
			if (cancelled)
			{
				++core->m_Statistics.m_CancelledWaiterCount;
			}
			// The active request remains discoverable until its BuildClaim finishes.
			// Participant cancellation must not create a second producer for the same key.
			EraseCompletedRequestLocked(*core, state);
		}
	}

	TextureArtifactBuildClaim::TextureArtifactBuildClaim(
		std::shared_ptr<TextureDerivedDataCoordinatorCore> core,
		std::shared_ptr<TextureDerivedDataRequestState> state,
		uint64_t serial) noexcept :
		m_Core(std::move(core)),
		m_State(std::move(state)),
		m_Serial(serial)
	{}

	TextureArtifactBuildClaim::TextureArtifactBuildClaim(
		TextureArtifactBuildClaim&& other) noexcept = default;

	TextureArtifactBuildClaim& TextureArtifactBuildClaim::operator=(
		TextureArtifactBuildClaim&& other) noexcept
	{
		if (this != &other)
		{
			Abandon();
			m_Core = std::move(other.m_Core);
			m_State = std::move(other.m_State);
			m_Serial = std::exchange(other.m_Serial, 0);
		}
		return *this;
	}

	TextureArtifactBuildClaim::~TextureArtifactBuildClaim()
	{
		Abandon();
	}

	bool TextureArtifactBuildClaim::IsValid() const noexcept
	{
		return m_Core && m_State && m_Serial != 0;
	}

	void TextureArtifactBuildClaim::Abandon() noexcept
	{
		GGLAB_UNUSED(FinishTextureDerivedDataBuild(*this, {}, {}));
	}

	TextureDerivedDataSystem::TextureDerivedDataSystem(
		std::filesystem::path cacheDirectory) noexcept :
		m_Core(std::make_shared<TextureDerivedDataCoordinatorCore>()),
		m_Store(std::move(cacheDirectory))
	{}

	TextureDerivedDataSystem::~TextureDerivedDataSystem()
	{
		std::scoped_lock lock(m_Core->m_Mutex);
		GGLAB_ASSERT_MSG(
			m_Core->m_Requests.empty(),
			"TextureDerivedDataSystem destroyed with active requests.");
	}

	TextureDerivedDataRequestResult TextureDerivedDataSystem::Request(
		const DerivedDataKey& key) noexcept
	{
		TextureDerivedDataRequestResult result{};
		if (!key.IsValid())
		{
			return result;
		}

		std::scoped_lock lock(m_Core->m_Mutex);
		++m_Core->m_Statistics.m_RequestCount;
		if (const auto request = m_Core->m_Requests.find(key);
			request != m_Core->m_Requests.end())
		{
			const auto& state = request->second;
			if (state->m_Complete && state->m_Published)
			{
				result.m_Disposition = ArtifactRequestDisposition::Hit;
				result.m_Artifact = state->m_Artifact;
				++m_Core->m_Statistics.m_ImmediateHitCount;
				return result;
			}
			++state->m_ParticipantCount;
			++m_Core->m_Statistics.m_ActiveWaiterCount;
			++m_Core->m_Statistics.m_WaitCount;
			result.m_Disposition = ArtifactRequestDisposition::Waiting;
			result.m_Waiter = TextureArtifactWaiterHandle(m_Core, state);
			return result;
		}

		auto state = std::make_shared<TextureDerivedDataRequestState>();
		state->m_Key = key;
		state->m_BuildSerial = m_Core->m_NextBuildSerial++;
		state->m_ParticipantCount = 1;
		m_Core->m_Requests.emplace(key, state);
		++m_Core->m_Statistics.m_ActiveBuildCount;
		++m_Core->m_Statistics.m_ActiveWaiterCount;
		++m_Core->m_Statistics.m_BuildRequiredCount;
		result.m_Disposition = ArtifactRequestDisposition::BuildRequired;
		result.m_Waiter = TextureArtifactWaiterHandle(m_Core, state);
		result.m_BuildClaim = TextureArtifactBuildClaim(
			m_Core,
			state,
			state->m_BuildSerial);
		return result;
	}

	TextureArtifactWaitResult TextureDerivedDataSystem::Wait(
		TextureArtifactWaiterHandle waiter,
		std::stop_token stopToken) noexcept
	{
		TextureArtifactWaitResult result{};
		if (!waiter.IsValid())
		{
			result.m_Error = "Invalid texture artifact waiter.";
			return result;
		}
		const auto core = waiter.m_Core;
		const auto state = waiter.m_State;
		std::unique_lock lock(core->m_Mutex);
		const bool complete = state->m_Completion.wait(
			lock,
			stopToken,
			[&state]() noexcept { return state->m_Complete; });
		if (!complete)
		{
			lock.unlock();
			waiter.Cancel();
			result.m_Disposition = ArtifactWaitDisposition::Cancelled;
			return result;
		}
		result.m_Artifact = state->m_Artifact;
		result.m_Error = state->m_Error;
		result.m_Disposition = state->m_Published ?
			ArtifactWaitDisposition::Succeeded : ArtifactWaitDisposition::Failed;
		return result;
	}

	bool TextureDerivedDataSystem::Publish(
		TextureArtifactBuildClaim claim,
		TextureDerivedDataArtifact artifact) noexcept
	{
		if (!artifact.IsValid())
		{
			return false;
		}
		return FinishTextureDerivedDataBuild(claim, std::move(artifact), {});
	}

	bool TextureDerivedDataSystem::Fail(
		TextureArtifactBuildClaim claim,
		std::string error) noexcept
	{
		return FinishTextureDerivedDataBuild(claim, {}, std::move(error));
	}

	TextureDerivedDataArtifact TextureDerivedDataSystem::Read(
		const DerivedDataKey& key,
		const TextureImportSettings& importSettings) noexcept
	{
		const LocalDerivedDataReadOptions readOptions{
			.m_MaxContainerBytes = ComputeLocalDerivedDataContainerByteLimit(
				TextureArtifactType,
				TextureArtifactCodec::GetMaximumSerializedBytes()),
		};
		DerivedDataReadResult cached = m_Store.Read(
			key,
			TextureArtifactType,
			TextureArtifactSchemaVersion,
			readOptions);
		if (cached.m_Disposition != DerivedDataReadDisposition::Hit)
		{
			return {};
		}
		TextureArtifactDecodeResult decoded = TextureArtifactCodec::Deserialize(
			cached.m_Payload,
			cached.m_ArtifactContentDigest);
		if (!decoded.Succeeded())
		{
			m_Store.DiscardObservedCorrupt(
				key,
				TextureArtifactType,
				TextureArtifactSchemaVersion,
				cached.m_ArtifactContentDigest,
				cached.m_PayloadDigest,
				readOptions);
			GGLAB_LOG_GRAPHICS_WARN(
				"Texture DDC entry '{}' failed codec validation: {}",
				DerivedDataKeyText(key),
				decoded.m_Error);
			return {};
		}
		const AssetContentFingerprint contentFingerprint =
			ComputeTextureContentFingerprint(decoded.m_Artifact.m_Data, importSettings);
		TextureDerivedDataArtifact result{
			.m_Artifact = std::make_shared<const TextureArtifact>(
				std::move(decoded.m_Artifact)),
			.m_ContentFingerprint = contentFingerprint,
			.m_DerivedDataCacheHit = true,
		};
		return result.IsValid() ? result : TextureDerivedDataArtifact{};
	}

	bool TextureDerivedDataSystem::Write(
		const DerivedDataKey& key,
		const TextureArtifact& artifact) noexcept
	{
		const std::vector<std::byte> payload = TextureArtifactCodec::Serialize(artifact);
		return !payload.empty() && m_Store.Write(
			key,
			TextureArtifactType,
			TextureArtifactSchemaVersion,
			artifact.m_ContentDigest,
			payload);
	}

	TextureDerivedDataCoordinatorStatistics
	TextureDerivedDataSystem::GetCoordinatorStatistics() const noexcept
	{
		std::scoped_lock lock(m_Core->m_Mutex);
		return m_Core->m_Statistics;
	}
}
