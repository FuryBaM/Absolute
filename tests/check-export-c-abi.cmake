if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Export C ABI LLVM IR file is missing: ${INPUT}")
endif()

file(READ "${INPUT}" IR)

if(WIN32)
    set(EXPORT_PREFIX "define dllexport")
else()
    set(EXPORT_PREFIX "define")
endif()

foreach(SIGNATURE IN ITEMS
        "${EXPORT_PREFIX} i32 @absolute_add(i32 %left, i32 %right)"
        "${EXPORT_PREFIX} void @absolute_increment(ptr %value)")
    string(FIND "${IR}" "${SIGNATURE}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Expected exported C ABI signature was not emitted: ${SIGNATURE}")
    endif()
endforeach()

if(IR MATCHES "absolute_add\\$")
    message(FATAL_ERROR "Exported C ABI symbol was mangled")
endif()

string(FIND "${IR}" "call i32 @absolute_add(i32 20, i32 21)" CALL_POSITION)
if(CALL_POSITION EQUAL -1)
    message(FATAL_ERROR "Absolute call site does not use the exported C ABI symbol")
endif()
