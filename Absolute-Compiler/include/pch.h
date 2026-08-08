#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <iterator>
#include <regex>
#include <vector>
#include <memory>

#ifdef _WIN32
#define PARSER_API  // Èìïîðò â äðóãîì ïðîåêòå
#define ANALYZER_API
#else
#define PARSER_API
#define ANALYZER_API
#endif

#include "scope.h"
#include "parser.h"
#include "expression_visitor.h"

#include "type.h"
#include "variable.h"
#include "analyzer.h"

// Available in every configuration: the driver parses -O0..-O3 even when the
// LLVM backend is not compiled in.
#include "optimization_level.h"

#ifdef ABSOLUTE_HAS_LLVM
#include "codegen.h"
#endif
