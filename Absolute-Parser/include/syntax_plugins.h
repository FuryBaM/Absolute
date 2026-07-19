#pragma once

#include "lexer.h"
#include "plugin_api.h"

#include <string>
#include <vector>

#ifndef PARSER_API
#define PARSER_API
#endif

namespace Absolute {
    struct PluginBinaryOperator {
        std::string pluginName;
        std::string leftType;
        std::string operatorText;
        std::string rightType;
        std::string functionName;
        std::string resultType;
    };

    PARSER_API void RegisterSyntaxPlugin(const AbsoluteSyntaxPluginV1* plugin);
    PARSER_API void RegisterSyntaxPluginPrelude(const std::string& pluginName, const char* source);
    PARSER_API void RegisterPluginBinaryOperators(
        const std::string& pluginName, const AbsoluteBinaryOperatorTableV1* operators);
    PARSER_API void ResetSyntaxPlugins();
    PARSER_API bool IsSyntaxPluginKeyword(const std::string& value);
    PARSER_API std::vector<std::string> SyntaxPluginPreludes();
    PARSER_API const PluginBinaryOperator* FindPluginBinaryOperator(
        const std::string& leftType, const std::string& operatorText, const std::string& rightType);
    PARSER_API std::vector<Token> ExpandSyntaxPlugins(std::vector<Token> tokens);
}
