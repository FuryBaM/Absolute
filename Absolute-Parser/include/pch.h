#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <memory>

#ifdef _WIN32
#define EXPORT_API __declspec(dllexport)  // Ёкспорт при создании DLL
#else
#define EXPORT_API
#endif

#include "lexer.h"
#include "scope.h"
#include "nodes.h"
#include "expression_visitor.h"

