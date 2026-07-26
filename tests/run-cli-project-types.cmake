if(NOT DEFINED COMPILER OR NOT DEFINED NODE OR NOT DEFINED CLI OR
    NOT DEFINED STD_ENV OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COMPILER, NODE, CLI, STD_ENV and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_absolute_cli label working_directory)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "ABSOLUTEC=${COMPILER}"
            "${NODE}" "${CLI}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status}):\n${output}\n${error}")
    endif()
    set("${label}_OUTPUT" "${output}" PARENT_SCOPE)
endfunction()

run_absolute_cli(create_app "${WORK_DIR}"
    new CliApp --type app --directory "${WORK_DIR}/CliApp")
set(app_project "${WORK_DIR}/CliApp/CliApp.absproj")
set(app_source "${WORK_DIR}/CliApp/src/main.abs")
if(NOT EXISTS "${app_project}" OR NOT EXISTS "${app_source}")
    message(FATAL_ERROR "application template files were not created")
endif()
file(READ "${app_project}" app_manifest)
if(NOT app_manifest MATCHES "\"type\"[ \t]*:[ \t]*\"app\"")
    message(FATAL_ERROR "application manifest does not declare type=app:\n${app_manifest}")
endif()
if(NOT app_manifest MATCHES "\"runArgs\"[ \t]*:[ \t]*\\[\\]")
    message(FATAL_ERROR "application manifest does not declare empty runArgs:\n${app_manifest}")
endif()
string(REPLACE "\"runArgs\": []" "\"runArgs\": [\"project-default\"]"
    app_manifest "${app_manifest}")
file(WRITE "${app_project}" "${app_manifest}")
file(TO_CMAKE_PATH "${STD_ENV}" std_env_path)
file(WRITE "${app_source}" "import \"${std_env_path}\";\n\n"
    "int32 main() {\n"
    "    assert(std.env.argsCount() == 2, \"project + CLI launch args\");\n"
    "    assert(std.env.argAt(0) == \"project-default\", \"project runArgs order\");\n"
    "    assert(std.env.argAt(1) == \"cli-test\", \"CLI run args order\");\n"
    "    println(\"cli-run-args=ok\");\n"
    "    return 0;\n"
    "}\n")

run_absolute_cli(build_app "${WORK_DIR}/CliApp" build)
set(app_artifact "${WORK_DIR}/CliApp/build/CliApp${APP_SUFFIX}")
if(NOT EXISTS "${app_artifact}")
    message(FATAL_ERROR "application artifact was not created: ${app_artifact}")
endif()
run_absolute_cli(run_app "${WORK_DIR}/CliApp" run -- cli-test)
if(NOT run_app_OUTPUT MATCHES "cli-run-args=ok")
    message(FATAL_ERROR "application output was unexpected:\n${run_app_OUTPUT}")
endif()

run_absolute_cli(create_lib "${WORK_DIR}"
    new CliLib --type lib --directory "${WORK_DIR}/CliLib")
set(lib_project "${WORK_DIR}/CliLib/CliLib.absproj")
set(lib_source "${WORK_DIR}/CliLib/src/lib.abs")
if(NOT EXISTS "${lib_project}" OR NOT EXISTS "${lib_source}")
    message(FATAL_ERROR "library template files were not created")
endif()
file(READ "${lib_project}" lib_manifest)
if(NOT lib_manifest MATCHES "\"type\"[ \t]*:[ \t]*\"lib\"")
    message(FATAL_ERROR "library manifest does not declare type=lib:\n${lib_manifest}")
endif()

run_absolute_cli(build_lib "${WORK_DIR}/CliLib" build)
set(lib_artifact "${WORK_DIR}/CliLib/build/${LIB_PREFIX}CliLib${LIB_SUFFIX}")
if(NOT EXISTS "${lib_artifact}")
    message(FATAL_ERROR "library artifact was not created: ${lib_artifact}")
endif()
run_absolute_cli(clean_lib "${WORK_DIR}/CliLib" clean)
if(EXISTS "${WORK_DIR}/CliLib/build")
    message(FATAL_ERROR "absolute clean did not remove the project build directory")
endif()
