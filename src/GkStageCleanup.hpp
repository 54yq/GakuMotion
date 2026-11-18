#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace GakuMotion
{
	// Process-lived ownership records for additive stage requests whose manager
	// or actor Scene was abandoned. Only integer Scene identities survive.
	class StageOrphanLedger
	{
	public:
		struct Entry { std::string key; std::vector<int> scenes; bool observed{false}; };
		std::vector<Entry> entries;
		void Remember(std::string_view key, const std::vector<int>& knownScenes = {})
		{
			if (key.empty() || BlocksMount(key)) return;
			entries.push_back({std::string(key), knownScenes, !knownScenes.empty()});
		}
		bool BlocksMount(std::string_view key) const
		{
			for (const auto& e : entries) if (e.key == key) return true;
			return false;
		}
		bool Pending() const
		{
			for (const auto& e : entries) if (!e.observed) return true;
			return false;
		}
		// The first exact-key scene appearing after an unfinished load is the
		// retired request's cleanup candidate. Once observed, do not capture a
		// later different Scene handle that happens to reuse the resource key.
		bool ShouldHide(std::string_view key, int scene)
		{
			if (!scene) return false;
			for (auto& e : entries) if (e.key == key)
			{
				if (!e.observed) { e.scenes.push_back(scene); e.observed = true; }
				return std::find(e.scenes.begin(), e.scenes.end(), scene) != e.scenes.end();
			}
			return false;
		}
		void FinishSnapshot(const std::vector<int>& present)
		{
			// A Unity Scene handle can be reused after unload. Once an owned
			// scene disappears, never attach its old integer token to a new scene.
			for (auto& e : entries)
				e.scenes.erase(std::remove_if(e.scenes.begin(), e.scenes.end(), [&](int scene)
					{ return std::find(present.begin(), present.end(), scene) == present.end(); }), e.scenes.end());
		}
	};
}
