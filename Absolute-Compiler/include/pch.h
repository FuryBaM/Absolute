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
#define EXPORT_API __declspec(dllimport)  // Импорт в другом проекте
#else
#define EXPORT_API
#endif
