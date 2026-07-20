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

        if (name == "move") {
            if (expression.arguments.size() != 1) Fail("move expects exactly one argument");
            Expression* argument = expression.arguments.front().get();
            llvm::Value* address = EvaluateAddress(argument);
            llvm::Type* type = TypeFromName(SemanticType(argument));
            value = builder.CreateLoad(type, address, "move.value");
            valueCreatesManagedOwner = IsManagedPointerTypeName(SemanticType(argument));
            if (ArrayRankName(SemanticType(argument)) > 0) {
                valueCreatesArrayOwner = true;
                valueArrayOwner = builder.CreateExtractValue(value, {1}, "move.array.owner");
            }
            uint64_t size = SizeOfTypeName(SemanticType(argument));
            if (size > 0) {
                builder.CreateMemSet(address, builder.getInt8(0), size, llvm::MaybeAlign(8));
            }
            return;
        }

        if (name == "copy") {
            if (expression.arguments.size() != 1)
                Fail("copy expects exactly one array or slice argument");
            Expression* argument = expression.arguments.front().get();
            const std::string typeName = SemanticType(argument);
            const size_t rank = ArrayRankName(typeName);
            if (rank == 0) Fail("copy expects an array or slice");

            ArrayView source = ViewOfArray(argument);
            const bool releaseTemporarySource = valueCreatesArrayOwner;
            llvm::Value* temporarySourceOwner = valueArrayOwner;
            llvm::Value* elementCount = builder.getInt64(1);
            for (llvm::Value* dimension : source.dimensions)
                elementCount = builder.CreateMul(elementCount, dimension, "copy.element.count");
            llvm::Value* byteCount = builder.CreateMul(elementCount,
                builder.getInt64(SizeOfTypeName(ArrayElementTypeName(typeName, rank))),
                "copy.byte.count");
            llvm::Value* copiedData = builder.CreateCall(Malloc(), {byteCount}, "copy.data");
            builder.CreateMemCpy(copiedData, llvm::MaybeAlign(16),
                source.address, llvm::MaybeAlign(1), byteCount);
            if (releaseTemporarySource)
                builder.CreateCall(Free(), {temporarySourceOwner});
            source.address = copiedData;
            source.owner = copiedData;
            value = BuildArrayDescriptor(source);
            valueCreatesManagedOwner = false;
            valueManagedPointee = nullptr;
            valueCreatesArrayOwner = true;
            valueArrayOwner = copiedData;
            return;
        }

        Fail("unknown builtin function '" + name + "'");
    }

}
