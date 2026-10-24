#pragma once

#include "process_manager.hpp"

#include <map>
#include <string>
#include <vector>

class FeatureController {
public:
    struct PatchSpec {
        const wchar_t* feature;
        const wchar_t* signature;
        uintptr_t offset;
        std::vector<uint8_t> original;
    };
    explicit FeatureController(ProcessManager& process) : process_(process) {}
    ~FeatureController();
    FeatureController(const FeatureController&) = delete;
    FeatureController& operator=(const FeatureController&) = delete;

    bool supports(const std::wstring& feature) const;
    bool setEnabled(const std::wstring& feature, bool enabled, std::wstring& error);
    bool readPlayerField(uintptr_t offset, size_t size, std::vector<uint8_t>& value, std::wstring& error) const;
    bool writePlayerField(uintptr_t offset, const std::vector<uint8_t>& value, std::wstring& error);
    void disableAll(bool processStillConnected);

private:
    struct InjectedPatch { MemoryPatch patch; uintptr_t cave = 0; };
    struct HookSpec {
        const wchar_t* signature;
        uintptr_t offset;
        std::vector<uint8_t> original;
        std::vector<uint8_t> body;
        uintptr_t returnExtra = 0;
        bool usesSharedState = false;
    };
    ProcessManager& process_;
    std::map<std::wstring,MemoryPatch> activePatches_;
    std::map<std::wstring,InjectedPatch> injectedPatches_;
    std::map<std::wstring,uintptr_t> featureStateCaves_;
    MemoryPatch playerDataHook_;
    uintptr_t playerDataCave_ = 0;
    uintptr_t playerPointerSlot_ = 0;
    uintptr_t playerSkillSlot_ = 0;

    bool setPlayerDataHook(bool enabled, std::wstring& error);
    bool setPlayerSkills(bool enabled, std::wstring& error);
    bool setReinforcement(const std::wstring& feature, bool enabled, std::wstring& error);
    bool installCodeHook(const std::wstring& feature, uintptr_t target,
                         const std::vector<uint8_t>& original,
                         const std::vector<uint8_t>& codeBody, std::wstring& error,
                         uintptr_t returnExtra = 0, uintptr_t sharedState = 0,
                         uintptr_t alternateTarget = 0);
    bool setScriptHooks(const std::wstring& feature, bool enabled,
                        const std::vector<HookSpec>& hooks, std::wstring& error);
    bool buildScriptHooks(const std::wstring& feature, std::vector<HookSpec>& hooks) const;
    bool currentPlayerPointer(uintptr_t& pointer, std::wstring& error) const;
};
