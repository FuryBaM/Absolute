if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)

string(REGEX MATCH
    "define internal void @GraphNode[.]__destroy\\(ptr %this\\) \\{[^}]*\\}"
    graph_node_destructor "${ir}")
if(graph_node_destructor STREQUAL "")
    message(FATAL_ERROR "missing GraphNode destructor")
endif()
if(graph_node_destructor MATCHES "parent[.]address")
    message(FATAL_ERROR "weak parent back-edge must not participate in owning cleanup")
endif()
if(NOT graph_node_destructor MATCHES "aggregate[.]destroy[.]slot")
    message(FATAL_ERROR "strong child must be destroyed recursively through its runtime destructor")
endif()

string(REGEX MATCHALL
    "call void @absolute_managed_destroy\\(i64"
    managed_destroy_calls "${graph_node_destructor}")
list(LENGTH managed_destroy_calls managed_destroy_count)
if(NOT managed_destroy_count EQUAL 1)
    message(FATAL_ERROR
        "GraphNode destructor must release exactly its one strong child; got ${managed_destroy_count} releases")
endif()

if(NOT ir MATCHES "call i1 @absolute_managed_valid\\(i64")
    message(FATAL_ERROR "weak observers must retain generation-checked validity checks")
endif()
