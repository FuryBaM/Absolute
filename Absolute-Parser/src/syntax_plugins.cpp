#include "parser_pch.h"
#include "syntax_plugins.h"
#include "ast/plugin.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Absolute {
    namespace {
        struct RegisteredRule {
            std::string pluginName;
            std::string keyword;
            AbsoluteSyntaxExpandV1 expand = nullptr;
            void* userData = nullptr;
        };

        std::vector<RegisteredRule> rules;
        std::unordered_map<std::string, size_t> rulesByKeyword;
        struct RegisteredOpaqueRule {
            std::string pluginName;
            std::string keyword;
            AbsoluteOpaqueParseV1 parse = nullptr;
            void* userData = nullptr;
        };
        std::vector<RegisteredOpaqueRule> opaqueRules;
        std::unordered_map<std::string, size_t> opaqueRulesByKeyword;
        std::vector<std::string> preludes;
        std::vector<PluginBinaryOperator> binaryOperators;

        std::string OperatorKey(const std::string& leftType, const std::string& operatorText,
            const std::string& rightType) {
            return leftType + "\n" + operatorText + "\n" + rightType;
        }

        std::unordered_map<std::string, size_t> binaryOperatorsBySignature;

        bool IsIdentifier(const std::string& value) {
            if (value.empty()) return false;
            const auto first = static_cast<unsigned char>(value.front());
            if (value.front() != '_' && !std::isalpha(first)) return false;
            return std::all_of(value.begin() + 1, value.end(), [](char character) {
                const auto byte = static_cast<unsigned char>(character);
                return character == '_' || std::isalnum(byte);
            });
        }

        std::string Location(const Token& token) {
            return "line " + std::to_string(token.line) + ", column " + std::to_string(token.column);
        }

        std::vector<AbsoluteSyntaxTokenV1> MakeTokenViews(
            const std::vector<Token>& tokens, size_t start) {
            std::vector<AbsoluteSyntaxTokenV1> result;
            result.reserve(tokens.size() - start);
            for (size_t index = start; index < tokens.size(); ++index) {
                const Token& token = tokens[index];
                result.push_back({
                    static_cast<uint32_t>(token.type), token.value.c_str(), token.value.size(),
                    static_cast<uint32_t>(token.line), static_cast<uint32_t>(token.column)
                });
            }
            return result;
        }

        void RebaseLocations(std::vector<Token>& replacement, const Token& trigger) {
            for (Token& token : replacement) {
                if (token.line == 1) token.column += trigger.column - 1;
                token.line += trigger.line - 1;
            }
        }

        struct ParserCursorContext {
            std::vector<AbsoluteSyntaxTokenV1> views;
            size_t index = 0;
        };

        size_t CursorRemaining(void* context) {
            const auto& cursor = *static_cast<ParserCursorContext*>(context);
            return cursor.index < cursor.views.size() ? cursor.views.size() - cursor.index : 0;
        }

        const AbsoluteSyntaxTokenV1* CursorPeek(void* context, size_t offset) {
            const auto& cursor = *static_cast<ParserCursorContext*>(context);
            const size_t index = cursor.index + offset;
            return index < cursor.views.size() ? &cursor.views[index] : nullptr;
        }

        int32_t CursorConsume(void* context, uint32_t expectedKind, const char* expectedText) {
            auto& cursor = *static_cast<ParserCursorContext*>(context);
            if (cursor.index >= cursor.views.size()) return 0;
            const AbsoluteSyntaxTokenV1& token = cursor.views[cursor.index];
            if (expectedKind != UINT32_MAX && token.kind != expectedKind) return 0;
            if (expectedText && std::string_view(token.text, token.text_length) != expectedText) return 0;
            ++cursor.index;
            return 1;
        }

        void DestroyOpaqueNode(AbsoluteOpaqueAstNodeV1& node) {
            if (node.payload && node.vtable && node.vtable->destroy) node.vtable->destroy(node.payload);
            node = {};
        }
    }

    void RegisterSyntaxPlugin(const AbsoluteSyntaxPluginV1* plugin) {
        if (!plugin) throw std::runtime_error("Syntax plugin returned a null descriptor");
        if (plugin->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION) {
            throw std::runtime_error(
                "Unsupported syntax plugin ABI " + std::to_string(plugin->abi_version) +
                "; compiler expects " + std::to_string(ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION));
        }
        if (!plugin->name || std::string(plugin->name).empty())
            throw std::runtime_error("Syntax plugin requires a non-empty name");
        if (plugin->rule_count != 0 && !plugin->rules)
            throw std::runtime_error("Syntax plugin '" + std::string(plugin->name) + "' has no rule table");

        std::unordered_set<std::string> newKeywords;
        for (size_t index = 0; index < plugin->rule_count; ++index) {
            const AbsoluteSyntaxRuleV1& rule = plugin->rules[index];
            const std::string keyword = rule.keyword ? rule.keyword : "";
            if (!IsIdentifier(keyword))
                throw std::runtime_error("Syntax plugin '" + std::string(plugin->name) +
                    "' registered invalid keyword '" + keyword + "'");
            if (!rule.expand)
                throw std::runtime_error("Syntax plugin rule '" + keyword + "' has no adapter");
            if (IsValidTokenValue(TokenType::KEYWORD, keyword))
                throw std::runtime_error("Syntax plugin cannot replace core keyword '" + keyword + "'");
            if (rulesByKeyword.contains(keyword) || opaqueRulesByKeyword.contains(keyword) ||
                !newKeywords.insert(keyword).second)
                throw std::runtime_error("Syntax keyword '" + keyword + "' is already registered");
        }

        for (size_t index = 0; index < plugin->rule_count; ++index) {
            const AbsoluteSyntaxRuleV1& rule = plugin->rules[index];
            const size_t ruleIndex = rules.size();
            rules.push_back({plugin->name, rule.keyword, rule.expand, rule.user_data});
            rulesByKeyword.emplace(rule.keyword, ruleIndex);
        }
    }

    void ResetSyntaxPlugins() {
        rulesByKeyword.clear();
        rules.clear();
        opaqueRulesByKeyword.clear();
        opaqueRules.clear();
        preludes.clear();
        binaryOperatorsBySignature.clear();
        binaryOperators.clear();
    }

    void RegisterSyntaxPluginPrelude(const std::string& pluginName, const char* source) {
        if (!source)
            throw std::runtime_error("Syntax plugin '" + pluginName + "' returned a null prelude");
        if (source[0] != '\0') preludes.emplace_back(source);
    }

    void RegisterPluginBinaryOperators(
        const std::string& pluginName, const AbsoluteBinaryOperatorTableV1* operators) {
        if (!operators) return;
        if (operators->rule_count != 0 && !operators->rules)
            throw std::runtime_error("Syntax plugin '" + pluginName + "' returned no operator table");
        std::unordered_set<std::string> newSignatures;
        for (size_t index = 0; index < operators->rule_count; ++index) {
            const AbsoluteBinaryOperatorRuleV1& rule = operators->rules[index];
            if (!rule.left_type || !rule.operator_text || !rule.right_type ||
                !rule.function_name || !rule.result_type)
                throw std::runtime_error("Syntax plugin '" + pluginName + "' returned an incomplete operator rule");
            const std::string key = OperatorKey(rule.left_type, rule.operator_text, rule.right_type);
            if (binaryOperatorsBySignature.contains(key) || !newSignatures.insert(key).second)
                throw std::runtime_error("Binary operator for '" + std::string(rule.left_type) + " " +
                    rule.operator_text + " " + rule.right_type + "' is already registered");
        }
        for (size_t index = 0; index < operators->rule_count; ++index) {
            const AbsoluteBinaryOperatorRuleV1& rule = operators->rules[index];
            const size_t ruleIndex = binaryOperators.size();
            binaryOperators.push_back({pluginName, rule.left_type, rule.operator_text,
                rule.right_type, rule.function_name, rule.result_type});
            binaryOperatorsBySignature.emplace(
                OperatorKey(rule.left_type, rule.operator_text, rule.right_type), ruleIndex);
        }
    }

    void RegisterOpaqueSyntaxRules(
        const std::string& pluginName, const AbsoluteOpaqueSyntaxTableV1* table) {
        if (!table) return;
        if (table->rule_count != 0 && !table->rules)
            throw std::runtime_error("Syntax plugin '" + pluginName + "' returned no opaque rule table");
        std::unordered_set<std::string> newKeywords;
        for (size_t index = 0; index < table->rule_count; ++index) {
            const AbsoluteOpaqueSyntaxRuleV1& rule = table->rules[index];
            const std::string keyword = rule.keyword ? rule.keyword : "";
            if (!IsIdentifier(keyword))
                throw std::runtime_error("Syntax plugin '" + pluginName +
                    "' registered invalid opaque keyword '" + keyword + "'");
            if (!rule.parse)
                throw std::runtime_error("Opaque syntax rule '" + keyword + "' has no parser callback");
            if (IsValidTokenValue(TokenType::KEYWORD, keyword))
                throw std::runtime_error("Syntax plugin cannot replace core keyword '" + keyword + "'");
            if (rulesByKeyword.contains(keyword) || opaqueRulesByKeyword.contains(keyword) ||
                !newKeywords.insert(keyword).second)
                throw std::runtime_error("Syntax keyword '" + keyword + "' is already registered");
        }
        for (size_t index = 0; index < table->rule_count; ++index) {
            const AbsoluteOpaqueSyntaxRuleV1& rule = table->rules[index];
            const size_t ruleIndex = opaqueRules.size();
            opaqueRules.push_back({pluginName, rule.keyword, rule.parse, rule.user_data});
            opaqueRulesByKeyword.emplace(rule.keyword, ruleIndex);
        }
    }

    bool IsSyntaxPluginKeyword(const std::string& value) {
        return rulesByKeyword.contains(value) || opaqueRulesByKeyword.contains(value);
    }

    std::vector<std::string> SyntaxPluginPreludes() {
        return preludes;
    }

    const PluginBinaryOperator* FindPluginBinaryOperator(
        const std::string& leftType, const std::string& operatorText, const std::string& rightType) {
        const auto found = binaryOperatorsBySignature.find(OperatorKey(leftType, operatorText, rightType));
        return found == binaryOperatorsBySignature.end() ? nullptr : &binaryOperators[found->second];
    }

    std::unique_ptr<Statement> TryParseOpaquePluginStatement(
        const std::vector<Token>& tokens, size_t& position) {
        if (position >= tokens.size()) return nullptr;
        const auto found = opaqueRulesByKeyword.find(tokens[position].value);
        if (found == opaqueRulesByKeyword.end()) return nullptr;
        const RegisteredOpaqueRule& rule = opaqueRules[found->second];
        const Token trigger = tokens[position];
        ParserCursorContext context{MakeTokenViews(tokens, position), 0};
        AbsoluteParserCursorV1 cursor{
            ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION, &context,
            &CursorRemaining, &CursorPeek, &CursorConsume
        };
        AbsoluteOpaqueParseResultV1 result{};
        int32_t succeeded = 0;
        try {
            succeeded = rule.parse(rule.userData, &cursor, &result);
        }
        catch (const std::exception& error) {
            DestroyOpaqueNode(result.node);
            throw std::runtime_error("Opaque syntax plugin '" + rule.pluginName + "' threw at " +
                Location(trigger) + ": " + error.what());
        }
        catch (...) {
            DestroyOpaqueNode(result.node);
            throw std::runtime_error("Opaque syntax plugin '" + rule.pluginName +
                "' threw an unknown exception at " + Location(trigger));
        }
        if (!succeeded) {
            const std::string detail = result.error_message ? result.error_message : "parser rejected input";
            DestroyOpaqueNode(result.node);
            throw std::runtime_error("Opaque syntax plugin '" + rule.pluginName + "' failed at " +
                Location(trigger) + ": " + detail);
        }
        if (context.index == 0 || context.index > context.views.size()) {
            DestroyOpaqueNode(result.node);
            throw std::runtime_error("Opaque syntax plugin '" + rule.pluginName +
                "' consumed an invalid token count at " + Location(trigger));
        }
        if (!result.node.payload || !result.node.vtable ||
            result.node.vtable->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION ||
            !result.node.vtable->destroy || !result.node.vtable->emit_llvm) {
            DestroyOpaqueNode(result.node);
            throw std::runtime_error("Opaque syntax plugin '" + rule.pluginName +
                "' returned an invalid AST node at " + Location(trigger));
        }
        position += context.index;
        return std::make_unique<OpaquePluginStmt>(rule.pluginName, rule.keyword, result.node);
    }

    std::vector<Token> ExpandSyntaxPlugins(std::vector<Token> tokens) {
        constexpr size_t maximumExpansions = 10000;
        size_t expansionCount = 0;
        size_t index = 0;

        while (index < tokens.size()) {
            const auto found = rulesByKeyword.find(tokens[index].value);
            if (found == rulesByKeyword.end()) {
                ++index;
                continue;
            }
            if (++expansionCount > maximumExpansions)
                throw std::runtime_error("Syntax plugin expansion limit exceeded; possible recursive adapter");

            const RegisteredRule& rule = rules[found->second];
            const Token trigger = tokens[index];
            std::vector<AbsoluteSyntaxTokenV1> views = MakeTokenViews(tokens, index);
            AbsoluteSyntaxExpansionV1 expansion{};
            int32_t succeeded = 0;
            try {
                succeeded = rule.expand(rule.userData, views.data(), views.size(), &expansion);
            }
            catch (const std::exception& error) {
                throw std::runtime_error("Syntax plugin '" + rule.pluginName + "' threw at " +
                    Location(trigger) + ": " + error.what());
            }
            catch (...) {
                throw std::runtime_error("Syntax plugin '" + rule.pluginName +
                    "' threw an unknown exception at " + Location(trigger));
            }

            if (!succeeded) {
                const std::string detail = expansion.error_message ? expansion.error_message : "adapter rejected input";
                throw std::runtime_error("Syntax plugin '" + rule.pluginName + "' failed at " +
                    Location(trigger) + ": " + detail);
            }
            if (expansion.consumed_tokens == 0 || expansion.consumed_tokens > views.size()) {
                throw std::runtime_error("Syntax plugin '" + rule.pluginName +
                    "' returned an invalid consumed token count at " + Location(trigger));
            }

            std::vector<Token> replacement;
            if (expansion.replacement_source && expansion.replacement_source[0] != '\0') {
                replacement = lexer(expansion.replacement_source);
                RebaseLocations(replacement, trigger);
            }
            const auto first = tokens.begin() + static_cast<std::ptrdiff_t>(index);
            const auto last = first + static_cast<std::ptrdiff_t>(expansion.consumed_tokens);
            tokens.erase(first, last);
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(index),
                std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
        }
        return tokens;
    }
}
