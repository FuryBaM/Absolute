if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)

if(NOT ir MATCHES "define i64 @observe\\(i64 %node\\)")
    message(FATAL_ERROR "weak return must use the existing managed handle ABI")
endif()
if(NOT ir MATCHES "call i1 @absolute_managed_valid\\(i64")
    message(FATAL_ERROR "weak truthiness must validate the generation-checked handle")
endif()

string(REGEX MATCH
    "define internal void @WeakNode[.]__destroy\\(ptr %this\\) \\{[^}]*\\}"
    weak_node_destructor "${ir}")
if(weak_node_destructor STREQUAL "")
    message(FATAL_ERROR "missing WeakNode destructor")
endif()
if(weak_node_destructor MATCHES "absolute_managed_destroy")
    message(FATAL_ERROR "weak field must not be destroyed as an owning managed field")
endif()
