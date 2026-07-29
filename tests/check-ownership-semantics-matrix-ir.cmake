if(NOT DEFINED IR_FILE OR NOT EXISTS "${IR_FILE}")
    message(FATAL_ERROR "Ownership-semantics LLVM IR file is missing: ${IR_FILE}")
endif()

file(READ "${IR_FILE}" IR)

if(NOT IR MATCHES
    "define void @dropNode\\(i64 %node, i1 %node[.]is_owner\\)")
    message(FATAL_ERROR
        "managed parameter must carry its hidden ownership role")
endif()

if(NOT IR MATCHES "role[.]owner[.]cleanup")
    message(FATAL_ERROR
        "owner-role RAII cleanup was not emitted")
endif()

if(NOT IR MATCHES "define i64 @forwardNode\\(i64( returned)? %node, i1 %node[.]is_owner\\)")
    message(FATAL_ERROR
        "moving an owner-role managed parameter out must preserve the returned owner ABI")
endif()

if(NOT IR MATCHES
    "define i64 @?\"?forwardGeneric[$]Node_2A\"?\\(i64( returned)? %value, i1 %value[.]is_owner\\)")
    message(FATAL_ERROR
        "generic role-polymorphic T* specialization was not emitted")
endif()

if(NOT IR MATCHES
    "define.*@seesOwner\\(i64 %node, i1 returned %node[.]is_owner\\)")
    message(FATAL_ERROR
        "isOwner does not receive the hidden role")
endif()

if(NOT IR MATCHES
    "define i64 @extractOwner\\(i64( returned)? %node, i1 %node[.]is_owner\\)")
    message(FATAL_ERROR
        "isOwner-guarded ownership forwarding lost the role-polymorphic ABI")
endif()

if(NOT IR MATCHES
    "call void @dropArray\\(%absolute[.]array[.]int32[.]1 %[^,]+, i1 true\\)")
    message(FATAL_ERROR
        "move call did not pass the owner role")
endif()
