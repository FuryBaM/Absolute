#include "analyzer_internal.h"

namespace Absolute {
    void Analyzer::Visit(BinaryExpr* expr) {
        const Result left = Evaluate(expr->left.get());
        const Result right = Evaluate(expr->right.get());
        const std::string& op = expr->op;
        if (const PluginBinaryOperator* pluginOperator =
                FindPluginBinaryOperator(left.type, op, right.type)) {
            const std::vector<std::string> expectedParameters{left.type, right.type};
            const Symbol* function = FindFunctionSymbol(pluginOperator->functionName, expectedParameters);
            const SymbolId functionId = function ? function->id : InvalidSymbolId;
            if (!function || function->kind != SymbolKind::Function ||
                function->parameterTypes != expectedParameters || function->type != pluginOperator->resultType) {
                Report("plugin operator '" + left.type + " " + op + " " + right.type +
                    "' requires function '" + pluginOperator->functionName + "(" + left.type + ", " +
                    right.type + ") -> " + pluginOperator->resultType + "'", "E_PLUGIN_OPERATOR_SIGNATURE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            const bool managedResult = IsManagedPointerType(pluginOperator->resultType);
            Save(expr, {functionId, pluginOperator->resultType, false, managedResult, false,
                InitializationState::Initialized,
                managedResult ? PointerValidity::Live : PointerValidity::NotPointer});
            return;
        }
        const bool leftPointer = IsPointerType(left.type);
        const bool rightPointer = IsPointerType(right.type);
        if ((leftPointer || rightPointer) && (op == "+" || op == "-")) {
            if (IsManagedPointerType(left.type) || IsManagedPointerType(right.type)) {
                Report("managed pointers do not support arithmetic; use raw T* when address arithmetic is required");
                Save(expr, {InvalidSymbolId, "error", false});
            }
            else if (op == "+" && IsRawPointerType(left.type) && IsInteger(right.type))
                Save(expr, {InvalidSymbolId, left.type, false, false, false,
                    left.initialization, left.pointerValidity, left.pointerOwner});
            else if (op == "+" && IsInteger(left.type) && IsRawPointerType(right.type))
                Save(expr, {InvalidSymbolId, right.type, false, false, false,
                    right.initialization, right.pointerValidity, right.pointerOwner});
            else if (op == "-" && IsRawPointerType(left.type) && IsInteger(right.type))
                Save(expr, {InvalidSymbolId, left.type, false, false, false,
                    left.initialization, left.pointerValidity, left.pointerOwner});
            else if (op == "-" && IsRawPointerType(left.type) && left.type == right.type)
                Save(expr, {InvalidSymbolId, "int64", false});
            else {
                Report("invalid pointer arithmetic between '" + left.type + "' and '" + right.type + "'");
                Save(expr, {InvalidSymbolId, "error", false});
            }
        }
        else if (op == "&&" || op == "||") {
            if (!IsConditionType(left.type) || !IsConditionType(right.type)) Report("logical operands must be boolean-compatible");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (op != "==" && op != "!=" &&
                (IsManagedPointerType(left.type) || IsManagedPointerType(right.type)))
                Report("managed pointers only support equality and null comparisons");
            if (!IsAssignable(left.type, right.type) && !IsAssignable(right.type, left.type))
                Report("cannot compare '" + left.type + "' with '" + right.type + "'");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            if (!IsInteger(left.type) || !IsInteger(right.type)) Report("bitwise operands must be integers");
            Save(expr, {InvalidSymbolId, CommonType(left.type, right.type), false});
        }
        else {
            if (!IsNumeric(left.type) || !IsNumeric(right.type))
                Report("operator '" + op + "' requires numeric operands");
            Save(expr, {InvalidSymbolId, CommonType(left.type, right.type), false});
        }
    }

    void Analyzer::Visit(TernaryExpr* expr) {
        const Result condition = Evaluate(expr->condition.get());
        if (!IsConditionType(condition.type)) Report("ternary condition must be boolean-compatible");
        const ValueFlowMap baseValues = valueFlow;
        const Result trueResult = Evaluate(expr->trueExpr.get());
        const ValueFlowMap trueValues = valueFlow;
        valueFlow = baseValues;
        const Result falseResult = Evaluate(expr->falseExpr.get());
        const ValueFlowMap falseValues = valueFlow;
        MergeValueFlowPaths(baseValues, {trueValues, falseValues});
        std::string type = CommonType(trueResult.type, falseResult.type);
        if (type == "error" && IsPointerType(trueResult.type) && falseResult.type == "null") type = trueResult.type;
        if (type == "error" && IsPointerType(falseResult.type) && trueResult.type == "null") type = falseResult.type;
        if (type == "error" && trueResult.type != "error" && falseResult.type != "error")
            Report("ternary branches have incompatible types '" + trueResult.type + "' and '" + falseResult.type + "'");
        PointerValidity pointerValidity = PointerValidity::NotPointer;
        if (IsPointerType(type)) {
            if (trueResult.pointerValidity == falseResult.pointerValidity)
                pointerValidity = trueResult.pointerValidity;
            else if ((trueResult.pointerValidity == PointerValidity::Null &&
                falseResult.pointerValidity == PointerValidity::Live) ||
                (falseResult.pointerValidity == PointerValidity::Null &&
                    trueResult.pointerValidity == PointerValidity::Live))
                pointerValidity = PointerValidity::MaybeNull;
            else pointerValidity = PointerValidity::MaybeInvalid;
        }
        SymbolId pointerOwner = trueResult.pointerOwner == falseResult.pointerOwner
            ? trueResult.pointerOwner : InvalidSymbolId;
        if (trueResult.pointerValidity == PointerValidity::Null) pointerOwner = falseResult.pointerOwner;
        if (falseResult.pointerValidity == PointerValidity::Null) pointerOwner = trueResult.pointerOwner;
        Result combined{InvalidSymbolId, type, false,
            trueResult.createsManagedOwner && falseResult.createsManagedOwner,
            trueResult.referencesManagedOwner && falseResult.referencesManagedOwner,
            InitializationState::Initialized, pointerValidity, pointerOwner};
        combined.createsRawOwner = trueResult.createsRawOwner && falseResult.createsRawOwner;
        Save(expr, std::move(combined));
    }

    void Analyzer::Visit(NullExpr* expr) {
        Save(expr, {InvalidSymbolId, "null", false, false, false,
            InitializationState::Initialized, PointerValidity::Null, InvalidSymbolId});
    }
    void Analyzer::Visit(BooleanLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "bool", false}); }
    void Analyzer::Visit(NumberLiteralExpr* expr) {
        Save(expr, {InvalidSymbolId, expr->value.find('.') == std::string::npos ? "int32" : "double", false});
    }
    void Analyzer::Visit(StringLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "string", false}); }
    void Analyzer::Visit(CharLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "char", false}); }

    void Analyzer::Visit(ArrayExpr* expr) {
        for (const auto& size : expr->sizes) {
            if (!size) continue;
            const Result resolved = Evaluate(size.get());
            if (!IsInteger(resolved.type)) Report("array size must be an integer");
        }
        const bool hasExpectedArrayType = expectedType.ends_with("[]");
        const std::string expectedElementType = hasExpectedArrayType
            ? ArrayElementType(expectedType) : std::string{};
        std::string elementType;
        for (const auto& value : expr->values) {
            const Result resolved = hasExpectedArrayType
                ? EvaluateExpected(value.get(), expectedElementType)
                : Evaluate(value.get());
            if (hasExpectedArrayType && !IsAssignable(expectedElementType, resolved.type))
                Report("array element has type '" + resolved.type + "', expected '" + expectedElementType + "'");
            const std::string common = elementType.empty()
                ? resolved.type : CommonType(elementType, resolved.type);
            if (!elementType.empty() && common == "error" &&
                elementType != "error" && resolved.type != "error")
                Report("array literal mixes incompatible element types '" + elementType +
                    "' and '" + resolved.type + "'");
            elementType = common;
        }
        if (hasExpectedArrayType) Save(expr, {InvalidSymbolId, expectedType, false});
        else {
            if (elementType.empty()) elementType = "dynamic";
            Save(expr, {InvalidSymbolId, elementType + "[]", false});
        }
    }

}
