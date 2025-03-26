#pragma once
#define BUILD_PARSER_DLL  // Сообщает, что создаётся библиотека

#include <string>
#include <iostream>
#include <vector>
#include <memory>

#ifdef _WIN32
#ifdef BUILD_PARSER_DLL
#define PARSER_API  // Экспорт при создании DLL
#else
#define PARSER_API  // Импорт в другом проекте
#endif
#else
#define PARSER_API
#endif

#include "scope.h"
#include "parser.h"
#include "expression_visitor.h"

