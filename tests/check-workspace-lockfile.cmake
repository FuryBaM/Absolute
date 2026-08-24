if(NOT DEFINED LOCKFILE)
    message(FATAL_ERROR "LOCKFILE is required")
endif()
if(NOT EXISTS "${LOCKFILE}")
    message(FATAL_ERROR "Workspace lockfile was not written: ${LOCKFILE}")
endif()
file(READ "${LOCKFILE}" CONTENT)
foreach(PKG IN ITEMS
    "absolute.catalog.core"
    "absolute.catalog.left"
    "absolute.catalog.right")
    if(NOT CONTENT MATCHES "${PKG}")
        message(FATAL_ERROR "Lockfile does not record ${PKG}:\n${CONTENT}")
    endif()
endforeach()
if(NOT CONTENT MATCHES "\"absolute.catalog.core\": \"0.1.0\"")
    message(FATAL_ERROR "Lockfile does not pin core at 0.1.0 once:\n${CONTENT}")
endif()
message(STATUS "workspace-lockfile=ok")
