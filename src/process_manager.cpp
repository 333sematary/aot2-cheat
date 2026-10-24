#include "process_manager.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <sstream>
#include <utility>

namespace {
struct UniqueHandle {
    HANDLE value = nullptr;
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : value(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) reset(std::exchange(other.value, nullptr));
        return *this;
    }
    HANDLE get() const { return value; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
    HANDLE release() { return std::exchange(value, nullptr); }
    void reset(HANDLE handle = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = handle;
    }
};

struct PatternSpec { const wchar_t* name; const char* pattern; };
constexpr PatternSpec kPatterns[] = {
    {L"Max Money", "8B 41 14 C3 CC CC CC CC CC CC CC CC CC CC CC CC 8B 41 10 44"},
    {L"Max City Materials", "41 8B 88 78 62 18 00"},
    {L"Max Wing of Freedom Badges", "0F B7 81 0C 80 00 00 C3"},
    {L"Infinite Gas", "D1 F3 0F 11 91 50 02 00 00"},
    {L"Infinite Blades", "F3 0F 11 80 30 02 00 00 76"},
    {L"Infinite Ammo", "66 41 89 88 E4 01 00 00"},
    {L"Infinite Combat Items", "41 0F B7 94 79 EC 06 00 00"},
    {L"Quick Friendship Gain", "0F B7 50 40 85 C9 74 3C"},
    {L"Quick Camaraderie Points", "48 8B 7C 24 20 44 8B 84"},
    {L"Infinite Dash & Multi-Attacks", "66 41 89 89 10 0A 00 00"},
    {L"Auto Hook Drive (grab)", "48 89 5C 24 30 49 8B 98"},
    {L"Auto Hook Drive (dodge)", "85 C0 0F 84 * * * * 8B 83 * * 00 00 D0 E8 A8 01"},
    {L"Auto Hook Drive (flag)", "48 C1 E8 16 A8 01 74 04"},
    {L"Auto Hook Drive (input)", "44 38 AC 88 FC 00 00 00"},
    {L"Auto Overall Score SS (rank)", "74 3D 41 3B D9 75 38"},
    {L"Auto Overall Score SS (clear)", "75 09 F7 41 0C 00 20 00 00"},
    {L"All Materials", "0F B7 84 D1 48 0A 01 00"},
    {L"All Gifts", "0F B7 84 D1 C8 52 01 00"},
    // Keep the exact AOB from the supplied CT. A shorter padding sequence can
    // incorrectly stop matching when another trainer has already hooked it.
    {L"Player Data", "0F B6 41 08 C3 CC CC CC CC CC CC CC CC CC CC CC 80"},
    {L"Titan Timer", "C2 F3 41 0F 11 41 04"},
    {L"Partners Skills No Cooldown", "F3 0F 10 43 14 F3 0F 5C C7"},
    {L"Max Reinforce New Equipment (01)", "41 FF 91 D0 00 00 00"},
    {L"Max Reinforce New Equipment (02)", "CB 41 FF 91 68 01 00 00"},
    {L"Max Reinforce New Equipment (03)", "41 FF 91 00 02 00 00 48"},
    {L"Max Reinforce New Equipment (04)", "41 FF 91 B0 02 00 00"},
    {L"Max Reinforce New Equipment (05)", "41 FF 91 48 03 00 00 48"},
    {L"Max Reinforce New Equipment (06)", "41 FF 91 E0 03 00 00"},
    {L"Max Awakening Ability Points", "0F B7 84 08 10 10 00 00"},
    {L"Showdown Equipment Always Available", "41 0F B7 B8 26 07 00 00"},
    {L"Battle Alert Signal Infinite Duration", "F3 0F 11 81 54 01 00 00"},
    {L"Spawn Dire Elimination (unlock)", "74 31 83 7B 3C 01"},
    {L"Spawn Dire Elimination (skip)", "75 14 C6 43 0A FF"},
    {L"Auto End/Win Battle", "0F 84 AE 05 00 00 F6"},
};

void logMessage(const std::wstring& message) {
    std::wstring line=L"[Launcher process] "+message+L"\r\n";
    OutputDebugStringW(line.c_str());
}

std::wstring win32Error(const wchar_t* operation, DWORD code=GetLastError()) {
    wchar_t* message=nullptr;
    const DWORD count=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|
        FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message),0,nullptr);
    std::wstring result=operation;
    result+=L" failed ("+std::to_wstring(code)+L")";
    if (count && message) {
        result+=L": "; result.append(message,count);
        while (!result.empty() && (result.back()==L'\r'||result.back()==L'\n')) result.pop_back();
        LocalFree(message);
    }
    return result;
}

DWORD findGameProcess(std::wstring& error) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if (!snapshot) { error=win32Error(L"CreateToolhelp32Snapshot(processes)"); return 0; }
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if (!Process32FirstW(snapshot.get(),&entry)) {
        error=win32Error(L"Process32FirstW"); return 0;
    }
    do {
        if (_wcsicmp(entry.szExeFile,L"AOT2_EU.exe")==0) return entry.th32ProcessID;
    } while (Process32NextW(snapshot.get(),&entry));
    return 0;
}

struct ModuleInfo { uintptr_t base=0; size_t size=0; };
bool findGameModule(DWORD pid, ModuleInfo& module, std::wstring& error) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if (!snapshot) { error=win32Error(L"CreateToolhelp32Snapshot(modules)"); return false; }
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if (!Module32FirstW(snapshot.get(),&entry)) {
        error=win32Error(L"Module32FirstW"); return false;
    }
    do {
        if (_wcsicmp(entry.szModule,L"AOT2_EU.exe")==0) {
            module.base=reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            module.size=static_cast<size_t>(entry.modBaseSize);
            return module.size!=0;
        }
    } while (Module32NextW(snapshot.get(),&entry));
    error=L"Module AOT2_EU.exe was not available in the target process.";
    return false;
}

bool readableProtection(DWORD protect) {
    if ((protect&PAGE_GUARD)!=0 || (protect&PAGE_NOACCESS)!=0) return false;
    switch (protect&0xFF) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return true;
    default: return false;
    }
}

std::vector<int> parsePattern(const char* text) {
    std::vector<int> result;
    std::istringstream input(text);
    std::string token;
    while (input>>token) {
        if (token=="?" || token=="??" || token=="*") result.push_back(-1);
        else {
            try { result.push_back(static_cast<int>(std::stoul(token,nullptr,16)&0xFF)); }
            catch (...) { return {}; }
        }
    }
    return result;
}

struct PatternMatch { size_t count=0; uintptr_t address=0; };
PatternMatch scanImage(const std::vector<uint8_t>& image,const std::vector<uint8_t>& readable,
                       uintptr_t base,const std::vector<int>& pattern) {
    PatternMatch result{};
    if (pattern.empty() || pattern.size()>image.size()) return result;
    size_t anchor=pattern.size();
    for (size_t i=0;i<pattern.size();++i) if (pattern[i]>=0) { anchor=i; break; }
    if (anchor==pattern.size()) return result;
    auto candidate=image.begin()+static_cast<std::ptrdiff_t>(anchor);
    const auto last=image.begin()+static_cast<std::ptrdiff_t>(image.size()-pattern.size()+anchor);
    const auto searchEnd=last+1;
    while (candidate<searchEnd) {
        candidate=std::find(candidate,searchEnd,static_cast<uint8_t>(pattern[anchor]));
        if (candidate==searchEnd) break;
        const size_t start=static_cast<size_t>(candidate-image.begin())-anchor;
        bool match=true;
        for (size_t i=0;i<pattern.size();++i) {
            if (!readable[start+i] || (pattern[i]>=0 && image[start+i]!=static_cast<uint8_t>(pattern[i]))) {
                match=false; break;
            }
        }
        if (match) { ++result.count; result.address=base+start; if (result.count>1) return result; }
        ++candidate;
    }
    return result;
}

bool scanRequiredPatterns(HANDLE process,const ModuleInfo& module,size_t& found,std::wstring& error,
                          const std::atomic<bool>& stopping,std::unordered_map<std::wstring,uintptr_t>& addresses) {
    found=0;
    if (!module.size || module.size>1024ull*1024ull*1024ull) {
        error=L"The game module size is invalid or exceeds the scanner safety limit.";
        return false;
    }
    std::vector<uint8_t> image(module.size,0);
    std::vector<uint8_t> readable(module.size,0);
    SYSTEM_INFO systemInfo{}; GetSystemInfo(&systemInfo);
    const uintptr_t pageSize=(std::max<DWORD>)(systemInfo.dwPageSize,4096);
    const uintptr_t begin=module.base, end=module.base+module.size;
    uintptr_t address=begin;
    while (address<end && !stopping.load()) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQueryEx(process,reinterpret_cast<LPCVOID>(address),&region,sizeof(region))) {
            address+=pageSize; continue;
        }
        const uintptr_t regionBegin=(std::max)(address,reinterpret_cast<uintptr_t>(region.BaseAddress));
        const uintptr_t regionEnd=(std::min)(end,reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize);
        if (regionEnd<=address) { address+=pageSize; continue; }
        if (region.State==MEM_COMMIT && readableProtection(region.Protect)) {
            uintptr_t cursor=regionBegin;
            while (cursor<regionEnd && !stopping.load()) {
                const SIZE_T amount=static_cast<SIZE_T>((std::min<uintptr_t>)(256*1024,regionEnd-cursor));
                SIZE_T transferred=0;
                if (ReadProcessMemory(process,reinterpret_cast<LPCVOID>(cursor),image.data()+(cursor-begin),amount,&transferred) && transferred) {
                    std::fill_n(readable.data()+(cursor-begin),transferred,static_cast<uint8_t>(1));
                }
                cursor+=amount;
            }
        }
        address=regionEnd;
    }
    if (stopping.load()) { error=L"Scan cancelled."; return false; }

    std::vector<std::wstring> unavailable;
    for (const auto& spec:kPatterns) {
        if (stopping.load()) { error=L"Scan cancelled."; return false; }
        const auto parsed=parsePattern(spec.pattern);
        const auto match=scanImage(image,readable,module.base,parsed);
        if (match.count==1) { ++found; addresses.emplace(spec.name,match.address); }
        else {
            std::wstring item=spec.name;
            item+=match.count==0?L" (not found)":L" (ambiguous)";
            unavailable.push_back(std::move(item));
        }
    }
    if (found!=std::size(kPatterns)) {
        error=std::to_wstring(found)+L"/"+std::to_wstring(std::size(kPatterns))+L" signatures matched uniquely. Unavailable: ";
        constexpr size_t maxNames=5;
        for (size_t i=0;i<(std::min)(unavailable.size(),maxNames);++i) {
            if (i) error+=L", ";
            error+=unavailable[i];
        }
        if (unavailable.size()>maxNames)
            error+=L" and "+std::to_wstring(unavailable.size()-maxNames)+L" more";
    } else error.clear();
    return true;
}

bool processIsRunning(HANDLE process) {
    if (!process || process==INVALID_HANDLE_VALUE) return false;
    if (WaitForSingleObject(process,0)!=WAIT_TIMEOUT) return false;
    DWORD exitCode=0;
    return GetExitCodeProcess(process,&exitCode) && exitCode==STILL_ACTIVE;
}

bool validateRange(HANDLE process,uintptr_t address,size_t size,MEMORY_BASIC_INFORMATION& region,std::wstring& error) {
    if (!address || !size || address>UINTPTR_MAX-size) { error=L"Invalid memory address or size."; return false; }
    if (!VirtualQueryEx(process,reinterpret_cast<LPCVOID>(address),&region,sizeof(region))) {
        error=win32Error(L"VirtualQueryEx"); return false;
    }
    const uintptr_t regionBegin=reinterpret_cast<uintptr_t>(region.BaseAddress);
    const uintptr_t regionEnd=regionBegin+region.RegionSize;
    if (region.State!=MEM_COMMIT || !readableProtection(region.Protect) || address<regionBegin || address+size>regionEnd) {
        error=L"The requested bytes are outside a committed readable memory region."; return false;
    }
    return true;
}
} // namespace

ProcessManager::~ProcessManager() { stop(); }

void ProcessManager::start() {
    if (worker_.joinable()) return;
    stopping_.store(false);
    worker_=std::thread(&ProcessManager::run,this);
}

void ProcessManager::stop() {
    stopping_.store(true);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    process_.reset();
    moduleBase_=0; moduleSize_=0;
    patternAddresses_.clear();
}

void ProcessManager::requestRetry() {
    { std::lock_guard<std::mutex> lock(mutex_); retryRequested_=true; }
    wake_.notify_all();
}

GameConnectionSnapshot ProcessManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool ProcessManager::signatureAddress(const std::wstring& name,uintptr_t& address) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state!=GameConnectionState::Connected || !processIsRunning(process_.get())) return false;
    const auto found=patternAddresses_.find(name);
    if (found==patternAddresses_.end()) return false;
    address=found->second;
    return true;
}

uintptr_t ProcessManager::moduleAddress(size_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state!=GameConnectionState::Connected || !moduleBase_ || offset>=moduleSize_)
        return 0;
    return moduleBase_+offset;
}

uintptr_t ProcessManager::allocateCodeNear(uintptr_t target,size_t size,std::wstring& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state!=GameConnectionState::Connected || !processIsRunning(process_.get())) {
        error=L"No validated game process is connected."; return 0;
    }
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    const uintptr_t granularity=(std::max<DWORD>)(info.dwAllocationGranularity,65536);
    const uintptr_t center=target&~(granularity-1);
    constexpr uintptr_t range=0x70000000;
    for (uintptr_t distance=granularity;distance<range;distance+=granularity) {
        const uintptr_t candidates[2]={center+distance,center>distance?center-distance:0};
        for (uintptr_t candidate:candidates) {
            if (!candidate || candidate>UINTPTR_MAX-size ||
                (candidate>target?candidate-target:target-candidate)>0x7FFF0000) continue;
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQueryEx(process_.get(),reinterpret_cast<LPCVOID>(candidate),&region,sizeof(region)) ||
                region.State!=MEM_FREE) continue;
            const uintptr_t regionBegin=reinterpret_cast<uintptr_t>(region.BaseAddress);
            const uintptr_t regionEnd=regionBegin+region.RegionSize;
            const uintptr_t allocation=(std::max)(candidate,regionBegin);
            if (allocation>regionEnd || size>regionEnd-allocation) continue;
            void* memory=VirtualAllocEx(process_.get(),reinterpret_cast<LPVOID>(allocation),size,
                MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
            if (memory) return reinterpret_cast<uintptr_t>(memory);
        }
    }
    error=L"Could not allocate a nearby executable code block for the game hook.";
    return 0;
}

bool ProcessManager::writeRemote(uintptr_t address,const std::vector<uint8_t>& bytes,std::wstring& error) {
    if (!address || bytes.empty()) { error=L"Invalid remote write address or empty data."; return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state!=GameConnectionState::Connected || !processIsRunning(process_.get())) {
        error=L"No validated game process is connected."; return false;
    }
    SIZE_T transferred=0;
    if (!WriteProcessMemory(process_.get(),reinterpret_cast<LPVOID>(address),bytes.data(),bytes.size(),&transferred) ||
        transferred!=bytes.size()) {
        error=win32Error(L"WriteProcessMemory(remote code)"); return false;
    }
    FlushInstructionCache(process_.get(),reinterpret_cast<LPCVOID>(address),bytes.size());
    return true;
}

bool ProcessManager::freeRemote(uintptr_t address,std::wstring& error) {
    if (!address) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!processIsRunning(process_.get())) { error=L"The game process is no longer available."; return false; }
    if (!VirtualFreeEx(process_.get(),reinterpret_cast<LPVOID>(address),0,MEM_RELEASE)) {
        error=win32Error(L"VirtualFreeEx"); return false;
    }
    return true;
}

void ProcessManager::publish(const GameConnectionSnapshot& value) {
    bool changed=false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed=snapshot_.state!=value.state || snapshot_.processId!=value.processId ||
            snapshot_.patternsFound!=value.patternsFound || snapshot_.detail!=value.detail;
        snapshot_=value;
    }
    if (changed) logMessage(value.detail.empty()?L"Connection state changed.":value.detail);
}

bool ProcessManager::waitFor(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock,duration,[this]{ return stopping_.load() || retryRequested_; });
    const bool retry=retryRequested_;
    retryRequested_=false;
    return retry;
}

bool ProcessManager::activeHandleValid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return processIsRunning(process_.get());
}

void ProcessManager::run() {
    constexpr DWORD access=PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|SYNCHRONIZE;
    while (!stopping_.load()) {
        std::wstring error;
        const DWORD pid=findGameProcess(error);
        if (!pid) {
            if (error.empty()) error=L"AOT2_EU.exe was not found.";
            publish({GameConnectionState::GameNotFound,0,0,std::size(kPatterns),error});
            waitFor(std::chrono::milliseconds(1000));
            continue;
        }
        publish({GameConnectionState::Connecting,pid,0,std::size(kPatterns),L"Game process detected; checking module and signatures."});
        UniqueHandle candidate(OpenProcess(access,FALSE,pid));
        if (!candidate) {
            error=win32Error(L"OpenProcess");
            publish({GameConnectionState::Connecting,pid,0,std::size(kPatterns),error});
            waitFor(std::chrono::milliseconds(1200)); continue;
        }
        if (!processIsRunning(candidate.get())) {
            publish({GameConnectionState::GameNotFound,0,0,std::size(kPatterns),L"Game exited during connection."});
            waitFor(std::chrono::milliseconds(500)); continue;
        }
        ModuleInfo module{};
        if (!findGameModule(pid,module,error)) {
            publish({GameConnectionState::Connecting,pid,0,std::size(kPatterns),error});
            waitFor(std::chrono::milliseconds(1200)); continue;
        }
        size_t found=0;
        std::unordered_map<std::wstring,uintptr_t> addresses;
        if (!scanRequiredPatterns(candidate.get(),module,found,error,stopping_,addresses)) {
            if (!stopping_.load()) publish({GameConnectionState::Connecting,pid,found,std::size(kPatterns),error});
            waitFor(std::chrono::milliseconds(1800)); continue;
        }
        if (!found) {
            publish({GameConnectionState::Connecting,pid,0,std::size(kPatterns),
                L"The game module was read, but none of the supplied script signatures matched. Check the game version and supplied scripts."});
            waitFor(std::chrono::milliseconds(1800)); continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            process_.reset(candidate.release());
            moduleBase_=module.base; moduleSize_=module.size;
            snapshot_={GameConnectionState::Connected,pid,found,std::size(kPatterns),error.empty()
                ?L"Connected; every supplied script signature is unique and readable."
                :L"Connected with partial script support: "+error};
            patternAddresses_=std::move(addresses);
        }
        logMessage(L"Connected to AOT2_EU.exe; "+std::to_wstring(found)+L"/"+
            std::to_wstring(std::size(kPatterns))+L" supplied AOB signatures are unique and readable.");
        bool retry=false;
        while (!stopping_.load()) {
            retry=waitFor(std::chrono::milliseconds(400));
            if (retry) break;
            if (!activeHandleValid()) break;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            process_.reset();
            moduleBase_=0; moduleSize_=0;
            patternAddresses_.clear();
        }
        if (stopping_.load()) break;
        if (!retry) publish({GameConnectionState::GameNotFound,0,0,std::size(kPatterns),L"Game process exited; connection closed."});
    }
}

bool ProcessManager::readMemory(uintptr_t address,std::vector<uint8_t>& bytes,std::wstring& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state!=GameConnectionState::Connected || !processIsRunning(process_.get())) {
        error=L"No validated game process is connected."; return false;
    }
    MEMORY_BASIC_INFORMATION region{};
    if (!validateRange(process_.get(),address,bytes.size(),region,error)) return false;
    SIZE_T transferred=0;
    if (!ReadProcessMemory(process_.get(),reinterpret_cast<LPCVOID>(address),bytes.data(),bytes.size(),&transferred) || transferred!=bytes.size()) {
        error=win32Error(L"ReadProcessMemory"); return false;
    }
    return true;
}

bool ProcessManager::compareAndWrite(uintptr_t address,const std::vector<uint8_t>& expected,
    const std::vector<uint8_t>& replacement,std::wstring& error) {
    if (expected.empty() || expected.size()!=replacement.size()) { error=L"Expected and replacement byte lengths must match."; return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state!=GameConnectionState::Connected || !processIsRunning(process_.get())) {
        error=L"No validated game process is connected."; return false;
    }
    MEMORY_BASIC_INFORMATION region{};
    if (!validateRange(process_.get(),address,expected.size(),region,error)) return false;
    std::vector<uint8_t> current(expected.size()); SIZE_T transferred=0;
    if (!ReadProcessMemory(process_.get(),reinterpret_cast<LPCVOID>(address),current.data(),current.size(),&transferred) || transferred!=current.size()) {
        error=win32Error(L"ReadProcessMemory(before patch)"); return false;
    }
    if (current!=expected) { error=L"Patch site bytes changed; refusing to modify the address."; return false; }
    DWORD oldProtection=0;
    if (!VirtualProtectEx(process_.get(),reinterpret_cast<LPVOID>(address),replacement.size(),PAGE_EXECUTE_READWRITE,&oldProtection)) {
        error=win32Error(L"VirtualProtectEx"); return false;
    }
    transferred=0;
    const BOOL wrote=WriteProcessMemory(process_.get(),reinterpret_cast<LPVOID>(address),replacement.data(),replacement.size(),&transferred);
    const DWORD writeError=wrote?ERROR_SUCCESS:GetLastError();
    const BOOL protectionRestored=VirtualProtectEx(process_.get(),reinterpret_cast<LPVOID>(address),replacement.size(),oldProtection,&oldProtection);
    FlushInstructionCache(process_.get(),reinterpret_cast<LPCVOID>(address),replacement.size());
    if (!wrote || transferred!=replacement.size()) { error=win32Error(L"WriteProcessMemory",writeError); return false; }
    if (!protectionRestored) error=win32Error(L"VirtualProtectEx(restore protection)");
    return true;
}

MemoryPatch::~MemoryPatch() { std::wstring ignored; restore(ignored); }

MemoryPatch::MemoryPatch(MemoryPatch&& other) noexcept
    : process_(std::exchange(other.process_,nullptr)),address_(other.address_),
      original_(std::move(other.original_)),replacement_(std::move(other.replacement_)) {}

MemoryPatch& MemoryPatch::operator=(MemoryPatch&& other) noexcept {
    if (this!=&other) {
        std::wstring ignored; restore(ignored);
        process_=std::exchange(other.process_,nullptr); address_=other.address_;
        original_=std::move(other.original_); replacement_=std::move(other.replacement_);
    }
    return *this;
}

bool MemoryPatch::install(ProcessManager& process,uintptr_t address,const std::vector<uint8_t>& expected,
    const std::vector<uint8_t>& replacement,std::wstring& error) {
    if (active()) { error=L"This patch object already owns a patch."; return false; }
    if (!process.compareAndWrite(address,expected,replacement,error)) return false;
    process_=&process; address_=address; original_=expected; replacement_=replacement;
    return true;
}

bool MemoryPatch::restore(std::wstring& error) {
    if (!process_) return true;
    if (!process_->compareAndWrite(address_,replacement_,original_,error)) return false;
    process_=nullptr; address_=0; original_.clear(); replacement_.clear();
    return true;
}

void MemoryPatch::abandon() noexcept {
    process_=nullptr; address_=0; original_.clear(); replacement_.clear();
}
