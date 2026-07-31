#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Graphics/Asset/TextureArtifact.h"

#include <memory>
#include <stop_token>
#include <string>

namespace gglab
{
	struct TextureDerivedDataCoordinatorCore;
	struct TextureDerivedDataRequestState;

	enum class ArtifactRequestDisposition : uint8_t
	{
		Hit,
		Waiting,
		BuildRequired,
	};

	struct TextureDerivedDataArtifact
	{
		TextureArtifactHandle m_Artifact;
		AssetContentFingerprint m_ContentFingerprint{};
		bool m_DerivedDataCacheHit = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Artifact && m_Artifact->IsValid() && m_ContentFingerprint.IsValid();
		}
	};

	class TextureArtifactWaiterHandle final
	{
	public:
		TextureArtifactWaiterHandle() noexcept = default;
		TextureArtifactWaiterHandle(TextureArtifactWaiterHandle&& other) noexcept;
		TextureArtifactWaiterHandle& operator=(TextureArtifactWaiterHandle&& other) noexcept;
		GGLAB_DELETE_COPYABLE(TextureArtifactWaiterHandle);
		~TextureArtifactWaiterHandle();

		[[nodiscard]] bool IsValid() const noexcept;
		bool Cancel() noexcept;

	private:
		friend class TextureDerivedDataSystem;
		TextureArtifactWaiterHandle(std::shared_ptr<TextureDerivedDataCoordinatorCore> core,
			std::shared_ptr<TextureDerivedDataRequestState> state) noexcept;
		void Release(bool cancelled) noexcept;

		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		std::shared_ptr<TextureDerivedDataRequestState> m_State;
	};

	class TextureArtifactBuildClaim final
	{
	public:
		TextureArtifactBuildClaim() noexcept = default;
		TextureArtifactBuildClaim(TextureArtifactBuildClaim&& other) noexcept;
		TextureArtifactBuildClaim& operator=(TextureArtifactBuildClaim&& other) noexcept;
		GGLAB_DELETE_COPYABLE(TextureArtifactBuildClaim);
		~TextureArtifactBuildClaim();

		[[nodiscard]] bool IsValid() const noexcept;

	private:
		friend class TextureDerivedDataSystem;
		friend bool FinishTextureDerivedDataBuild(TextureArtifactBuildClaim& claim,
			TextureDerivedDataArtifact artifact, std::string error) noexcept;
		TextureArtifactBuildClaim(std::shared_ptr<TextureDerivedDataCoordinatorCore> core,
			std::shared_ptr<TextureDerivedDataRequestState> state, uint64_t serial) noexcept;
		void Abandon() noexcept;

		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		std::shared_ptr<TextureDerivedDataRequestState> m_State;
		uint64_t m_Serial = 0;
	};

	struct TextureDerivedDataRequestResult
	{
		ArtifactRequestDisposition m_Disposition = ArtifactRequestDisposition::Waiting;
		TextureDerivedDataArtifact m_Artifact;
		TextureArtifactWaiterHandle m_Waiter;
		TextureArtifactBuildClaim m_BuildClaim;
	};

	enum class ArtifactWaitDisposition : uint8_t
	{
		Succeeded,
		Failed,
		Cancelled,
	};

	struct TextureArtifactWaitResult
	{
		ArtifactWaitDisposition m_Disposition = ArtifactWaitDisposition::Failed;
		TextureDerivedDataArtifact m_Artifact;
		std::string m_Error;
	};

	struct TextureDerivedDataCoordinatorStatistics
	{
		uint32_t m_ActiveBuildCount = 0;
		uint32_t m_ActiveWaiterCount = 0;
		uint64_t m_RequestCount = 0;
		uint64_t m_ImmediateHitCount = 0;
		uint64_t m_WaitCount = 0;
		uint64_t m_BuildRequiredCount = 0;
		uint64_t m_PublishCount = 0;
		uint64_t m_BuildFailureCount = 0;
		uint64_t m_CancelledWaiterCount = 0;
		uint64_t m_FanoutDeliveryCount = 0;
	};

	class TextureDerivedDataSystem final
	{
	public:
		explicit TextureDerivedDataSystem(std::filesystem::path cacheDirectory) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureDerivedDataSystem);
		~TextureDerivedDataSystem();

		[[nodiscard]] TextureDerivedDataRequestResult Request(const DerivedDataKey& key) noexcept;
		[[nodiscard]] TextureArtifactWaitResult Wait(
			TextureArtifactWaiterHandle waiter, std::stop_token stopToken) noexcept;
		[[nodiscard]] bool Publish(
			TextureArtifactBuildClaim claim, TextureDerivedDataArtifact artifact) noexcept;
		[[nodiscard]] bool Fail(TextureArtifactBuildClaim claim, std::string error) noexcept;

		[[nodiscard]] TextureDerivedDataArtifact Read(
			const DerivedDataKey& key, const TextureImportSettings& importSettings) noexcept;
		[[nodiscard]] bool Write(
			const DerivedDataKey& key, const TextureArtifact& artifact) noexcept;

		[[nodiscard]] TextureDerivedDataCoordinatorStatistics GetCoordinatorStatistics()
			const noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStoreStatistics() const noexcept
		{
			return m_Store.GetStatistics();
		}
		[[nodiscard]] bool Contains(const DerivedDataKey& key) const noexcept
		{
			return m_Store.Contains(key);
		}
		[[nodiscard]] bool Clear() noexcept { return m_Store.Clear(); }

	private:
		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		LocalDerivedDataStore m_Store;
	};
}
