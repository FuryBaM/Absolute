#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <atomic>

namespace {
    struct Slot {
        void* pointer = nullptr;
        std::uint32_t generation = 1;
        std::uint64_t type = 0;
    };
}

extern "C" {
    Slot* absolute_managed_slots_data = nullptr;
    std::uint32_t absolute_managed_slots_size = 0;
}

namespace {

    std::vector<Slot> slots;
    std::vector<std::uint32_t> freeSlots;

    std::uint64_t MakeHandle(std::uint32_t id, std::uint32_t generation) {
        return (static_cast<std::uint64_t>(generation) << 32U) | id;
    }

    std::uint32_t HandleId(std::uint64_t handle) {
        return static_cast<std::uint32_t>(handle);
    }

    std::uint32_t HandleGeneration(std::uint64_t handle) {
        return static_cast<std::uint32_t>(handle >> 32U);
    }

    Slot* Find(std::uint64_t handle) {
        if (handle == 0) return nullptr;
        const std::uint32_t id = HandleId(handle);
        if (id >= slots.size()) return nullptr;
        Slot& slot = slots[id];
        return slot.generation == HandleGeneration(handle) ? &slot : nullptr;
    }
}

extern "C" void absolute_managed_check_leaks() {
    std::uint32_t leakCount = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].pointer != nullptr) {
            std::cerr << "Absolute runtime error: memory leak detected for handle " 
                      << MakeHandle(static_cast<std::uint32_t>(i), slots[i].generation) << "\n";
            leakCount++;
        }
    }
    if (leakCount > 0) {
        std::cerr << "Absolute runtime error: " << leakCount << " managed pointer(s) leaked!\n";
        std::abort();
    }
}

extern "C" std::uint64_t absolute_managed_create(std::uint64_t size) {
    static std::atomic<bool> initialized = false;
    if (!initialized.exchange(true)) {
        std::atexit(absolute_managed_check_leaks);
    }

    void* allocation = std::calloc(1, static_cast<std::size_t>(size == 0 ? 1 : size));
    if (!allocation) {
        std::cerr << "Absolute runtime error: managed allocation failed\n";
        std::abort();
    }

    std::uint32_t id;
    if (!freeSlots.empty()) {
        id = freeSlots.back();
        freeSlots.pop_back();
        slots[id].pointer = allocation;
        slots[id].type = 0;
    }
    else {
        if (slots.size() >= std::numeric_limits<std::uint32_t>::max()) {
            std::free(allocation);
            std::cerr << "Absolute runtime error: managed pointer slot table is full\n";
            std::abort();
        }
        id = static_cast<std::uint32_t>(slots.size());
        slots.push_back({allocation, 1});
        absolute_managed_slots_data = slots.data();
        absolute_managed_slots_size = static_cast<std::uint32_t>(slots.size());
    }
    return MakeHandle(id, slots[id].generation);
}

extern "C" void* absolute_managed_get(std::uint64_t handle) {
    Slot* slot = Find(handle);
    return slot ? slot->pointer : nullptr;
}

extern "C" bool absolute_managed_valid(std::uint64_t handle) {
    return absolute_managed_get(handle) != nullptr;
}

extern "C" void absolute_managed_set_type(std::uint64_t handle, std::uint64_t type) {
    if (Slot* slot = Find(handle)) slot->type = type;
}

extern "C" std::uint64_t absolute_managed_type(std::uint64_t handle) {
    if (Slot* slot = Find(handle)) return slot->type;
    return 0;
}

extern "C" void* absolute_managed_require(std::uint64_t handle) {
    if (void* pointer = absolute_managed_get(handle)) return pointer;
    std::cerr << "Absolute runtime error: null or expired managed pointer\n";
    std::abort();
}

extern "C" void absolute_managed_destroy(std::uint64_t handle) {
    Slot* slot = Find(handle);
    if (!slot || !slot->pointer) return;
    const std::uint32_t id = HandleId(handle);
    std::free(slot->pointer);
    slot->pointer = nullptr;
    slot->type = 0;
    ++slot->generation;
    if (slot->generation == 0) ++slot->generation;
    freeSlots.push_back(id);
}
