#pragma once

#include "lexer.h"
#include "plugin_api.h"

#include <string>
#include <memory>
#include <vector>

#ifndef PARSER_API
#define PARSER_API
#endif

namespace Absolute {
    struct Statement;
    struct PluginBinaryOperator {
        std::string pluginName;
        std::string leftType;
        std::string operatorText;
        std::string rightType;
        std::string functionName;
        std::string resultType;
    };

    struct PluginResourceDescriptor {
        std::string pluginName;
        std::string typeName;
        bool isResource;
        std::string destroyFunction;
        std::string moveIntoFunction;
        std::string copyMessageFunction;
        std::string makeImmutableFunction;
        std::string rehomeFunction;

        bool canCrossIsolateBoundary() const {
            return !copyMessageFunction.empty() ||
                   !makeImmutableFunction.empty() ||
                   !rehomeFunction.empty();
        }
    };

    PARSER_API void RegisterSyntaxPlugin(const AbsoluteSyntaxPluginV1* plugin);
    PARSER_API void RegisterSyntaxPluginPrelude(const std::string& pluginName, const char* source);
    PARSER_API void RegisterPluginBinaryOperators(
        const std::string& pluginName, const AbsoluteBinaryOperatorTableV1* operators);
    PARSER_API void RegisterOpaqueSyntaxRules(
        const std::string& pluginName, const AbsoluteOpaqueSyntaxTableV1* rules);
    PARSER_API void RegisterPluginResources(
        const std::string& pluginName, const AbsoluteResourceTableV1* resources);
    PARSER_API void RegisterPluginVirtualModules(
        const std::string& pluginName, const AbsoluteVirtualModuleTableV1* modules);
    PARSER_API void ResetSyntaxPlugins();
    PARSER_API void UnregisterSyntaxPlugin(const std::string& pluginName);
    PARSER_API bool IsSyntaxPluginKeyword(const std::string& value);
    PARSER_API std::vector<std::string> SyntaxPluginPreludes();
    PARSER_API const PluginBinaryOperator* FindPluginBinaryOperator(
        const std::string& leftType, const std::string& operatorText, const std::string& rightType);
    PARSER_API const PluginResourceDescriptor* GetPluginResourceDescriptor(const std::string& typeName);
    PARSER_API const std::string* FindPluginVirtualModule(const std::string& moduleName);
    PARSER_API std::unique_ptr<Statement> TryParseOpaquePluginStatement(
        const std::vector<Token>& tokens, size_t& position);
    PARSER_API std::vector<Token> ExpandSyntaxPlugins(std::vector<Token> tokens);
}
