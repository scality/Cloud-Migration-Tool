MACRO (COMPILE_DOT DotFile)
    GET_FILENAME_COMPONENT(DotFileNoExt ${DotFile} NAME_WE)
    GET_FILENAME_COMPONENT(DotFileName ${DotFile} NAME)
    GET_FILENAME_COMPONENT(DotFilePath ${DotFile} PATH)
    ADD_CUSTOM_TARGET("${DotFileNoExt}.png")
    ADD_CUSTOM_COMMAND(SOURCE ${DotFileNoExt}.png
        COMMAND sh
        ARGS -c '${DOT_EXECUTABLE} -Tpng -o ${CLOUDMIG_BINARY_DIR}/doc/${DotFileNoExt}.png ${DotFile}'
        TARGET ${DotFileNoExt}.png
    )
    # This allows reuniting each DotFile compilation under the
    # "dots" target.
    IF (NOT DOTS_COMMAND)
        SET (DOTS_COMMAND TRUE)
        # Creates a make rule allowing to make all dot files
        ADD_CUSTOM_TARGET(dots)
        # Creates the doc/ directory in the build tree
        FILE(MAKE_DIRECTORY ${CLOUDMIG_BINARY_DIR}/doc/)
    ENDIF (NOT DOTS_COMMAND)
    # makes the general dots rule depend on the newly generated file
    ADD_DEPENDENCIES(dots "${DotFileNoExt}.png")
ENDMACRO (COMPILE_DOT DotFile)
