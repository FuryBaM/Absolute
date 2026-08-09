if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Struct ABI LLVM IR file is missing: ${INPUT}")
endif()

file(READ "${INPUT}" IR)

set(REQUIRED_FRAGMENTS
    "define %absolute.struct.Pair @makePair(i32 %x, i32 %y)"
    "define i32 @sumPair(%absolute.struct.Pair %value)"
    "define void @makeLarge(ptr %__result, i64 %seed)"
    "define i64 @mutateLargeCopy(ptr %value)"
    "define void @LargeBox.read(ptr %__result, ptr %this)"
    "define void @\"LargeBox.__ctor$LargeValue\"(ptr %this, ptr %initial)"
    "define void @\"LargeMapper.map$LargeValue\"(ptr %__result, ptr %this, ptr %value)"
    "call void @makeLarge(ptr %makeLarge.result, i64 10)"
    "call i64 @mutateLargeCopy(ptr %value.argument.copy)"
)

foreach(FRAGMENT IN LISTS REQUIRED_FRAGMENTS)
    string(FIND "${IR}" "${FRAGMENT}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Expected struct value ABI fragment was not emitted: ${FRAGMENT}")
    endif()
endforeach()
