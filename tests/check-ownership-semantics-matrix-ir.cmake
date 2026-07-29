if(NOT DEFINED IR_FILE OR NOT EXISTS "${IR_FILE}")
    message(FATAL_ERROR "Ownership-semantics LLVM IR file is missing: ${IR_FILE}")
endif()

file(READ "${IR_FILE}" IR)

if(NOT IR MATCHES
    "define void @consumeNode\\(i64 %node\\)[^{]*\\{[^}]*absolute_managed_destroy\\(i64 %node\\)")
    message(FATAL_ERROR
        "consume managed parameter must destroy its incoming owner on fallthrough")
endif()

if(NOT IR MATCHES
    "define void @consumeArray\\([^)]*%values\\)[^{]*\\{[^}]*call void @free\\(ptr %array[.]owner\\)")
    message(FATAL_ERROR
        "consume array parameter must free its incoming storage on fallthrough")
endif()

if(NOT IR MATCHES "define i64 @forwardNode\\(i64 returned %node\\)")
    message(FATAL_ERROR
        "moving a consume managed parameter out must preserve the returned owner ABI")
endif()

if(NOT IR MATCHES
    "define i64 @?\"?forwardGeneric[$]consume_20Node_2A\"?\\(i64( returned)? %value\\)")
    message(FATAL_ERROR
        "generic consume T* specialization was not emitted with owner return semantics")
endif()
