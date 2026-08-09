if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)

if(NOT ir MATCHES
    "%move[.]value = load i64, ptr %a[^}]*call void @llvm[.]memset[.]p0[.]i64\\(ptr align 8 %a, i8 0, i64 8[^}]*store i64 %move[.]value, ptr %b")
    message(FATAL_ERROR "managed move must load the owner handle, zero the source, and store it in the destination")
endif()

if(NOT ir MATCHES
    "%move[.]value = load i64, ptr %local[^}]*call void @llvm[.]memset[.]p0[.]i64\\(ptr align 8 %local, i8 0, i64 8[^}]*ret i64 %move[.]value")
    message(FATAL_ERROR "managed move return must zero its local source before returning the handle")
endif()

if(NOT ir MATCHES "%move[.]value[0-9]* = load i64, ptr %fieldSource")
    message(FATAL_ERROR "managed field ownership transfer is missing")
endif()

if(NOT ir MATCHES "call i1 @absolute_managed_valid\\(i64")
    message(FATAL_ERROR "subscribers and weak observers must keep generation validity checks after a move")
endif()
