macro(install_pdb TARGET)
    if(WIN32)
        install(FILES
            "$<TARGET_FILE_DIR:${TARGET}>/$<TARGET_FILE_PREFIX:${TARGET}>$<TARGET_FILE_BASE_NAME:${TARGET}>.pdb"
            TYPE BIN
            OPTIONAL
        )
    endif()
endmacro()
