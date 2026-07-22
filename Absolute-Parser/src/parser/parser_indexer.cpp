#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    namespace {
        std::unique_ptr<Expression> CloneIndexerTypeExpression(const Expression& expression);

        std::unique_ptr<TypeExpr> CloneIndexerType(const TypeExpr& type) {
            if (const auto* primitive = dynamic_cast<const PrimitiveTypeExpr*>(&type))
                return std::make_unique<PrimitiveTypeExpr>(primitive->type);
            if (const auto* user = dynamic_cast<const UserTypeExpr*>(&type))
                return std::make_unique<UserTypeExpr>(CloneIndexerTypeExpression(*user->typeExpr));
            if (const auto* pointer = dynamic_cast<const PointerTypeExpr*>(&type))
                return std::make_unique<PointerTypeExpr>(
                    CloneIndexerType(*pointer->pointee), pointer->raw);
            if (const auto* array = dynamic_cast<const ArrayTypeExpr*>(&type))
                return std::make_unique<ArrayTypeExpr>(CloneIndexerType(*array->element));
            throw std::runtime_error("Unsupported indexer type expression");
        }

        std::unique_ptr<Expression> CloneIndexerTypeExpression(const Expression& expression) {
            if (const auto* type = dynamic_cast<const TypeExpr*>(&expression))
                return CloneIndexerType(*type);
            if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression))
                return std::make_unique<IdentifierExpr>(identifier->name);
            if (const auto* member = dynamic_cast<const MemberAccessExpr*>(&expression))
                return std::make_unique<MemberAccessExpr>(
                    CloneIndexerTypeExpression(*member->base), member->member);
            if (const auto* specialization = dynamic_cast<const TemplateExpr*>(&expression)) {
                std::vector<std::unique_ptr<Expression>> arguments;
                for (const auto& argument : specialization->types)
                    arguments.push_back(CloneIndexerTypeExpression(*argument));
                return std::make_unique<TemplateExpr>(
                    CloneIndexerTypeExpression(*specialization->base), std::move(arguments));
            }
            throw std::runtime_error("Unsupported indexer type name");
        }

        std::unique_ptr<VarDeclExpr> CloneIndexerParameter(const VarDeclExpr& parameter) {
            auto* identifier = dynamic_cast<IdentifierExpr*>(parameter.name.get());
            auto result = std::make_unique<VarDeclExpr>(
                CloneIndexerTypeExpression(*parameter.type),
                std::make_unique<IdentifierExpr>(identifier ? identifier->name : std::string{}),
                nullptr);
            result->isConst = parameter.isConst;
            result->isReference = parameter.isReference;
            return result;
        }

        bool HasIndexerAccessModifier(const std::vector<Token>& modifiers) {
            return std::any_of(modifiers.begin(), modifiers.end(), [](const Token& modifier) {
                return modifier.value == "public" || modifier.value == "protected" ||
                    modifier.value == "private" || modifier.value == "internal";
            });
        }

        std::vector<Token> IndexerAccessorModifiers(
            const std::vector<Token>& indexerModifiers,
            const std::vector<Token>& declaredModifiers) {
            std::vector<Token> result = indexerModifiers;
            if (HasIndexerAccessModifier(declaredModifiers)) {
                result.erase(std::remove_if(result.begin(), result.end(), [](const Token& modifier) {
                    return modifier.value == "public" || modifier.value == "protected" ||
                        modifier.value == "private" || modifier.value == "internal";
                }), result.end());
            }
            result.insert(result.end(), declaredModifiers.begin(), declaredModifiers.end());
            return result;
        }

        size_t SkipIndexerType(const std::vector<Token>& tokens, size_t index) {
            if (index < tokens.size() && tokens[index].type == TokenType::KEYWORD &&
                tokens[index].value == "raw") ++index;
            if (index >= tokens.size()) return tokens.size();
            if (tokens[index].type == TokenType::KEYWORD && IsPrimitiveType(tokens[index].value)) {
                ++index;
            }
            else if (tokens[index].type == TokenType::IDENTIFIER) {
                ++index;
                int templateDepth = 0;
                while (index < tokens.size()) {
                    if (index + 1 < tokens.size() && templateDepth == 0 &&
                        tokens[index].value == "." &&
                        tokens[index + 1].type == TokenType::IDENTIFIER) index += 2;
                    else if (tokens[index].value == "<") { ++templateDepth; ++index; }
                    else if (tokens[index].value == ">" && templateDepth > 0) { --templateDepth; ++index; }
                    else if (templateDepth > 0) ++index;
                    else break;
                }
            }
            else return tokens.size();
            while (index < tokens.size() && tokens[index].type == TokenType::OPERATOR &&
                tokens[index].value == "*") ++index;
            while (index + 1 < tokens.size() && tokens[index].value == "[" &&
                tokens[index + 1].value == "]") index += 2;
            return index;
        }
    }

    bool Parser::LooksLikeIndexerDeclaration() const {
        const size_t index = SkipIndexerType(tokens, pos);
        return index + 1 < tokens.size() && tokens[index].type == TokenType::KEYWORD &&
            tokens[index].value == "this" && tokens[index + 1].type == TokenType::BRACKET &&
            tokens[index + 1].value == "[";
    }

    std::unique_ptr<IndexerDeclStmt> Parser::ParseIndexerDeclaration() {
        const std::vector<Token> indexerModifiers = modifiers;
        const std::vector<Attribute> indexerAttributes = attributes;
        std::unique_ptr<TypeExpr> elementType = ParseType();
        Token thisToken = *Consume(TokenType::KEYWORD, "this");
        Consume(TokenType::BRACKET, "[");
        std::vector<std::unique_ptr<VarDeclExpr>> indexParameters;
        while (RequireCurrent("an indexer parameter or ']'")->value != "]") {
            const bool isConst = CurrentToken()->type == TokenType::KEYWORD &&
                CurrentToken()->value == "const";
            if (isConst) Consume(TokenType::KEYWORD, "const");
            std::unique_ptr<TypeExpr> parameterType = ParseType();
            Token* parameterName = Consume(TokenType::IDENTIFIER);
            auto parameter = std::make_unique<VarDeclExpr>(
                std::move(parameterType),
                std::make_unique<IdentifierExpr>(parameterName->value), nullptr);
            parameter->isConst = isConst;
            indexParameters.push_back(std::move(parameter));
            if (CurrentToken() && CurrentToken()->value == ",") {
                Consume(TokenType::DELIMITER, ",");
                continue;
            }
            break;
        }
        Consume(TokenType::BRACKET, "]");
        if (indexParameters.empty()) {
            ReportSyntaxError(&thisToken, "Indexer requires at least one parameter");
            throw std::runtime_error("Empty indexer parameter list");
        }
        Consume(TokenType::BRACKET, "{");

        std::unique_ptr<FunctionDeclStmt> getter;
        std::unique_ptr<FunctionDeclStmt> setter;
        while (CurrentToken() && !(CurrentToken()->type == TokenType::BRACKET &&
            CurrentToken()->value == "}")) {
            ParseAttributes();
            ParseModifiers();
            const std::vector<Token> declaredModifiers = modifiers;
            const std::vector<Attribute> accessorAttributes = attributes;
            Token* accessor = CurrentToken();
            if (!accessor || accessor->type != TokenType::KEYWORD ||
                (accessor->value != "get" && accessor->value != "set")) {
                ReportSyntaxError(accessor, "Expected an indexer 'get' or 'set' accessor");
                throw std::runtime_error("Invalid indexer accessor");
            }
            const bool isGetter = accessor->value == "get";
            Token accessorToken = *Consume(TokenType::KEYWORD, accessor->value);
            std::unique_ptr<Statement> body;
            if (CurrentToken() && CurrentToken()->type == TokenType::DELIMITER &&
                CurrentToken()->value == ";") {
                Consume(TokenType::DELIMITER, ";");
                if (GetCurrentScopeType() != ScopeType::Interface) {
                    ReportSyntaxError(&accessorToken,
                        "Class and struct indexers require explicit accessor bodies");
                    throw std::runtime_error("Automatic indexer accessor is not supported");
                }
            }
            else if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
                CurrentToken()->value == "{") body = ParseCompoundStatement();
            else {
                ReportSyntaxError(CurrentToken(), "Expected ';' or an indexer accessor body");
                throw std::runtime_error("Invalid indexer accessor body");
            }

            std::vector<std::unique_ptr<VarDeclExpr>> parameters;
            for (const auto& parameter : indexParameters)
                parameters.push_back(CloneIndexerParameter(*parameter));
            std::unique_ptr<TypeExpr> returnType;
            if (isGetter) {
                if (getter) {
                    ReportSyntaxError(accessor, "Duplicate indexer getter");
                    throw std::runtime_error("Duplicate indexer getter");
                }
                returnType = CloneIndexerType(*elementType);
            }
            else {
                if (setter) {
                    ReportSyntaxError(accessor, "Duplicate indexer setter");
                    throw std::runtime_error("Duplicate indexer setter");
                }
                returnType = std::make_unique<PrimitiveTypeExpr>("void");
                parameters.push_back(std::make_unique<VarDeclExpr>(
                    CloneIndexerType(*elementType),
                    std::make_unique<IdentifierExpr>("value"), nullptr));
            }
            Token methodToken = accessorToken;
            methodToken.type = TokenType::IDENTIFIER;
            methodToken.value = isGetter ? IndexerGetterName() : IndexerSetterName();
            auto method = std::make_unique<FunctionDeclStmt>(
                std::move(returnType), std::make_unique<Token>(methodToken),
                std::move(parameters), std::move(body));
            method->propertyAccessor = isGetter
                ? PropertyAccessorKind::Getter : PropertyAccessorKind::Setter;
            method->propertyName = "this";
            method->modifiers = IndexerAccessorModifiers(indexerModifiers, declaredModifiers);
            method->attributes = accessorAttributes;
            if (isGetter) getter = std::move(method);
            else setter = std::move(method);
        }
        Consume(TokenType::BRACKET, "}");
        if (!getter && !setter) {
            ReportSyntaxError(&thisToken, "Indexer requires at least one accessor");
            throw std::runtime_error("Empty indexer declaration");
        }
        auto indexer = std::make_unique<IndexerDeclStmt>(
            std::move(elementType), std::move(indexParameters),
            std::move(getter), std::move(setter));
        indexer->modifiers = indexerModifiers;
        indexer->attributes = indexerAttributes;
        return indexer;
    }
}
