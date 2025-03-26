#pragma once

#ifdef _WIN32
#define PARSER_API
#define ANALYZER_API  // Импорт в другом проекте
#else
#define PARSER_API
#define ANALYZER_API
#endif

#include "parser_pch.h"
#include "type.h"
#include "variable.h"
#include "analyzer.h"