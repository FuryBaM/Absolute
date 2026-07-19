#pragma once

#include "lexer.h"
#include "plugin_api.h"

#include <string>
#include <vector>

namespace Absolute {
    PARSER_API void RegisterSyntaxPlugin(const AbsoluteSyntaxPluginV1* plugin);
    PARSER_API void ResetSyntaxPlugins();
    PARSER_API bool IsSyntaxPluginKeyword(const std::string& value);
    PARSER_API std::vector<Token> ExpandSyntaxPlugins(std::vector<Token> tokens);
}

