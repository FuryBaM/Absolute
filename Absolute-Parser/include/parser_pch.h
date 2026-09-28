#pragma once
#define BUILD_PARSER_DLL  // Building the parser library

#include <string>
#include <iostream>
#include <vector>
#include <memory>

#ifdef _WIN32
#ifdef BUILD_PARSER_DLL
#define PARSER_API  // Export when building the DLL
#else
#define PARSER_API  // Import when used by another project
#endif
#else
#define PARSER_API
#endif

#include "scope.h"
#include "parser.h"
#include "expression_visitor.h"

