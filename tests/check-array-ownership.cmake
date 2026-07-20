if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)

if(NOT ir MATCHES "%absolute[.]array[.]int32[.]1 = type \\{ ptr, ptr, i64 \\}")
    message(FATAL_ERROR "array descriptor does not carry a separate owner pointer")
endif()

string(REGEX MATCHALL "call void @free\\(ptr [^)]+\\)" releases "${ir}")
list(LENGTH releases release_count)
if(release_count LESS 10)
    message(FATAL_ERROR "expected owning array cleanup paths, found ${release_count}")
endif()

string(REGEX MATCH "define %absolute[.]array[.]int32[.]1 @aliasedCopy\\([^}]+\\}" aliased_copy "${ir}")
if(NOT aliased_copy)
    message(FATAL_ERROR "aliasedCopy IR body was not found")
endif()
if(aliased_copy MATCHES "call void @free")
    message(FATAL_ERROR "aliasedCopy frees storage instead of transferring its root owner")
endif()

if(NOT ir MATCHES "%first[.]result = call i32 @first\\([^\r\n]+[\r\n]+[ ]*call void @free")
    message(FATAL_ERROR "temporary array argument is not released after the call")
endif()
