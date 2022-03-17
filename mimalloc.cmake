set(MI_OVERRIDE OFF CACHE BOOL "Override the standard malloc interface (e.g. define entry points for malloc() etc)")
set(MI_USE_CXX ON CACHE BOOL "Use the C++ compiler to compile the library (instead of the C compiler)" FORCE)

add_subdirectory(mimalloc EXCLUDE_FROM_ALL)

install(TARGETS mimalloc)
if(WIN32)
    install(FILES
        mimalloc/bin/mimalloc-redirect.dll
        TYPE BIN
    )
    install(FILES
        mimalloc/bin/mimalloc-redirect.lib
        TYPE LIB
    )
endif()
