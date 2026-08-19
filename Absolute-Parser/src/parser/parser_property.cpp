#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    namespace {
        std::unique_ptr<Expression> CloneTypeExpression(const Expression& expression);

        std::unique_ptr<TypeExpr> CloneType(const TypeExpr& type) {
            if (const auto* primitive = dynamic_cast<const PrimitiveTypeExpr*>(&type))
                return std::make_unique<PrimitiveTypeExpr>(primitive->type);
            if (const auto* user = dynamic_cast<const UserTypeExpr*>(&type))
                return std::make_unique<UserTypeExpr>(CloneTypeExpression(*user->typeExpr));
            if (const auto* pointer = dynamic_cast<const PointerTypeExpr*>(&type))
                return std::make_unique<PointerTypeExpr>(
                    CloneType(*pointer->pointee), pointer->ownership);
            if (const auto* array = dynamic_cast<const ArrayTypeExpr*>(&type))
                return std::make_unique<ArrayTypeExpr>(CloneType(*array->element));
            throw std::runtime_error("Unsupported property type expression");
        }

        std::unique_ptr<Expression> CloneTypeExpression(const Expression& expression) {
            if (const auto* type = dynamic_cast<const TypeExpr*>(&expression)) return CloneType(*type);
            if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression))
                return std::make_unique<IdentifierExpr>(identifier->name);
            if (const auto* member = dynamic_cast<const MemberAccessExpr*>(&expression))
                return std::make_unique<MemberAccessExpr>(
                    CloneTypeExpression(*member->base), member->member);
            if (const auto* specialization = dynamic_cast<const TemplateExpr*>(&expression)) {
                std::vector<std::unique_ptr<Expression>> arguments;
                arguments.reserve(specialization->types.size());
                for (const auto& argument : specialization->types)
                    arguments.push_back(CloneTypeExpression(*argument));
                return std::make_unique<TemplateExpr>(
                    CloneTypeExpression(*specialization->base), std::move(arguments));
            }
            throw std::runtime_error("Unsupported property type name");
        }

        bool HasAccessModifier(const std::vector<Token>& modifiers) {
            return std::any_of(modifiers.begin(), modifiers.end(), [](const Token& modifier) {
                return modifier.value == "public" || modifier.value == "protected" ||
                    modifier.value == "private" || modifier.value == "internal";
            });
        }

        std::vector<Token> AccessorModifiers(
            const std::vector<Token>& propertyModifiers,
            const std::vector<Token>& declaredModifiers) {
            std::vector<Token> result = propertyModifiers;
            if (HasAccessModifier(declaredModifiers)) {
                result.erase(std::remove_if(result.begin(), result.end(), [](const Token& modifier) {
                    return modifier.value == "public" || modifier.value == "protected" ||
                        modifier.value == "private" || modifier.value == "internal";
                }), result.end());
            }
            result.insert(result.end(), declaredModifiers.begin(), declaredModifiers.end());
            return result;
        }
    }

    bool Parser::LooksLikePropertyDeclaration() const {
        size_t index = SkipOwnershipQualifier(pos);
        if (index >= tokens.size()) return false;
        if (tokens[index].type == TokenType::KEYWORD && IsPrimitiveType(tokens[index].value)) {
            ++index;
        }
        else if (tokens[index].type == TokenType::IDENTIFIER) {
            ++index;
            while (index < tokens.size()) {
                if (index + 1 < tokens.size() && tokens[index].value == "." &&
                    tokens[index + 1].type == TokenType::IDENTIFIER) {
                    index += 2;
                }
                else if (tokens[index].value == "<") {
                    size_t close = 0;
                    if (!IsTemplateArgumentList(index, &close)) return false;
                    index = close + 1;
                }
                else break;
            }
        }
        else return false;
        while (index < tokens.size() && tokens[index].type == TokenType::OPERATOR &&
            tokens[index].value == "*") ++index;
        while (index + 1 < tokens.size() && tokens[index].value == "[" &&
            tokens[index + 1].value == "]") index += 2;
        if (index >= tokens.size() || tokens[index].type != TokenType::IDENTIFIER) return false;
        ++index;
        return index < tokens.size() && tokens[index].type == TokenType::BRACKET &&
            tokens[index].value == "{";
    }

    std::unique_ptr<PropertyDeclStmt> Parser::ParsePropertyDeclaration() {
        const std::vector<Token> propertyModifiers = modifiers;
        const std::vector<Attribute> propertyAttributes = attributes;
        std::unique_ptr<TypeExpr> propertyType = ParseType();
        Token propertyToken = *Consume(TokenType::IDENTIFIER);
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
                ReportSyntaxError(accessor, "Expected a property 'get' or 'set' accessor");
                throw std::runtime_error("Invalid property accessor");
            }
            const bool isGetter = accessor->value == "get";
            Token accessorToken = *Consume(TokenType::KEYWORD, accessor->value);
            std::unique_ptr<Statement> body;
            bool automatic = false;
            if (CurrentToken() && CurrentToken()->type == TokenType::DELIMITER &&
                CurrentToken()->value == ";") {
                Consume(TokenType::DELIMITER, ";");
                const ScopeType scope = GetCurrentScopeType();
                automatic = scope == ScopeType::Class || scope == ScopeType::Struct;
            }
            else if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
                CurrentToken()->value == "{") {
                body = ParseCompoundStatement();
            }
            else {
                ReportSyntaxError(CurrentToken(), "Expected ';' or a property accessor body");
                throw std::runtime_error("Invalid property accessor body");
            }

            Token methodToken = accessorToken;
            methodToken.type = TokenType::IDENTIFIER;
            methodToken.value = isGetter
                ? PropertyGetterName(propertyToken.value) : PropertySetterName(propertyToken.value);
            std::vector<std::unique_ptr<VarDeclExpr>> parameters;
            std::unique_ptr<TypeExpr> returnType;
            if (isGetter) {
                if (getter) {
                    ReportSyntaxError(accessor, "Duplicate property getter");
                    throw std::runtime_error("Duplicate property getter");
                }
                returnType = CloneType(*propertyType);
            }
            else {
                if (setter) {
                    ReportSyntaxError(accessor, "Duplicate property setter");
                    throw std::runtime_error("Duplicate property setter");
                }
                returnType = std::make_unique<PrimitiveTypeExpr>("void");
                parameters.push_back(std::make_unique<VarDeclExpr>(
                    CloneType(*propertyType), std::make_unique<IdentifierExpr>("value"), nullptr));
            }
            auto method = std::make_unique<FunctionDeclStmt>(
                std::move(returnType), std::make_unique<Token>(methodToken),
                std::move(parameters), std::move(body));
            method->propertyAccessor = isGetter
                ? PropertyAccessorKind::Getter : PropertyAccessorKind::Setter;
            method->propertyName = propertyToken.value;
            method->autoPropertyAccessor = automatic;
            method->modifiers = AccessorModifiers(propertyModifiers, declaredModifiers);
            method->attributes = accessorAttributes;
            if (isGetter) getter = std::move(method);
            else setter = std::move(method);
        }
        Consume(TokenType::BRACKET, "}");
        if (!getter && !setter) {
            ReportSyntaxError(&propertyToken, "Property requires at least one accessor");
            throw std::runtime_error("Empty property declaration");
        }
        auto property = std::make_unique<PropertyDeclStmt>(
            std::move(propertyType), propertyToken.value, std::move(getter), std::move(setter));
        property->modifiers = propertyModifiers;
        property->attributes = propertyAttributes;
        return property;
    }
}
