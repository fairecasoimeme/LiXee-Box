#pragma once
#include <esp_heap_caps.h>
#include <string>
#include <vector>
#include <unordered_map>

template <class T>
struct PsramAllocator {
    using value_type = T;

    PsramAllocator() noexcept {}
    template <class U> PsramAllocator(const PsramAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        if (!p) {
            log_e("Échec d'allocation PSRAM!");
            abort();
        }
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t /*n*/) {
        heap_caps_free(p);
    }
};

template <class T, class U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) {return true;}

template <class T, class U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) {return false;}

// ===== PSRAM-backed type aliases =====

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

template<typename T>
using PsVector = std::vector<T, PsramAllocator<T>>;

// Hash pour PsString (nécessaire pour unordered_map)
struct PsStringHash {
    size_t operator()(const PsString& s) const {
        size_t hash = 2166136261u;
        for (auto c : s) {
            hash ^= (size_t)c;
            hash *= 16777619u;
        }
        return hash;
    }
};

// Cache de devices avec clés en PSRAM
template<typename V>
using PsUnorderedMap = std::unordered_map<
    PsString, V,
    PsStringHash,
    std::equal_to<PsString>,
    PsramAllocator<std::pair<const PsString, V>>
>;
