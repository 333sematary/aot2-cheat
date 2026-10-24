#include "feature_controller.hpp"

#include <array>
#include <cstring>
#include <utility>

namespace {
const std::array<FeatureController::PatchSpec,4> kDirectPatches{{
    {L"Infinite Gas",L"Infinite Gas",1,{0xF3,0x0F,0x11,0x91,0x50,0x02,0x00,0x00}},
    {L"Infinite Blades",L"Infinite Blades",0,{0xF3,0x0F,0x11,0x80,0x30,0x02,0x00,0x00}},
    {L"Infinite Ammo",L"Infinite Ammo",0,{0x66,0x41,0x89,0x88,0xE4,0x01,0x00,0x00}},
    {L"Battle Alert Signal Infinite Duration",L"Battle Alert Signal Infinite Duration",0,{0xF3,0x0F,0x11,0x81,0x54,0x01,0x00,0x00}},
}};

void logPatchError(const std::wstring& feature,const std::wstring& error) {
    std::wstring line=L"[Launcher feature] "+feature+L": "+error+L"\r\n";
    OutputDebugStringW(line.c_str());
}

void append32(std::vector<uint8_t>& bytes,uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value>>8));
    bytes.push_back(static_cast<uint8_t>(value>>16));
    bytes.push_back(static_cast<uint8_t>(value>>24));
}

struct ReinforcementSpec { const wchar_t* name; size_t moduleOffset; uintptr_t fieldOffset; std::vector<uint8_t> original; };
const ReinforcementSpec kReinforcementSpecs[]={
    {L"Blades Reinforcement",0x1198A6,0x09,{0x0F,0xB6,0x4C,0x08,0x09}},
    {L"Scabbards / Canisters Reinforcement",0x1197D9,0x18C9,{0x0F,0xB6,0x8C,0x08,0xC9,0x18,0x00,0x00}},
    {L"Omni-Directional Mobility Gears Reinforcement",0x119709,0x3189,{0x0F,0xB6,0x8C,0x08,0x89,0x31,0x00,0x00}},
    {L"Gun Barrel Reinforcement",0x116446,0x09,{0x0F,0xB6,0x4C,0x08,0x09}},
    {L"Ammo Belt / Canister Reinforcement",0x116309,0x18C9,{0x0F,0xB6,0x8C,0x08,0xC9,0x18,0x00,0x00}},
    {L"Anti-Personnel ODM Reinforcement",0x1161C9,0x3189,{0x0F,0xB6,0x8C,0x08,0x89,0x31,0x00,0x00}}
};

void appendJump(std::vector<uint8_t>& bytes,uintptr_t cave,uintptr_t destination) {
    bytes.push_back(0xE9);
    const uintptr_t after=cave+bytes.size()+4;
    const int64_t displacement=static_cast<int64_t>(destination)-static_cast<int64_t>(after);
    append32(bytes,static_cast<uint32_t>(static_cast<int32_t>(displacement)));
}

void appendWriteByte(std::vector<uint8_t>& bytes,uintptr_t offset,uint8_t value) {
    if (offset<=0x7F) bytes.insert(bytes.end(),{0xC6,0x44,0x08,static_cast<uint8_t>(offset),value});
    else {
        bytes.insert(bytes.end(),{0xC6,0x84,0x08}); append32(bytes,static_cast<uint32_t>(offset)); bytes.push_back(value);
    }
}

void appendStoreDword(std::vector<uint8_t>& code,const std::vector<uint8_t>& prefix,
                      uint32_t displacement,uint32_t value) {
    code.insert(code.end(),prefix.begin(),prefix.end()); append32(code,displacement); append32(code,value);
}

void appendStoreWordIndexed(std::vector<uint8_t>& code,uint32_t displacement,uint16_t value) {
    code.insert(code.end(),{0x66,0xC7,0x84,0xC8}); append32(code,displacement);
    code.push_back(static_cast<uint8_t>(value)); code.push_back(static_cast<uint8_t>(value>>8));
}

void appendStoreByteIndexedR9Rdi2(std::vector<uint8_t>& code,uint32_t displacement,uint8_t value) {
    code.insert(code.end(),{0x41,0xC6,0x84,0x79}); append32(code,displacement); code.push_back(value);
}

void appendRelocation(std::vector<uint8_t>& code,uint32_t marker) { append32(code,marker); }

void patchShortBranch(std::vector<uint8_t>& code,size_t displacementAt,size_t destination) {
    const auto delta=static_cast<int64_t>(destination)-static_cast<int64_t>(displacementAt+1);
    code[displacementAt]=static_cast<uint8_t>(static_cast<int8_t>(delta));
}

}

FeatureController::~FeatureController() {
    disableAll(process_.snapshot().state==GameConnectionState::Connected);
}

bool FeatureController::buildScriptHooks(const std::wstring& feature,std::vector<HookSpec>& hooks) const {
    hooks.clear();
    auto add=[&](const wchar_t* signature,std::vector<uint8_t> original,std::vector<uint8_t> body,uintptr_t offset=0,uintptr_t returnExtra=0,bool usesState=false) {
        hooks.push_back({signature,offset,std::move(original),std::move(body),returnExtra,usesState});
    };
    std::vector<uint8_t> body;
    if (feature==L"Max Money") {
        body={0xC7,0x41,0x14}; append32(body,999999999u);
        body.insert(body.end(),{0x8B,0x41,0x14,0xC3});
        add(L"Max Money",{0x8B,0x41,0x14,0xC3,0xCC},std::move(body));
    } else if (feature==L"Max City Materials") {
        appendStoreDword(body,{0x41,0xC7,0x80},0x00186278,99999999u);
        body.insert(body.end(),{0x41,0x8B,0x88,0x78,0x62,0x18,0x00});
        add(L"Max City Materials",{0x41,0x8B,0x88,0x78,0x62,0x18,0x00},std::move(body));
    } else if (feature==L"Max Wing of Freedom Badges") {
        body.insert(body.end(),{0xC6,0x81,0x08,0x80,0x00,0x00,0x57});
        body.insert(body.end(),{0x66,0xC7,0x81,0x0C,0x80,0x00,0x00,0xE7,0x03});
        body.insert(body.end(),{0x0F,0xB7,0x81,0x0C,0x80,0x00,0x00});
        add(L"Max Wing of Freedom Badges",{0x0F,0xB7,0x81,0x0C,0x80,0x00,0x00},std::move(body));
    } else if (feature==L"Infinite Combat Items") {
        appendStoreByteIndexedR9Rdi2(body,0x6EC,10);
        body.insert(body.end(),{0x41,0x0F,0xB7,0x94,0x79,0xEC,0x06,0x00,0x00});
        add(L"Infinite Combat Items",{0x41,0x0F,0xB7,0x94,0x79,0xEC,0x06,0x00,0x00},std::move(body));
    } else if (feature==L"Quick Friendship Gain") {
        body.insert(body.end(),{0x66,0xC7,0x40,0x40,0x0F,0x27});
        body.insert(body.end(),{0x0F,0xB7,0x50,0x40,0x85,0xC9});
        add(L"Quick Friendship Gain",{0x0F,0xB7,0x50,0x40,0x85,0xC9},std::move(body));
    } else if (feature==L"Quick Camaraderie Points" || feature==L"Quick Comraderie Points") {
        body.insert(body.end(),{0x6B,0xF6,0x0F,0x48,0x8B,0x7C,0x24,0x20});
        add(L"Quick Camaraderie Points",{0x48,0x8B,0x7C,0x24,0x20},std::move(body));
    } else if (feature==L"Infinite Dash & Multi-Attacks") {
        body.insert(body.end(),{0x41,0xC6,0x81,0x10,0x0A,0x00,0x00,0x05});
        add(L"Infinite Dash & Multi-Attacks",{0x66,0x41,0x89,0x89,0x10,0x0A,0x00,0x00},std::move(body));
    } else if (feature==L"All Materials") {
        body.insert(body.end(),{0x48,0x81,0xFF,0x72,0x00,0x00,0x00,0x77,0x0C,0x66,0xB8,0xE7,0x03});
        body.insert(body.end(),{0x66,0x89,0x84,0xD1,0x48,0x0A,0x01,0x00});
        body.insert(body.end(),{0x0F,0xB7,0x84,0xD1,0x48,0x0A,0x01,0x00});
        add(L"All Materials",{0x0F,0xB7,0x84,0xD1,0x48,0x0A,0x01,0x00},std::move(body));
    } else if (feature==L"All Gifts") {
        body.insert(body.end(),{0x48,0x81,0xFF,0x31,0x00,0x00,0x00,0x77,0x0C,0x66,0xB8,0x63,0x00});
        body.insert(body.end(),{0x66,0x89,0x84,0xD1,0xC8,0x52,0x01,0x00});
        body.insert(body.end(),{0x0F,0xB7,0x84,0xD1,0xC8,0x52,0x01,0x00});
        add(L"All Gifts",{0x0F,0xB7,0x84,0xD1,0xC8,0x52,0x01,0x00},std::move(body));
    } else if (feature==L"Titan Timer") {
        body.insert(body.end(),{0x41,0xC7,0x41,0x04,0x00,0x00,0xC8,0x41});
        add(L"Titan Timer",{0xF3,0x41,0x0F,0x11,0x41,0x04},std::move(body),1);
    } else if (feature==L"Partners Skills No Cooldown") {
        body.insert(body.end(),{0xC7,0x43,0x14,0x00,0x00,0x00,0x00,0xF3,0x0F,0x10,0x43,0x14});
        add(L"Partners Skills No Cooldown",{0xF3,0x0F,0x10,0x43,0x14},std::move(body));
    } else if (feature==L"Max Awakening Ability Points (+140)") {
        for (uint8_t offset=0x10;offset<=0x1A;offset=static_cast<uint8_t>(offset+2))
            body.insert(body.end(),{0xC6,0x84,0x08,offset,0x10,0x00,0x00,140});
        body.insert(body.end(),{0x0F,0xB7,0x84,0x08,0x10,0x10,0x00,0x00});
        add(L"Max Awakening Ability Points",{0x0F,0xB7,0x84,0x08,0x10,0x10,0x00,0x00},std::move(body));
    } else if (feature==L"Showdown Equipment Always Available") {
        body.insert(body.end(),{0x41,0xC6,0x80,0x26,0x07,0x00,0x00,0x09});
        body.insert(body.end(),{0x41,0x0F,0xB7,0xB8,0x26,0x07,0x00,0x00});
        add(L"Showdown Equipment Always Available",{0x41,0x0F,0xB7,0xB8,0x26,0x07,0x00,0x00},std::move(body));
    } else if (feature==L"Infinite Showdown Equipment (on rescue missions)") {
        body.insert(body.end(),{0xC7,0x42,0x04,0x00,0x00,0xC8,0x43,0xF3,0x0F,0x10,0x42,0x04});
        add(L"Infinite Showdown Equipment (on rescue missions)",{0xF3,0x0F,0x10,0x42,0x04},std::move(body),0x9B6CE1);
    } else if (feature==L"Auto End/Win Battle") {
        body.push_back(0x90); // Skip the original conditional jump by returning after it.
        add(L"Auto End/Win Battle",{0x0F,0x84,0xAE,0x05,0x00,0x00},std::move(body));
    } else if (feature==L"Auto Overall Score SS") {
        body={0x44,0x39,0xCB}; // cmp ebx,r9d from the supplied script
        add(L"Auto Overall Score SS (rank)",{0x74,0x3D,0x41,0x3B,0xD9,0x75,0x38},std::move(body));
        add(L"Auto Overall Score SS (clear)",{0x75,0x09,0xF7,0x41,0x0C,0x00,0x20,0x00,0x00},{},0,2);
    } else if (feature==L"Auto Hook Drive") {
        body.insert(body.end(),{0x83,0x3D}); appendRelocation(body,0xDEADBEEFu); body.push_back(0x00);
        body.insert(body.end(),{0x75,0x0A,0xC7,0x05}); appendRelocation(body,0xDEADBEEFu); append32(body,4);
        body.insert(body.end(),{0x48,0x83,0xC4,0x20,0x5F,0x58,0xE9}); appendRelocation(body,0xFEEDFACEu);
        add(L"Auto Hook Drive (grab)",{0x48,0x89,0x5C,0x24,0x30},std::move(body),0,0,true);

        body={0x48,0xC1,0xE8,0x16,0x83,0x3D}; appendRelocation(body,0xDEADBEEFu); body.push_back(0x00);
        body.insert(body.end(),{0x74,0x02,0x0C,0x01,0xA8,0x01});
        add(L"Auto Hook Drive (flag)",{0x48,0xC1,0xE8,0x16,0xA8,0x01},std::move(body),0,0,true);

        body={0x83,0x3D}; appendRelocation(body,0xDEADBEEFu); body.push_back(0x00);
        body.push_back(0x74); const size_t toOriginalDisp=body.size(); body.push_back(0);
        body.insert(body.end(),{0x53,0x49,0x8B,0x5F,0x08,0x44,0x38,0x5B,0x1C});
        body.push_back(0x75); const size_t toCleanup1=body.size(); body.push_back(0);
        body.insert(body.end(),{0x83,0xBC,0x88,0xFC,0x00,0x00,0x00,0x80});
        body.push_back(0x74); const size_t toCleanup2=body.size(); body.push_back(0);
        body.insert(body.end(),{0xC7,0x84,0x88,0xFC,0x00,0x00,0x00,0x80,0x01,0x00,0x01,0xFF,0x0D});
        appendRelocation(body,0xDEADBEEFu);
        const size_t cleanup=body.size(); body.push_back(0x5B);
        const size_t originalCmp=body.size();
        body.insert(body.end(),{0x44,0x38,0xAC,0x88,0xFC,0x00,0x00,0x00});
        patchShortBranch(body,toOriginalDisp,originalCmp);
        patchShortBranch(body,toCleanup1,cleanup);
        patchShortBranch(body,toCleanup2,cleanup);
        add(L"Auto Hook Drive (input)",{0x44,0x38,0xAC,0x88,0xFC,0x00,0x00,0x00},std::move(body),0,0,true);
    } else if (feature==L"Max Reinforce New Equipment (Buy)") {
        const struct BuyHook { const wchar_t* sig; std::vector<uint8_t> bytes; std::vector<uint8_t> body; uintptr_t off; } buys[]={
            {L"Max Reinforce New Equipment (01)",{0x41,0xFF,0x91,0xD0,0x00,0x00,0x00},{0x41,0xFF,0x91,0xD0,0x00,0x00,0x00,0x66,0xC7,0x42,0x09,0xC6,0x01},0},
            {L"Max Reinforce New Equipment (02)",{0x41,0xFF,0x91,0x68,0x01,0x00,0x00},{0x41,0xFF,0x92,0x68,0x01,0x00,0x00,0x66,0xC7,0x82,0xC9,0x18,0x00,0x00,0xC6,0x01},1},
            {L"Max Reinforce New Equipment (03)",{0x41,0xFF,0x91,0x00,0x02,0x00,0x00},{0x41,0xFF,0x92,0x00,0x02,0x00,0x00,0x66,0xC7,0x82,0x89,0x31,0x00,0x00,0xC6,0x01},0},
            {L"Max Reinforce New Equipment (04)",{0x41,0xFF,0x91,0xB0,0x02,0x00,0x00},{0x41,0xFF,0x91,0xB0,0x02,0x00,0x00,0x66,0xC7,0x42,0x09,0xC6,0x01},0},
            {L"Max Reinforce New Equipment (05)",{0x41,0xFF,0x91,0x48,0x03,0x00,0x00},{0x41,0xFF,0x91,0x48,0x03,0x00,0x00,0x66,0xC7,0x82,0xC9,0x18,0x00,0x00,0xC6,0x01},0},
            {L"Max Reinforce New Equipment (06)",{0x41,0xFF,0x91,0xE0,0x03,0x00,0x00},{0x41,0xFF,0x91,0xE0,0x03,0x00,0x00,0x66,0xC7,0x82,0x89,0x31,0x00,0x00,0xC6,0x01},0}
        };
        for (const auto& buy:buys) {
            if (buy.off==1) add(buy.sig,{0x41,0xFF,0x91,0x68,0x01,0x00,0x00},buy.body,1);
            else add(buy.sig,buy.bytes,buy.body,buy.off);
        }
    } else if (feature==L"Spawn Dire Elimination") {
        add(L"Spawn Dire Elimination (unlock)",{0x74,0x31,0x83,0x7B,0x3C,0x01},{0x83,0x7B,0x3C,0x01});
        add(L"Spawn Dire Elimination (skip)",{0x75,0x14,0xC6,0x43,0x0A,0xFF},{0xC6,0x43,0x0A,0xFF});
    }
    return !hooks.empty();
}

bool FeatureController::setScriptHooks(const std::wstring& feature,bool enabled,
                                        const std::vector<HookSpec>& hooks,std::wstring& error) {
    const std::wstring prefix=feature+L"::";
    if (!enabled) {
        for (auto it=injectedPatches_.begin();it!=injectedPatches_.end();) {
            if (it->first.compare(0,prefix.size(),prefix)!=0) { ++it; continue; }
            if (!it->second.patch.restore(error)) return false;
            std::wstring freeError;
            if (!process_.freeRemote(it->second.cave,freeError)) logPatchError(feature,freeError);
            it=injectedPatches_.erase(it);
        }
        const auto state=featureStateCaves_.find(feature);
        if (state!=featureStateCaves_.end()) {
            std::wstring freeError;
            if (!process_.freeRemote(state->second,freeError)) logPatchError(feature,freeError);
            featureStateCaves_.erase(state);
        }
        return true;
    }
    for (const auto& active:injectedPatches_) if (active.first.compare(0,prefix.size(),prefix)==0) return true;
    uintptr_t sharedState=0;
    for (const auto& hook:hooks) sharedState|=hook.usesSharedState?1u:0u;
    if (sharedState) {
        uintptr_t base=0;
        if (!process_.signatureAddress(hooks.front().signature,base)) {
            error=L"The table signature for "+feature+L" is not uniquely available. Disable overlapping Cheat Engine scripts and reconnect.";
            return false;
        }
        sharedState=process_.allocateCodeNear(base,4096,error);
        if (!sharedState) return false;
        if (!process_.writeRemote(sharedState,std::vector<uint8_t>(4,0),error)) {
            std::wstring ignored; process_.freeRemote(sharedState,ignored); return false;
        }
        featureStateCaves_[feature]=sharedState;
    }
    size_t installed=0;
    for (size_t i=0;i<hooks.size();++i) {
        uintptr_t target=0;
        if (!process_.signatureAddress(hooks[i].signature,target)) {
            error=L"The table signature for "+feature+L" is not uniquely available. Disable overlapping Cheat Engine scripts and reconnect.";
            break;
        }
        target+=hooks[i].offset;
        uintptr_t alternate=0;
        if (feature==L"Auto Hook Drive" && i==0) {
            if (!process_.signatureAddress(L"Auto Hook Drive (dodge)",alternate)) {
                error=L"Auto Hook Drive's dodge-branch signature is unavailable.";
                break;
            }
            alternate+=8;
        }
        if (!installCodeHook(prefix+std::to_wstring(i),target,hooks[i].original,hooks[i].body,error,
                             hooks[i].returnExtra,sharedState,alternate)) break;
        ++installed;
    }
    if (installed==hooks.size()) return true;
    for (size_t i=0;i<installed;++i) {
        auto it=injectedPatches_.find(prefix+std::to_wstring(i));
        if (it==injectedPatches_.end()) continue;
        std::wstring ignored;
        if (it->second.patch.restore(ignored)) process_.freeRemote(it->second.cave,ignored);
        injectedPatches_.erase(it);
    }
    if (sharedState) {
        std::wstring ignored; process_.freeRemote(sharedState,ignored);
        featureStateCaves_.erase(feature);
    }
    return false;
}

bool FeatureController::supports(const std::wstring& feature) const {
    if (feature==L"Player Data" || feature==L"Equip All Skills" || feature==L"Equipe All Skills") return true;
    for (const auto& spec:kReinforcementSpecs) if (feature==spec.name) return true;
    for (const auto& spec:kDirectPatches) if (feature==spec.feature) return true;
    std::vector<HookSpec> hooks;
    return buildScriptHooks(feature,hooks);
}

bool FeatureController::setEnabled(const std::wstring& feature,bool enabled,std::wstring& error) {
    if (feature==L"Player Data") return setPlayerDataHook(enabled,error);
    if (feature==L"Equip All Skills" || feature==L"Equipe All Skills") return setPlayerSkills(enabled,error);
    for (const auto& spec:kReinforcementSpecs) if (feature==spec.name) return setReinforcement(feature,enabled,error);
    std::vector<HookSpec> scriptHooks;
    if (buildScriptHooks(feature,scriptHooks)) return setScriptHooks(feature,enabled,scriptHooks,error);
    const auto active=activePatches_.find(feature);
    if (!enabled) {
        if (active==activePatches_.end()) return true;
        if (!active->second.restore(error)) { logPatchError(feature,error); return false; }
        activePatches_.erase(active);
        return true;
    }
    if (active!=activePatches_.end()) return true;
    const PatchSpec* spec=nullptr;
    for (const auto& item:kDirectPatches) if (feature==item.feature) { spec=&item; break; }
    if (!spec) { error=L"This script has not yet been ported to a native C++ feature module."; return false; }
    uintptr_t address=0;
    if (!process_.signatureAddress(spec->signature,address)) {
        const auto state=process_.snapshot();
        error=L"The "+feature+L" signature is not uniquely available in this game process ("+
            std::to_wstring(state.patternsFound)+L"/"+std::to_wstring(state.patternsRequired)+
            L" signatures matched). Check the game build against the supplied script.";
        return false;
    }
    address+=spec->offset;
    std::vector<uint8_t> replacement(spec->original.size(),0x90);
    MemoryPatch patch;
    if (!patch.install(process_,address,spec->original,replacement,error)) {
        logPatchError(feature,error); return false;
    }
    activePatches_.emplace(feature,std::move(patch));
    return true;
}

bool FeatureController::installCodeHook(const std::wstring& feature,uintptr_t target,
    const std::vector<uint8_t>& original,const std::vector<uint8_t>& codeBody,std::wstring& error,
    uintptr_t returnExtra,uintptr_t sharedState,uintptr_t alternateTarget) {
    if (original.size()<5) { error=L"The verified code site is too short for a relative hook."; return false; }
    const uintptr_t cave=process_.allocateCodeNear(target,128,error);
    if (!cave) return false;
    std::vector<uint8_t> code=codeBody;
    const uint8_t stateMarker[]={0xEF,0xBE,0xAD,0xDE};
    const uint8_t branchMarker[]={0xCE,0xFA,0xED,0xFE};
    for (size_t i=0;i+4<=code.size();++i) {
        uintptr_t destination=0;
        if (std::equal(stateMarker,stateMarker+4,code.begin()+static_cast<std::ptrdiff_t>(i))) destination=sharedState;
        else if (std::equal(branchMarker,branchMarker+4,code.begin()+static_cast<std::ptrdiff_t>(i))) destination=alternateTarget;
        if (!destination) continue;
        const uintptr_t after=cave+i+4;
        const int64_t displacement=static_cast<int64_t>(destination)-static_cast<int64_t>(after);
        if (displacement<INT32_MIN || displacement>INT32_MAX) {
            error=L"A shared code reference is outside the x64 RIP-relative range.";
            std::wstring ignored; process_.freeRemote(cave,ignored); return false;
        }
        const uint32_t relative=static_cast<uint32_t>(static_cast<int32_t>(displacement));
        for (size_t b=0;b<4;++b) code[i+b]=static_cast<uint8_t>(relative>>(b*8));
        i+=3;
    }
    appendJump(code,cave,target+original.size()+returnExtra);
    if (!process_.writeRemote(cave,code,error)) { std::wstring ignored; process_.freeRemote(cave,ignored); return false; }
    const int64_t displacement=static_cast<int64_t>(cave)-static_cast<int64_t>(target+5);
    if (displacement<INT32_MIN || displacement>INT32_MAX) {
        error=L"Allocated code block is outside the x64 relative-jump range.";
        std::wstring ignored; process_.freeRemote(cave,ignored); return false;
    }
    std::vector<uint8_t> replacement={0xE9};
    append32(replacement,static_cast<uint32_t>(static_cast<int32_t>(displacement)));
    replacement.resize(original.size(),0x90);
    InjectedPatch active; active.cave=cave;
    if (!active.patch.install(process_,target,original,replacement,error)) {
        std::wstring ignored; process_.freeRemote(cave,ignored); return false;
    }
    injectedPatches_.emplace(feature,std::move(active));
    return true;
}

bool FeatureController::setReinforcement(const std::wstring& feature,bool enabled,std::wstring& error) {
    auto active=injectedPatches_.find(feature);
    if (!enabled) {
        if (active==injectedPatches_.end()) return true;
        if (!active->second.patch.restore(error)) return false;
        std::wstring freeError;
        if (!process_.freeRemote(active->second.cave,freeError)) logPatchError(feature,freeError);
        injectedPatches_.erase(active);
        return true;
    }
    if (active!=injectedPatches_.end()) return true;
    const ReinforcementSpec* spec=nullptr;
    for (const auto& item:kReinforcementSpecs) if (feature==item.name) { spec=&item; break; }
    if (!spec) { error=L"Unknown reinforcement feature."; return false; }
    const uintptr_t target=process_.moduleAddress(spec->moduleOffset);
    if (!target) { error=L"The game's verified module address is unavailable."; return false; }
    std::vector<uint8_t> body;
    appendWriteByte(body,spec->fieldOffset,198);
    appendWriteByte(body,spec->fieldOffset+1,1);
    body.insert(body.end(),spec->original.begin(),spec->original.end());
    return installCodeHook(feature,target,spec->original,body,error);
}

bool FeatureController::setPlayerDataHook(bool enabled,std::wstring& error) {
    if (enabled && playerDataHook_.active()) return true;
    if (!enabled && !playerDataHook_.active()) return true;
    if (!enabled) {
        if (!playerDataHook_.restore(error)) return false;
        std::wstring freeError;
        if (!process_.freeRemote(playerDataCave_,freeError)) logPatchError(L"Player Data hook cleanup",freeError);
        playerDataCave_=playerPointerSlot_=playerSkillSlot_=0;
        return true;
    }

    uintptr_t target=0;
    if (!process_.signatureAddress(L"Player Data",target)) {
        // The CT's original-code listing identifies this injection point at
        // AOT2_AS.exe+1172F0. If the site starts with a relative JMP, another
        // script has already replaced the original AOB and stacking a hook is
        // unsafe. The RVA is diagnostic only; normal installation uses the AOB.
        const uintptr_t knownSite=process_.moduleAddress(0x1172F0);
        std::vector<uint8_t> siteBytes(5);
        std::wstring readError;
        if (knownSite && process_.readMemory(knownSite,siteBytes,readError) && siteBytes[0]==0xE9) {
            error=L"Player Data is already hooked by another script (the game's original code is replaced). Disable Player Data in Cheat Engine, then click the connection status to retry. Stacking another hook is unsafe.";
        } else {
            const auto state=process_.snapshot();
            error=L"The Player Data signature from the supplied table was not found uniquely ("+
                std::to_wstring(state.patternsFound)+L"/"+std::to_wstring(state.patternsRequired)+
                L" signatures matched). Check that the game build matches the table.";
        }
        return false;
    }
    const uintptr_t cave=process_.allocateCodeNear(target,128,error);
    if (!cave) return false;

    // Native equivalent of the supplied Player Data script: cache RCX, optionally
    // set the eight skill masks, then preserve the original byte getter and RET.
    std::vector<uint8_t> code;
    code.reserve(89);
    code.insert(code.end(),{0x48,0x89,0x0D}); append32(code,73); // mov [rip+pointerSlot],rcx
    code.insert(code.end(),{0x80,0x3D}); append32(code,74); code.push_back(0x01); // cmp [rip+skillSlot],1
    code.insert(code.end(),{0x75,0x38}); // skip eight writes when OFF
    const struct { uint8_t offset; uint32_t value; } masks[]={
        {0x18,0xFFFFFFFFu},{0x1C,0xFFFFFFFFu},{0x20,0x003FFFFFu},{0x24,0xFFFFFFF0u},
        {0x28,0xFFFFFFFFu},{0x2C,0x07FF8FFFu},{0x30,0xF0000700u},{0x34,0x00073FFFu}
    };
    for (const auto& mask:masks) {
        code.insert(code.end(),{0xC7,0x41,mask.offset}); append32(code,mask.value);
    }
    code.insert(code.end(),{0x0F,0xB6,0x41,0x08,0xC3});
    if (code.size()!=77) { error=L"Internal Player Data hook layout mismatch."; process_.freeRemote(cave,error); return false; }
    code.resize(80,0xCC);
    code.resize(89,0);
    if (!process_.writeRemote(cave,code,error)) { std::wstring ignored; process_.freeRemote(cave,ignored); return false; }

    const int64_t rel=static_cast<int64_t>(cave)-static_cast<int64_t>(target+5);
    if (rel<INT32_MIN || rel>INT32_MAX) {
        error=L"Allocated Player Data hook is outside the x64 relative-jump range.";
        std::wstring ignored; process_.freeRemote(cave,ignored); return false;
    }
    std::vector<uint8_t> original={0x0F,0xB6,0x41,0x08,0xC3};
    std::vector<uint8_t> patch={0xE9}; append32(patch,static_cast<uint32_t>(static_cast<int32_t>(rel)));
    if (!playerDataHook_.install(process_,target,original,patch,error)) {
        std::wstring ignored; process_.freeRemote(cave,ignored); return false;
    }
    playerDataCave_=cave; playerPointerSlot_=cave+80; playerSkillSlot_=cave+88;
    return true;
}

bool FeatureController::setPlayerSkills(bool enabled,std::wstring& error) {
    if (!playerDataHook_.active() || !playerSkillSlot_) {
        error=L"Enable Player Data first so the game can expose the skill flag.";
        return false;
    }
    return process_.writeRemote(playerSkillSlot_,{static_cast<uint8_t>(enabled?1:0)},error);
}

bool FeatureController::currentPlayerPointer(uintptr_t& pointer,std::wstring& error) const {
    pointer=0;
    if (!playerDataHook_.active() || !playerPointerSlot_) {
        error=L"Enable Player Data first."; return false;
    }
    std::vector<uint8_t> bytes(sizeof(pointer));
    if (!process_.readMemory(playerPointerSlot_,bytes,error)) return false;
    std::memcpy(&pointer,bytes.data(),sizeof(pointer));
    if (!pointer) { error=L"Waiting for the game to update the Player Data record. Open the character/status menu and retry."; return false; }
    return true;
}

bool FeatureController::readPlayerField(uintptr_t offset,size_t size,std::vector<uint8_t>& value,std::wstring& error) const {
    uintptr_t pointer=0;
    if (!currentPlayerPointer(pointer,error)) return false;
    if (pointer>UINTPTR_MAX-offset) { error=L"Player Data field address overflow."; return false; }
    value.assign(size,0);
    return process_.readMemory(pointer+offset,value,error);
}

bool FeatureController::writePlayerField(uintptr_t offset,const std::vector<uint8_t>& value,std::wstring& error) {
    uintptr_t pointer=0;
    if (!currentPlayerPointer(pointer,error)) return false;
    if (value.empty() || pointer>UINTPTR_MAX-offset) { error=L"Invalid Player Data field address or size."; return false; }
    std::vector<uint8_t> expected(value.size());
    if (!process_.readMemory(pointer+offset,expected,error)) return false;
    return process_.compareAndWrite(pointer+offset,expected,value,error);
}

void FeatureController::disableAll(bool processStillConnected) {
    if (playerDataHook_.active()) {
        if (processStillConnected) {
            std::wstring error;
            if (setPlayerDataHook(false,error)) {
                playerSkillSlot_=playerPointerSlot_=playerDataCave_=0;
            } else logPatchError(L"Player Data",error);
        } else {
            playerDataHook_.abandon();
            playerSkillSlot_=playerPointerSlot_=playerDataCave_=0;
        }
    }
    for (auto it=injectedPatches_.begin();it!=injectedPatches_.end();) {
        std::wstring error;
        if (processStillConnected && it->second.patch.restore(error)) {
            std::wstring freeError;
            if (!process_.freeRemote(it->second.cave,freeError)) logPatchError(it->first,freeError);
            it=injectedPatches_.erase(it);
        } else {
            if (!error.empty()) logPatchError(it->first,error);
            if (!processStillConnected) it->second.patch.abandon();
            it=injectedPatches_.erase(it);
        }
    }
    if (processStillConnected) {
        for (const auto& state:featureStateCaves_) {
            std::wstring error;
            if (!process_.freeRemote(state.second,error)) logPatchError(state.first,error);
        }
    }
    featureStateCaves_.clear();
    for (auto it=activePatches_.begin();it!=activePatches_.end();) {
        std::wstring error;
        if (processStillConnected && it->second.restore(error)) {
            it=activePatches_.erase(it);
        } else {
            if (!error.empty()) logPatchError(it->first,error);
            if (!processStillConnected) it->second.abandon();
            it=activePatches_.erase(it);
        }
    }
}
