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
#define PARSER_API  // Импорт в другом проекте
#define ANALYZER_API
#else
#define PARSER_API
#define ANALYZER_API
#endif

#include "analyzer_pch.h"