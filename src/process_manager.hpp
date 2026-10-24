#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class GameConnectionState { GameNotFound, Connecting, Connected };

struct GameConnectionSnapshot {
    GameConnectionState state = GameConnectionState::GameNotFound;
    DWORD processId = 0;
    size_t patternsFound = 0;
    size_t patternsRequired = 0;
    std::wstring detail;
};

class ScopedProcessHandle {
public:
    ScopedProcessHandle() = default;
    explicit ScopedProcessHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedProcessHandle() { reset(); }
    ScopedProcessHandle(const ScopedProcessHandle&) = delete;
    ScopedProcessHandle& operator=(const ScopedProcessHandle&) = delete;
    ScopedProcessHandle(ScopedProcessHandle&& other) noexcept : handle_(other.release()) {}
    ScopedProcessHandle& operator=(ScopedProcessHandle&& other) noexcept {
        if (this!=&other) reset(other.release());
        return *this;
    }
    HANDLE get() const { return handle_; }
    HANDLE release() { HANDLE result=handle_; handle_=nullptr; return result; }
    void reset(HANDLE handle=nullptr) {
        if (handle_ && handle_!=INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_=handle;
    }
private:
    HANDLE handle_ = nullptr;
};

class ProcessManager {
public:
    ProcessManager() = default;
    ~ProcessManager();
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    void start();
    void stop();
    void requestRetry();
    GameConnectionSnapshot snapshot() const;
    bool signatureAddress(const std::wstring& name, uintptr_t& address) const;

    bool readMemory(uintptr_t address, std::vector<uint8_t>& bytes, std::wstring& error) const;
    bool compareAndWrite(uintptr_t address, const std::vector<uint8_t>& expected,
                         const std::vector<uint8_t>& replacement, std::wstring& error);
    uintptr_t allocateCodeNear(uintptr_t target, size_t size, std::wstring& error);
    bool writeRemote(uintptr_t address, const std::vector<uint8_t>& bytes, std::wstring& error);
    bool freeRemote(uintptr_t address, std::wstring& error);
    uintptr_t moduleAddress(size_t offset) const;

private:
    void run();
    void publish(const GameConnectionSnapshot& snapshot);
    bool waitFor(std::chrono::milliseconds duration);
    bool activeHandleValid() const;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool retryRequested_ = false;
    ScopedProcessHandle process_;
    uintptr_t moduleBase_ = 0;
    size_t moduleSize_ = 0;
    GameConnectionSnapshot snapshot_{};
    std::unordered_map<std::wstring,uintptr_t> patternAddresses_;
};

// Restores bytes only while the patched bytes are still present at the site.
class MemoryPatch {
public:
    MemoryPatch() = default;
    ~MemoryPatch();
    MemoryPatch(const MemoryPatch&) = delete;
    MemoryPatch& operator=(const MemoryPatch&) = delete;
    MemoryPatch(MemoryPatch&& other) noexcept;
    MemoryPatch& operator=(MemoryPatch&& other) noexcept;

    bool install(ProcessManager& process, uintptr_t address,
                 const std::vector<uint8_t>& expected,
                 const std::vector<uint8_t>& replacement, std::wstring& error);
    bool restore(std::wstring& error);
    void abandon() noexcept;
    bool active() const { return process_ != nullptr; }

private:
    ProcessManager* process_ = nullptr;
    uintptr_t address_ = 0;
    std::vector<uint8_t> original_;
    std::vector<uint8_t> replacement_;
};
