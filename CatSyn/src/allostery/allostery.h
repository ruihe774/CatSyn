size_t round_size(size_t size) noexcept;
void round_copy(void* __restrict dst, const void* __restrict src, size_t size) noexcept;
void* re_alloc(void* ptr, size_t new_size) noexcept;

#ifdef ALLOSTERY_IMPL
#define ALLOSTERY_API extern "C" __declspec(dllexport)
#else
#define ALLOSTERY_API extern "C" __declspec(dllimport)
#endif

ALLOSTERY_API void* alloc(size_t size);
ALLOSTERY_API void dealloc(void* ptr);
ALLOSTERY_API size_t alloc_size(void* ptr);
