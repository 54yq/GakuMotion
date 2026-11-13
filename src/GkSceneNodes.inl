// Included inside StageManager. No Unity pointer leaves this provider.
private:
    using SceneNodesStatus = scene_nodes::v1::Status;
    struct SceneNodeIdentity
    {
        void* object{};
        int id{};
        uint64_t generation{};
        bool operator==(const SceneNodeIdentity&) const = default;
    };
    struct SceneNodeEntry
    {
        SceneNodeIdentity identity;
        std::vector<SceneNodeIdentity> ancestors;
        uint64_t handle{};
        std::string name;
        bool active{}, baseline{};
    };
    std::vector<SceneNodeEntry> sceneNodesCatalog_;
    uint64_t sceneNodesRevision_{1}, sceneNodesNextHandle_{1}, sceneNodesNextLease_{1}, sceneNodesLease_{};
    bool sceneNodesAllowed_{}, sceneNodesAvailable_{}, sceneNodesReady_{}, sceneNodesRestorePending_{};

    static std::string SceneNodeLower(std::string value)
    {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return value;
    }
    static SceneNodeIdentity SceneNodeId(const StageCandidate& candidate)
    {
        return {candidate.gameObject, candidate.instanceId, candidate.generation};
    }
    static std::string SceneNodeShortName(const StageCandidate& candidate)
    {
        if (!candidate.displayName.empty()) return candidate.displayName;
        const auto separator = candidate.name.rfind(" / ");
        return separator == std::string::npos ? candidate.name : candidate.name.substr(separator + 3);
    }
    template<size_t N> static void SceneNodeText(char (&output)[N], const std::string& value)
    {
        size_t length = (std::min)(N - 1, value.size());
        while (length && length < value.size() && (static_cast<unsigned char>(value[length]) & 0xc0) == 0x80) --length;
        std::memcpy(output, value.data(), length);
        output[length] = '\0';
    }
    bool SceneNodeMatches(const SceneNodeIdentity& identity) const
    {
        return identity.object && identity.id && identity.generation && api.fnMatches &&
            api.fnMatches(identity.object, identity.id, identity.generation);
    }
    void SceneNodePublishActive(const SceneNodeIdentity& identity, bool active)
    {
        for (auto& candidate : candidates)
            if (SceneNodeId(candidate) == identity) candidate.currentlyActive = active;
    }
    bool SceneNodesWriterBusy()
    {
        if (!sceneNodesLease_ && !sceneNodesRestorePending_) return false;
        statusNotice = "Scene nodes are leased; release or restore before changing stage controls";
        return true;
    }
    bool SceneNodesOwnsCandidate(const StageCandidate& candidate) const
    {
        if (!sceneNodesLease_) return false;
        const auto identity = SceneNodeId(candidate);
        for (const auto& entry : sceneNodesCatalog_)
            if (entry.identity == identity || std::find(entry.ancestors.begin(), entry.ancestors.end(), identity) != entry.ancestors.end()) return true;
        return false;
    }
    void InvalidateSceneNodesLocked()
    {
        ++sceneNodesRevision_;
        // A lost identity requires a new detected hierarchy, not repeated
        // reacquisition of the same stale candidate cache.
        candidatesDiscovered = false;
        sceneNodesCatalog_.clear();
        sceneNodesLease_ = 0;
        sceneNodesRestorePending_ = false;
        sceneNodesReady_ = false;
    }
    SceneNodesStatus CheckSceneNodesLiveLocked(bool requireActiveAncestors)
    {
        if (!api.fnMatches || !api.fnTryGetActiveSelf || !api.fnGeneration || !api.fnSetActive) return SceneNodesStatus::Unavailable;
        const auto generation = api.fnGeneration();
        for (const auto& entry : sceneNodesCatalog_)
        {
            if (entry.identity.generation != generation || !SceneNodeMatches(entry.identity)) return SceneNodesStatus::StaleCatalog;
            for (const auto& ancestor : entry.ancestors)
            {
                if (ancestor.generation != generation || !SceneNodeMatches(ancestor)) return SceneNodesStatus::StaleCatalog;
                if (requireActiveAncestors)
                {
                    bool active{};
                    if (!api.fnTryGetActiveSelf(ancestor.object, active)) return SceneNodesStatus::Failed;
                    if (!active) return SceneNodesStatus::NotReady;
                }
            }
        }
        return SceneNodesStatus::Ok;
    }
    SceneNodesStatus RestoreSceneNodesLeaseLocked()
    {
        if (!sceneNodesLease_) return SceneNodesStatus::Ok;
        StageAccessScope access(api);
        if (!access.ready) { sceneNodesRestorePending_ = true; return SceneNodesStatus::Failed; }
        if (!api.fnGeneration || !api.fnMatches || !api.fnSetActive || !api.fnTryGetActiveSelf)
        { sceneNodesRestorePending_ = true; return SceneNodesStatus::Failed; }
        const auto generation = api.fnGeneration();
        if (!sceneNodesCatalog_.empty() && sceneNodesCatalog_.front().identity.generation != generation)
        { InvalidateSceneNodesLocked(); return SceneNodesStatus::StaleCatalog; }
        bool complete = true, stale = false;
        for (auto& entry : sceneNodesCatalog_)
        {
            // Losing one identity does not erase the remaining live objects'
            // restoration responsibility. Never invoke a stale/reused pointer.
            if (!SceneNodeMatches(entry.identity)) { stale = true; continue; }
            const bool set = api.fnSetActive(entry.identity.object, entry.baseline);
            bool actual{};
            const bool read = api.fnTryGetActiveSelf(entry.identity.object, actual);
            if (read) { entry.active = actual; SceneNodePublishActive(entry.identity, actual); }
            if (!set || !read || actual != entry.baseline) { complete = false; continue; }
        }
        sceneNodesRestorePending_ = !complete;
        if (!complete) return SceneNodesStatus::Failed;
        sceneNodesLease_ = 0;
        if (stale) { InvalidateSceneNodesLocked(); return SceneNodesStatus::StaleCatalog; }
        return SceneNodesStatus::Ok;
    }

public:
    void RefreshSceneNodes(bool allowed)
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        sceneNodesAllowed_ = allowed;
        sceneNodesAvailable_ = api.fnSetActive && api.fnTryGetActiveSelf && api.fnMatches && api.fnGeneration;
        if (!allowed || !sceneNodesAvailable_ || !candidatesDiscovered)
        {
            sceneNodesReady_ = false;
            if (sceneNodesLease_) RestoreSceneNodesLeaseLocked();
            return;
        }
        std::vector<SceneNodeEntry> next;
        bool ready = true;
        for (const auto& candidate : candidates)
        {
            const int parent = candidate.parentIndex;
            if (parent < 0 || parent >= static_cast<int>(candidates.size())) continue;
            if (SceneNodeLower(candidates[parent].displayName) != "cmn-parts") continue;
            const auto shortName = SceneNodeShortName(candidate);
            const auto lower = SceneNodeLower(shortName);
            if (lower == "cmn-parts" || lower.find("floor") != std::string::npos ||
                candidate.isColliderLocked || candidate.isLightingRoot || candidate.isLightingNode ||
                !candidate.isIdentifiedBackground || IsActorOrProtectedNode(candidate.name) ||
                IsActorOrProtectedNode(shortName) || IsColliderOrBoundNode(shortName)) continue;
            SceneNodeEntry entry;
            entry.identity = SceneNodeId(candidate);
            entry.name = shortName;
            entry.active = candidate.currentlyActive;
            if (!entry.identity.object || !entry.identity.id || !entry.identity.generation) { ready = false; continue; }
            int ancestor = parent;
            size_t depth = 0;
            while (ancestor >= 0 && ancestor < static_cast<int>(candidates.size()) && depth++ < candidates.size())
            {
                const auto& value = candidates[ancestor];
                const auto identity = SceneNodeId(value);
                if (!identity.object || !identity.id || identity.generation != entry.identity.generation ||
                    !value.currentlyActive) ready = false;
                entry.ancestors.push_back(identity);
                ancestor = value.parentIndex;
            }
            if (ancestor >= 0) ready = false;
            next.push_back(std::move(entry));
        }
        bool changed = next.size() != sceneNodesCatalog_.size();
        if (!changed) for (size_t i = 0; i < next.size(); ++i)
            if (next[i].identity != sceneNodesCatalog_[i].identity || next[i].ancestors != sceneNodesCatalog_[i].ancestors ||
                next[i].name != sceneNodesCatalog_[i].name) { changed = true; break; }
        if (changed)
        {
            const auto restored = RestoreSceneNodesLeaseLocked();
            if (restored != SceneNodesStatus::Ok && restored != SceneNodesStatus::StaleCatalog)
            { sceneNodesReady_ = false; return; }
            ++sceneNodesRevision_;
            for (auto& entry : next)
            {
                entry.handle = sceneNodesNextHandle_++;
                for (const auto& candidate : candidates)
                    if (SceneNodeId(candidate) == entry.identity) { entry.active = candidate.currentlyActive; break; }
            }
            sceneNodesCatalog_ = std::move(next);
        }
        else for (size_t i = 0; i < next.size(); ++i) sceneNodesCatalog_[i].active = next[i].active;
        sceneNodesReady_ = ready && !sceneNodesCatalog_.empty();
    }

    SceneNodesStatus ReadSceneNodesState(uint32_t size, scene_nodes::v1::State* output)
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        if (!output) return SceneNodesStatus::InvalidArgument;
        if (size != sizeof(*output) || output->structSize != sizeof(*output)) return SceneNodesStatus::SizeMismatch;
        if (output->apiVersion != scene_nodes::v1::Version) return SceneNodesStatus::VersionMismatch;
        scene_nodes::v1::State state;
        state.available = sceneNodesAvailable_;
        state.ready = sceneNodesAllowed_ && sceneNodesReady_ && !sceneNodesRestorePending_;
        state.count = static_cast<uint32_t>(sceneNodesCatalog_.size());
        state.owned = sceneNodesLease_ != 0;
        state.restorePending = sceneNodesRestorePending_;
        state.catalogRevision = sceneNodesRevision_;
        state.status = !sceneNodesAllowed_ ? SceneNodesStatus::Protected : !sceneNodesAvailable_ ? SceneNodesStatus::Unavailable :
            sceneNodesRestorePending_ ? SceneNodesStatus::Failed : !state.ready ? SceneNodesStatus::NotReady :
            sceneNodesLease_ ? SceneNodesStatus::Busy : SceneNodesStatus::Ok;
        SceneNodeText(state.message, sceneNodesRestorePending_ ? "Restore pending; retry release before another apply" :
            !state.ready ? "Approved scene nodes are not ready" : sceneNodesLease_ ? "Catalog exclusively leased" : "Ready");
        *output = state;
        return SceneNodesStatus::Ok;
    }

    SceneNodesStatus ReadSceneNode(uint64_t revision, uint32_t index, uint32_t size, scene_nodes::v1::Node* output)
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        if (!output) return SceneNodesStatus::InvalidArgument;
        if (size != sizeof(*output) || output->structSize != sizeof(*output)) return SceneNodesStatus::SizeMismatch;
        if (output->apiVersion != scene_nodes::v1::Version) return SceneNodesStatus::VersionMismatch;
        if (revision != sceneNodesRevision_) return SceneNodesStatus::StaleCatalog;
        if (index >= sceneNodesCatalog_.size()) return SceneNodesStatus::InvalidArgument;
        const auto& entry = sceneNodesCatalog_[index];
        scene_nodes::v1::Node node;
        node.handle = entry.handle;
        node.active = entry.active;
        node.ordinal = index;
        SceneNodeText(node.name, entry.name);
        *output = node;
        return SceneNodesStatus::Ok;
    }

    SceneNodesStatus AcquireSceneNodes(uint64_t revision, uint64_t* lease)
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        if (!lease) return SceneNodesStatus::InvalidArgument;
        *lease = 0;
        if (revision != sceneNodesRevision_) return SceneNodesStatus::StaleCatalog;
        if (sceneNodesLease_) return SceneNodesStatus::Busy;
        if (!sceneNodesAllowed_) return SceneNodesStatus::Protected;
        if (!sceneNodesReady_) return SceneNodesStatus::NotReady;
        StageAccessScope access(api);
        if (!access.ready) return SceneNodesStatus::NotReady;
        const auto live = CheckSceneNodesLiveLocked(true);
        if (live == SceneNodesStatus::StaleCatalog) InvalidateSceneNodesLocked();
        if (live != SceneNodesStatus::Ok) return live;
        std::vector<bool> baseline;
        for (const auto& entry : sceneNodesCatalog_)
        {
            bool active{};
            if (!api.fnTryGetActiveSelf(entry.identity.object, active)) return SceneNodesStatus::Failed;
            baseline.push_back(active);
        }
        for (size_t i = 0; i < baseline.size(); ++i)
        {
            auto& entry = sceneNodesCatalog_[i];
            entry.active = entry.baseline = baseline[i];
            SceneNodePublishActive(entry.identity, entry.active);
            // The fresh untouched native value supersedes a stale discovery
            // snapshot. Never overwrite an existing stage/manual restore debt.
            for (auto& candidate : candidates)
                if (SceneNodeId(candidate) == entry.identity && !candidate.injectionTouched)
                { candidate.originalActive = entry.active; candidate.originalStateConfirmed = true; }
        }
        sceneNodesLease_ = sceneNodesNextLease_++;
        sceneNodesRestorePending_ = false;
        *lease = sceneNodesLease_;
        return SceneNodesStatus::Ok;
    }

    SceneNodesStatus ApplySceneNodes(uint64_t lease, uint64_t revision, uint32_t count, const scene_nodes::v1::Visibility* values)
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        if (!lease || lease != sceneNodesLease_ || revision != sceneNodesRevision_) return SceneNodesStatus::StaleCatalog;
        if (sceneNodesRestorePending_) return SceneNodesStatus::Failed;
        if (!sceneNodesAllowed_) return SceneNodesStatus::Protected;
        if (!sceneNodesReady_) return SceneNodesStatus::NotReady;
        if (!values || count != sceneNodesCatalog_.size()) return SceneNodesStatus::InvalidArgument;
        std::vector<int> targets(count, -1);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (values[i].active > 1 || values[i].reserved) return SceneNodesStatus::InvalidArgument;
            const auto match = std::find_if(sceneNodesCatalog_.begin(), sceneNodesCatalog_.end(),
                [&](const SceneNodeEntry& entry) { return entry.handle == values[i].handle; });
            if (match == sceneNodesCatalog_.end()) return SceneNodesStatus::InvalidArgument;
            const auto index = static_cast<size_t>(match - sceneNodesCatalog_.begin());
            if (targets[index] != -1) return SceneNodesStatus::InvalidArgument;
            targets[index] = static_cast<int>(values[i].active);
        }
        StageAccessScope access(api);
        if (!access.ready) return SceneNodesStatus::NotReady;
        const auto live = CheckSceneNodesLiveLocked(true);
        if (live == SceneNodesStatus::StaleCatalog)
        {
            if (!sceneNodesCatalog_.empty() && api.fnGeneration &&
                sceneNodesCatalog_.front().identity.generation != api.fnGeneration()) InvalidateSceneNodesLocked();
            else
            {
                // Apply's preflight never writes, even while discovering a lost
                // identity. Release/refresh restores the still-live subset.
                sceneNodesRestorePending_ = true;
                sceneNodesReady_ = false;
                ++sceneNodesRevision_;
            }
        }
        if (live != SceneNodesStatus::Ok) return live;
        for (size_t i = 0; i < sceneNodesCatalog_.size(); ++i)
        {
            auto& entry = sceneNodesCatalog_[i];
            const bool desired = targets[i] != 0;
            const bool set = api.fnSetActive(entry.identity.object, desired);
            bool actual{};
            const bool read = api.fnTryGetActiveSelf(entry.identity.object, actual);
            if (read) { entry.active = actual; SceneNodePublishActive(entry.identity, actual); }
            if (!set || !read || actual != desired)
            { sceneNodesRestorePending_ = true; return SceneNodesStatus::Failed; }
        }
        return SceneNodesStatus::Ok;
    }

    SceneNodesStatus ReleaseSceneNodes(uint64_t lease)
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        if (!lease || lease != sceneNodesLease_) return SceneNodesStatus::StaleCatalog;
        return RestoreSceneNodesLeaseLocked();
    }
