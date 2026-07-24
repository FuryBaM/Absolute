if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Export C bool LLVM IR file is missing: ${INPUT}")
endif()

file(READ "${INPUT}" IR)

if(WIN32)
    set(EXPORT_PREFIX "define dllexport")
else()
    set(EXPORT_PREFIX "define")
endif()

# C ABI bool must be i8 (_Bool), not Absolute's internal i1.
set(SIGNATURE "${EXPORT_PREFIX} i8 @absolute_is_positive(i32 %value)")
string(FIND "${IR}" "${SIGNATURE}" POSITION)
if(POSITION EQUAL -1)
    message(FATAL_ERROR "Expected C ABI bool export as i8: ${SIGNATURE}")
endif()

if(IR MATCHES "define[^\n]*i1 @absolute_is_positive")
    message(FATAL_ERROR "C ABI bool export incorrectly uses i1")
endif()
