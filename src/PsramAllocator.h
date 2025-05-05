#pragma once
#include <esp_heap_caps.h>

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
