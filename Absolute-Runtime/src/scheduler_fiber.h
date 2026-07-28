#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__ANDROID__)
#include <libucontext/libucontext.h>
#else
#include <ucontext.h>
#endif

namespace Absolute::RuntimeDetail {
    class FiberContext {
    public:
        using Entry = void (*)(void*);

    private:
#if defined(_WIN32)
        void* handle = nullptr;
        bool threadRoot = false;

        struct Start {
            Entry entry = nullptr;
            void* argument = nullptr;
        } start;

        static VOID CALLBACK Trampoline(void* opaque) {
            auto* self = static_cast<FiberContext*>(opaque);
            self->start.entry(self->start.argument);
            std::abort();
        }
#else
#if defined(__ANDROID__)
        using NativeContext = libucontext_ucontext_t;
#else
        using NativeContext = ucontext_t;
#endif
        NativeContext context{};
        std::unique_ptr<std::byte[]> stack;

        struct Start {
            Entry entry = nullptr;
            void* argument = nullptr;
        } start;

        static void Trampoline(std::uint32_t low, std::uint32_t high) {
            const std::uintptr_t encoded =
                static_cast<std::uintptr_t>(low) |
                (static_cast<std::uintptr_t>(high) << 32U);
            auto* self = reinterpret_cast<FiberContext*>(encoded);
            self->start.entry(self->start.argument);
            std::abort();
        }
#endif

#if !defined(_WIN32)
        static int GetContext(NativeContext* value) {
#if defined(__ANDROID__)
            return libucontext_getcontext(value);
#else
            return getcontext(value);
#endif
        }

        static void MakeContext(
            NativeContext* value, void (*entry)(), int count,
            std::uint32_t low, std::uint32_t high) {
#if defined(__ANDROID__)
            libucontext_makecontext(value, entry, count, low, high);
#else
            makecontext(value, entry, count, low, high);
#endif
        }

        static int SwapContext(NativeContext* from, NativeContext* to) {
#if defined(__ANDROID__)
            return libucontext_swapcontext(from, to);
#else
            return swapcontext(from, to);
#endif
        }
#endif

    public:
        FiberContext() = default;
        FiberContext(const FiberContext&) = delete;
        FiberContext& operator=(const FiberContext&) = delete;

        void InitializeThreadRoot() {
#if defined(_WIN32)
            handle = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
            if (!handle && GetLastError() == ERROR_ALREADY_FIBER)
                handle = GetCurrentFiber();
            if (!handle) std::abort();
            threadRoot = true;
#else
            if (GetContext(&context) != 0) std::abort();
#endif
        }

        void Create(Entry entry, void* argument) {
            start = {entry, argument};
#if defined(_WIN32)
            constexpr std::size_t stackCommit = 64U * 1024U;
            constexpr std::size_t stackReserve = 1024U * 1024U;
            handle = CreateFiberEx(
                stackCommit, stackReserve, FIBER_FLAG_FLOAT_SWITCH,
                &FiberContext::Trampoline, this);
            if (!handle) std::abort();
#else
            constexpr std::size_t stackSize = 1024U * 1024U;
            stack = std::make_unique<std::byte[]>(stackSize);
            if (GetContext(&context) != 0) std::abort();
            context.uc_stack.ss_sp = stack.get();
            context.uc_stack.ss_size = stackSize;
            context.uc_link = nullptr;
            const std::uintptr_t encoded = reinterpret_cast<std::uintptr_t>(this);
            MakeContext(
                &context, reinterpret_cast<void (*)()>(&FiberContext::Trampoline), 2,
                static_cast<std::uint32_t>(encoded),
                static_cast<std::uint32_t>(encoded >> 32U));
#endif
        }

        void SwitchTo(FiberContext& target) {
#if defined(_WIN32)
            SwitchToFiber(target.handle);
#else
            if (SwapContext(&context, &target.context) != 0) std::abort();
#endif
        }

        void Destroy() {
#if defined(_WIN32)
            if (handle && !threadRoot) DeleteFiber(handle);
            handle = nullptr;
#else
            stack.reset();
#endif
        }

        void DestroyThreadRoot() {
#if defined(_WIN32)
            if (threadRoot) {
                ConvertFiberToThread();
                threadRoot = false;
                handle = nullptr;
            }
#endif
        }

        bool IsCreated() const {
#if defined(_WIN32)
            return handle != nullptr;
#else
            return stack != nullptr;
#endif
        }
    };
}
