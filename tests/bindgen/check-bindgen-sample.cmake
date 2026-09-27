if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR "Generated Absolute bindgen file is missing: ${GENERATED}")
endif()

file(READ "${GENERATED}" TEXT)

foreach(SNIPPET IN ITEMS
        "using SampleHandle = raw void*;"
        "extern \"C\" int32 sample_add(int32 left, int32 right);"
        "extern \"C\" void sample_increment(raw int32* value);"
        "extern \"C\" raw int8* sample_name();"
        "extern \"C\" SampleHandle sample_create(int32 tag);"
        "extern \"C\" void sample_destroy(SampleHandle handle);"
        "extern \"C\" bool sample_ready(SampleHandle handle);"
        "variadic functions are not supported")
    string(FIND "${TEXT}" "${SNIPPET}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Generated bindgen output is missing expected snippet:\n  ${SNIPPET}\n---\n${TEXT}")
    endif()
endforeach()

# Must not emit a bindable variadic prototype.
if(TEXT MATCHES "extern \"C\"[^\n]*sample_printf")
    message(FATAL_ERROR "Variadic sample_printf must not be emitted as extern \"C\"")
endif()


string(FIND "${TEXT}" "copy with std.text.fromCString" CSTRING_NOTE_POSITION)
if(CSTRING_NOTE_POSITION EQUAL -1)
    message(FATAL_ERROR "const char* return must explain how to copy the borrowed native pointer")
endif()
