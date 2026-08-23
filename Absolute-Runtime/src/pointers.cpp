#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <atomic>
#include <mutex>

namespace {
    // Read on every dereference of a managed pointer, which is the hottest
    // path the runtime has: the fields a reader touches are atomic so that
    // reading one takes no lock at all.
    struct Slot {
        std::atomic<void*> pointer{nullptr};
        std::atomic<std::uint32_t> generation{1};
        std::atomic<std::uint64_t> type{0};
        // Only ever read and written while slotsMutex is held.
        void (*deleter)(void*) = nullptr;
    };
}

namespace {
    // Slots live in chunks that are allocated once and never moved, because a
    // reader holds a slot's address without a lock. The table used to be a
    // std::vector, whose growth relocates every slot, which is why reading one
    // had to take the mutex: an insertion sort through a managed pointer spent
    // 22 nanoseconds per element access against 0.6 for the same array in a
    // local, and 55 seconds against 1.5.
    constexpr std::uint32_t SlotsPerChunk = 4096;
    constexpr std::uint32_t MaxChunks = 65536;

    std::mutex slotsMutex;
    std::once_flag leakCheckRegistration;
    std::atomic<Slot*> slotChunks[MaxChunks];
    std::atomic<std::uint32_t> slotCount{0};
    std::vector<std::uint32_t> freeSlots;

    void DefaultDeleter(void* pointer) {
        std::free(pointer);
    }

    std::uint64_t MakeHandle(std::uint32_t id, std::uint32_t generation) {
        return (static_cast<std::uint64_t>(generation) << 32U) | id;
    }

    std::uint32_t HandleId(std::uint64_t handle) {
        return static_cast<std::uint32_t>(handle);
    }

    std::uint32_t HandleGeneration(std::uint64_t handle) {
        return static_cast<std::uint32_t>(handle >> 32U);
    }

    Slot* SlotAt(std::uint32_t id) {
        const std::uint32_t chunk = id / SlotsPerChunk;
        if (chunk >= MaxChunks) return nullptr;
        Slot* base = slotChunks[chunk].load(std::memory_order_acquire);
        return base ? base + (id % SlotsPerChunk) : nullptr;
    }

    // The locked lookup, for everything that changes a slot.
    Slot* Find(std::uint64_t handle) {
        if (handle == 0) return nullptr;
        const std::uint32_t id = HandleId(handle);
        if (id >= slotCount.load(std::memory_order_relaxed)) return nullptr;
        Slot* slot = SlotAt(id);
        if (!slot) return nullptr;
        return slot->generation.load(std::memory_order_relaxed) ==
            HandleGeneration(handle) ? slot : nullptr;
    }

    std::uint64_t RegisterAllocation(
        void* allocation, void (*deleter)(void*)) {
        std::lock_guard<std::mutex> lock(slotsMutex);
        std::uint32_t id;
        if (!freeSlots.empty()) {
            id = freeSlots.back();
            freeSlots.pop_back();
        }
        else {
            id = slotCount.load(std::memory_order_relaxed);
            if (id / SlotsPerChunk >= MaxChunks) {
                std::cerr << "Absolute runtime error: managed pointer slot table is full\n";
                std::abort();
            }
            const std::uint32_t chunk = id / SlotsPerChunk;
            if (!slotChunks[chunk].load(std::memory_order_relaxed)) {
                Slot* base = new (std::nothrow) Slot[SlotsPerChunk];
                if (!base) {
                    std::cerr << "Absolute runtime error: managed pointer slot table allocation failed\n";
                    std::abort();
                }
                slotChunks[chunk].store(base, std::memory_order_release);
            }
            // Published after the chunk, so a reader that sees the count sees
            // storage to read it from.
            slotCount.store(id + 1, std::memory_order_release);
        }
        Slot* slot = SlotAt(id);
        slot->type.store(0, std::memory_order_relaxed);
        slot->deleter = deleter ? deleter : DefaultDeleter;
        // Last, and with a release: the generation already identifies this
        // handle, so the pointer is what makes the slot live.
        slot->pointer.store(allocation, std::memory_order_release);
        return MakeHandle(id, slot->generation.load(std::memory_order_relaxed));
    }
}

extern "C" void absolute_managed_check_leaks() {
    std::lock_guard<std::mutex> lock(slotsMutex);
    std::uint32_t leakCount = 0;
    const std::uint32_t count = slotCount.load(std::memory_order_relaxed);
    for (std::uint32_t i = 0; i < count; ++i) {
        Slot* slot = SlotAt(i);
        if (slot && slot->pointer.load(std::memory_order_relaxed) != nullptr) {
            std::cerr << "Absolute runtime error: memory leak detected for handle "
                      << MakeHandle(i, slot->generation.load(std::memory_order_relaxed)) << "\n";
            leakCount++;
        }
    }
    if (leakCount > 0) {
        std::cerr << "Absolute runtime error: " << leakCount << " managed pointer(s) leaked!\n";
        std::abort();
    }
}

extern "C" std::uint64_t absolute_managed_create(std::uint64_t size) {
    std::call_once(leakCheckRegistration, [] {
        std::atexit(absolute_managed_check_leaks);
    });

    void* allocation = std::calloc(1, static_cast<std::size_t>(size == 0 ? 1 : size));
    if (!allocation) {
        std::cerr << "Absolute runtime error: managed allocation failed\n";
        std::abort();
    }

    return RegisterAllocation(allocation, DefaultDeleter);
}

extern "C" void* absolute_managed_get(std::uint64_t handle) {
    // No lock. The generation is read before and after the pointer: a destroy
    // in between bumps it, so a pointer belonging to a slot that has already
    // been handed to someone else is discarded rather than returned. What this
    // does not do -- keep the object alive past the call -- the locked version
    // did not do either, since it released the mutex before returning.
    if (handle == 0) return nullptr;
    const std::uint32_t id = HandleId(handle);
    if (id >= slotCount.load(std::memory_order_acquire)) return nullptr;
    Slot* slot = SlotAt(id);
    if (!slot) return nullptr;
    const std::uint32_t expected = HandleGeneration(handle);
    if (slot->generation.load(std::memory_order_acquire) != expected) return nullptr;
    void* pointer = slot->pointer.load(std::memory_order_acquire);
    if (slot->generation.load(std::memory_order_acquire) != expected) return nullptr;
    return pointer;
}

extern "C" bool absolute_managed_valid(std::uint64_t handle) {
    return absolute_managed_get(handle) != nullptr;
}

extern "C" void absolute_managed_set_type(std::uint64_t handle, std::uint64_t type) {
    std::lock_guard<std::mutex> lock(slotsMutex);
    if (Slot* slot = Find(handle)) slot->type.store(type, std::memory_order_release);
}

extern "C" std::uint64_t absolute_managed_type(std::uint64_t handle) {
    // Read on every runtime type test and every virtual call through an
    // interface, so it takes the same lock-free path the pointer does.
    if (handle == 0) return 0;
    const std::uint32_t id = HandleId(handle);
    if (id >= slotCount.load(std::memory_order_acquire)) return 0;
    Slot* slot = SlotAt(id);
    if (!slot) return 0;
    const std::uint32_t expected = HandleGeneration(handle);
    if (slot->generation.load(std::memory_order_acquire) != expected) return 0;
    const std::uint64_t type = slot->type.load(std::memory_order_acquire);
    if (slot->generation.load(std::memory_order_acquire) != expected) return 0;
    return type;
}

extern "C" void* absolute_managed_require(std::uint64_t handle) {
    if (void* pointer = absolute_managed_get(handle)) return pointer;
    std::cerr << "Absolute runtime error: null or expired managed pointer\n";
    std::abort();
}

extern "C" void absolute_managed_destroy(std::uint64_t handle) {
    void* pointer = nullptr;
    void (*deleter)(void*) = nullptr;
    {
        std::lock_guard<std::mutex> lock(slotsMutex);
        Slot* slot = Find(handle);
        if (!slot) return;
        pointer = slot->pointer.load(std::memory_order_relaxed);
        if (!pointer) return;
        const std::uint32_t id = HandleId(handle);
        deleter = slot->deleter ? slot->deleter : DefaultDeleter;
        slot->pointer.store(nullptr, std::memory_order_release);
        slot->type.store(0, std::memory_order_relaxed);
        slot->deleter = nullptr;
        std::uint32_t next = slot->generation.load(std::memory_order_relaxed) + 1;
        if (next == 0) next = 1;
        // Published last: a reader that has already read this generation reads
        // it again after the pointer and sees the change.
        slot->generation.store(next, std::memory_order_release);
        freeSlots.push_back(id);
    }
    deleter(pointer);
}

extern "C" std::uint64_t absolute_managed_transfer(std::uint64_t handle) {
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot* slot = Find(handle);
    if (!slot || !slot->pointer.load(std::memory_order_relaxed)) {
        std::cerr << "Absolute runtime error: cannot transfer a null or expired managed owner\n";
        std::abort();
    }
    const std::uint32_t id = HandleId(handle);
    std::uint32_t next = slot->generation.load(std::memory_order_relaxed) + 1;
    if (next == 0) next = 1;
    slot->generation.store(next, std::memory_order_release);
    return MakeHandle(id, next);
}

namespace {
    struct CapsuleImpl {
        std::uint64_t handle = 0;
        std::atomic<bool> transferred{false};
        void (*deleter_fn)(void*) = nullptr;
    };
}

extern "C" void* absolute_capsule_create_typed(
    std::uint64_t handle, void (*deleter)(void*));

extern "C" void* absolute_capsule_create(std::uint64_t handle) {
    return absolute_capsule_create_typed(handle, nullptr);
}

extern "C" void* absolute_capsule_create_typed(
    std::uint64_t handle, void (*deleter)(void*)) {
    CapsuleImpl* capsule = new CapsuleImpl;
    capsule->handle = absolute_managed_transfer(handle);
    capsule->deleter_fn = deleter;
    return capsule;
}

extern "C" std::uint64_t absolute_capsule_unwrap(void* capsulePtr) {
    if (!capsulePtr) {
        std::cerr << "Absolute runtime error: cannot unwrap a null transfer capsule\n";
        std::abort();
    }
    CapsuleImpl* capsule = static_cast<CapsuleImpl*>(capsulePtr);
    if (capsule->transferred.exchange(true)) {
        std::cerr << "Absolute runtime error: transfer capsule already unwrapped or transferred\n";
        std::abort();
    }
    std::uint64_t handle = capsule->handle;
    delete capsule;
    return handle;
}

extern "C" void absolute_capsule_destroy(void* capsulePtr) {
    if (!capsulePtr) return;
    CapsuleImpl* capsule = static_cast<CapsuleImpl*>(capsulePtr);
    if (!capsule->transferred.load() && capsule->handle != 0) {
        if (capsule->deleter_fn) {
            void* rawObj = absolute_managed_get(capsule->handle);
            if (rawObj) capsule->deleter_fn(rawObj);
        }
        absolute_managed_destroy(capsule->handle);
    }
    delete capsule;
}

extern "C" std::uint64_t absolute_managed_adopt_raw(void* ptr, void (*deleter)(void*)) {
    if (!ptr) return 0;
    std::call_once(leakCheckRegistration, [] {
        std::atexit(absolute_managed_check_leaks);
    });
    return RegisterAllocation(ptr, deleter ? deleter : DefaultDeleter);
}
