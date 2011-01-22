IF (NOT DROPLET_FOUND)
    # Based on the current install rules of the droplet library,
    # the includes have a "droplet" prefix and no prefix is used for
    # the said library.

    # First, we need to find the path to droplet includes
    # the "-1.0" part is the version part,
    FIND_PATH(DROPLET_INCLUDE_DIR
              NAMES droplet.h
              PATH_SUFFIXES droplet droplet-1.0
    )

    # If it was found, we should be able to find the library too...
    FIND_LIBRARY(DROPLET_LIBRARY droplet)

    IF (DROPLET_INCLUDE_DIR AND DROPLET_LIBRARY)
        SET (DROPLET_FOUND TRUE)
        MESSAGE(STATUS "Droplet include path found : ${DROPLET_INCLUDE_DIR}")
        MESSAGE(STATUS "Droplet library found : ${DROPLET_LIBRARY}")
    ENDIF (DROPLET_INCLUDE_DIR AND DROPLET_LIBRARY)


ENDIF (NOT DROPLET_FOUND)
