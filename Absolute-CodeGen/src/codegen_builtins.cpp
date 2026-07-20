#include "codegen_internal.h"

namespace Absolute {
    void CodeGenerator::Impl::EmitBuiltin(FunctionCallExpr& expression, const std::string& name) {
        if (name == "print" || name == "println") {
            std::vector<PrintableValue> values;
            std::string format;
            for (const auto& argument : expression.arguments) {
                PrintableValue printable = PreparePrintable(Evaluate(argument.get()), argument.get());
                format += printable.specifier;
                values.push_back(std::move(printable));
            }
            if (name == "println") format += '\n';
            EmitPrintf(format, values);
            value = nullptr;
            return;
        }

        if (name == "toString") {
            if (expression.arguments.size() != 1) Fail("toString expects exactly one argument");
            PrintableValue printable = PreparePrintable(
                Evaluate(expression.arguments.front().get()), expression.arguments.front().get());
            value = EmitFormat(printable.specifier, {printable});
            return;
        }

        if (name == "format") {
            if (expression.arguments.empty()) Fail("format expects a string literal template");
            StringLiteralProbe probe;
            expression.arguments.front()->Accept(probe);
            if (!probe.literal) Fail("format template must be a string literal");
            std::vector<PrintableValue> values;
            for (size_t index = 1; index < expression.arguments.size(); ++index)
                values.push_back(PreparePrintable(
                    Evaluate(expression.arguments[index].get()), expression.arguments[index].get()));
            value = EmitFormat(BuildFormat(probe.literal->value, values), values);
            return;
        }

        if (name == "assert") {
            if (expression.arguments.empty() || expression.arguments.size() > 2)
                Fail("assert expects a condition and an optional message");
            llvm::Function* function = CurrentFunction();
            if (!function) Fail("assert outside a function");
            llvm::Value* condition = AsCondition(Evaluate(expression.arguments.front().get()));
            llvm::BasicBlock* success = llvm::BasicBlock::Create(context, "assert.success", function);
            llvm::BasicBlock* failure = llvm::BasicBlock::Create(context, "assert.failure", function);
            builder.CreateCondBr(condition, success, failure);
            builder.SetInsertPoint(failure);
            std::vector<PrintableValue> values;
            std::string format = "Assertion failed";
            if (expression.arguments.size() == 2) {
                format += ": %s";
                values.push_back(PreparePrintable(
                    Evaluate(expression.arguments[1].get()), expression.arguments[1].get()));
            }
            format += '\n';
            EmitPrintf(format, values);
            builder.CreateCall(Abort());
            builder.CreateUnreachable();
            builder.SetInsertPoint(success);
            value = nullptr;
            return;
        }

        Fail("unknown builtin function '" + name + "'");
    }

}
