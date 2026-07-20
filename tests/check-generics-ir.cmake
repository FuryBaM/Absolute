if(NOT DEFINED FUNCTIONS_IR OR NOT DEFINED TYPES_IR)
    message(FATAL_ERROR "FUNCTIONS_IR and TYPES_IR are required")
endif()

file(READ "${FUNCTIONS_IR}" functions_ir)
foreach(symbol IN ITEMS
    "identity$int32"
    "identity$double"
    "identity$string"
    "first$int32_5B_5D"
    "chooseFirst$int32$int32")
    string(FIND "${functions_ir}" "define" has_definitions)
    string(FIND "${functions_ir}" "@\"${symbol}\"" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "missing generic function specialization ${symbol}")
    endif()
endforeach()
if(functions_ir MATCHES "define[^\n]*@identity\\(")
    message(FATAL_ERROR "open generic identity template was emitted")
endif()

file(READ "${TYPES_IR}" types_ir)
foreach(type_name IN ITEMS
    "absolute.struct.Pair<int32>"
    "absolute.class.Box<int32>"
    "absolute.class.Box<double>")
    string(FIND "${types_ir}" "${type_name}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "missing generic aggregate specialization ${type_name}")
    endif()
endforeach()
