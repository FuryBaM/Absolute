if(NOT DEFINED COMPILER OR NOT DEFINED PLUGIN OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "COMPILER, PLUGIN, SOURCE, and OUTPUT are required")
endif()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${COMPILER}" "${SOURCE}" --plugin "${PLUGIN}" --emit-llvm -o "${OUTPUT}"
    RESULT_VARIABLE compiler_result
    OUTPUT_VARIABLE compiler_stdout
    ERROR_VARIABLE compiler_stderr
)
if(NOT compiler_result EQUAL 0)
    message(FATAL_ERROR
        "desktop RAII IR compilation failed (${compiler_result})\n"
        "${compiler_stdout}${compiler_stderr}")
endif()

file(READ "${OUTPUT}" ir)
if(NOT ir MATCHES
    "define internal void @Desktop[.]Window[.]__destroy\\(ptr %this\\)[^{]*\\{[^}]*call void @Desktop[.]Window[.]destroy\\(ptr %this\\)")
    message(FATAL_ERROR "managed Desktop.Window cleanup does not call Window.destroy()")
endif()
if(NOT ir MATCHES
    "define void @Desktop[.]Window[.]destroy\\(ptr %this\\)[^{]*\\{[^}]*call void @absolute_desktop_destroy\\(i64")
    message(FATAL_ERROR "Desktop.Window.destroy() does not release the native window handle")
endif()
