if(NOT DEFINED IR_FILE OR NOT EXISTS "${IR_FILE}")
    message(FATAL_ERROR "Ownership-stability LLVM IR file is missing: ${IR_FILE}")
endif()

file(READ "${IR_FILE}" IR)

foreach(signature IN ITEMS
    "define i64 @inspectNode\\(i64 %node, i1 %node[.]is_owner\\)"
    "define i64 @routeNode\\(i64 %node, i1 %node[.]is_owner, i1 %forward\\)"
    "define %absolute[.]array[.]int32[.]1 @routeArray\\(%absolute[.]array[.]int32[.]1 %values, i1 %values[.]is_owner\\)"
    "define void @dropArray\\(%absolute[.]array[.]int32[.]1 %values, i1 %values[.]is_owner\\)"
    "define void @routePacket\\(ptr[^,]* %__result, ptr[^,]* %packet, i1 %packet[.]is_owner\\)"
    "define void @dropPacket\\(ptr[^,]* %packet, i1 %packet[.]is_owner\\)"
)
    if(NOT IR MATCHES "${signature}")
        message(FATAL_ERROR
            "role-polymorphic ownership ABI is missing: ${signature}")
    endif()
endforeach()

if(NOT IR MATCHES "role[.]owner[.]cleanup")
    message(FATAL_ERROR
        "owner-role RAII cleanup was not emitted")
endif()

if(NOT IR MATCHES
    "define i64 @?\"?genericRoute[$]StableNode_2A\"?\\(i64( returned)? %value, i1 %value[.]is_owner\\)")
    message(FATAL_ERROR
        "generic forwarding lost the hidden ownership role")
endif()

if(NOT IR MATCHES "absolute_managed_valid")
    message(FATAL_ERROR
        "weak references are not lowered through the managed runtime")
endif()
