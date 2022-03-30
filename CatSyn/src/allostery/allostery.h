size_t round_size(size_t size) noexcept;
void round_copy(void* __restrict dst, const void* __restrict src, size_t size) noexcept;
void* re_alloc(void* ptr, size_t new_size) noexcept;

#ifdef _WIN32
#define ALLOSTERY_EXPORT __declspec(dllexport)
#define ALLOSTERY_IMPORT __declspec(dllimport)
#else
#define ALLOSTERY_EXPORT __attribute__((visibility("default")))
#define ALLOSTERY_IMPORT
#endif

#ifdef ALLOSTERY_IMPL
#define ALLOSTERY_API extern "C" ALLOSTERY_EXPORT
#else
#define ALLOSTERY_API extern "C" ALLOSTERY_IMPORT
#endif

ALLOSTERY_API void* alloc(size_t size);
ALLOSTERY_API void dealloc(void* ptr);
ALLOSTERY_API size_t alloc_size(void* ptr);
