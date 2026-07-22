if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)
string(REGEX MATCHALL "call ptr @absolute_managed_require" require_calls "${ir}")
list(LENGTH require_calls require_count)
if(NOT require_count EQUAL 3)
    message(FATAL_ERROR
        "expected allocation checks plus one borrowed slow path, found ${require_count}")
endif()
if(ir MATCHES "[.]owner = alloca i1" OR ir MATCHES "absolute_managed_destroy_if")
    message(FATAL_ERROR "runtime owner flag remains in generated IR")
endif()
if(NOT ir MATCHES "cached[.]pointee")
    message(FATAL_ERROR "managed owner pointee cache is missing")
endif()
string(REGEX MATCH
    "while[.]body:[^\n]*\n([^\n]*\n)*while[.]end:"
    owner_loop "${ir}")
if(NOT owner_loop)
    message(FATAL_ERROR "managed owner loop was not found")
endif()
if(owner_loop MATCHES "absolute_managed_|managed[.]slots|managed[.]gen")
    message(FATAL_ERROR "managed owner loop still performs a lifetime/generation check")
endif()
if(NOT owner_loop MATCHES "cached[.]pointee")
    message(FATAL_ERROR "managed owner loop does not use the cached pointee")
endif()

string(REGEX MATCH
    "define i64 @checkedBorrowedDeref[(]i64 [^)]*[)] [{][^\n]*\n([^\n]*\n)*[}]"
    borrowed_function "${ir}")
if(NOT borrowed_function MATCHES "absolute_managed_require")
    message(FATAL_ERROR "borrowed managed dereference lost its lifetime check")
endif()
