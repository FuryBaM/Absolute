#include "codegen_internal.h"

namespace Absolute {
    void CodeGenerator::Visit(BinaryExpr* expr) {
        const std::string leftType = impl->SemanticType(expr->left.get());
        const std::string rightType = impl->SemanticType(expr->right.get());
        llvm::Value* left = impl->Evaluate(expr->left.get());
        llvm::Value* right = impl->Evaluate(expr->right.get());

        if (const PluginBinaryOperator* pluginOperator =
                FindPluginBinaryOperator(leftType, expr->op, rightType)) {
            llvm::Function* function = impl->module->getFunction(impl->ResolvedName(expr));
            if (!function || function->arg_size() != 2)
                impl->Fail("missing plugin operator function '" + pluginOperator->functionName + "'");
            left = impl->Coerce(left, function->getFunctionType()->getParamType(0));
            right = impl->Coerce(right, function->getFunctionType()->getParamType(1));
            impl->value = impl->builder.CreateCall(function, {left, right}, "plugin.operator");
            impl->EmitExceptionCheck();
            impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(pluginOperator->resultType);
            impl->valueManagedPointee = nullptr;
            return;
        }

        const bool leftRaw = IsRawPointerTypeName(leftType);
        const bool rightRaw = IsRawPointerTypeName(rightType);
        const bool leftManaged = IsManagedPointerTypeName(leftType);
        const bool rightManaged = IsManagedPointerTypeName(rightType);
        std::string cfuncReturn;
        std::vector<std::string> cfuncParameters;
        const bool leftCFunction = ParseCodegenCFunctionType(leftType, cfuncReturn, cfuncParameters);
        const bool rightCFunction = ParseCodegenCFunctionType(rightType, cfuncReturn, cfuncParameters);
        const bool equality = expr->op == "==" || expr->op == "!=";

        if (equality && (leftCFunction || rightCFunction) &&
            (leftCFunction || leftType == "null") &&
            (rightCFunction || rightType == "null")) {
            llvm::Value* leftPtr = impl->Coerce(left, impl->builder.getPtrTy());
            llvm::Value* rightPtr = impl->Coerce(right, impl->builder.getPtrTy());
            impl->value = expr->op == "=="
                ? impl->builder.CreateICmpEQ(leftPtr, rightPtr, "cfunc.equal")
                : impl->builder.CreateICmpNE(leftPtr, rightPtr, "cfunc.not.equal");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if (equality && leftType == "string" && rightType == "string") {
            llvm::FunctionType* compareType = llvm::FunctionType::get(
                impl->builder.getInt32Ty(),
                {impl->builder.getPtrTy(), impl->builder.getPtrTy()}, false);
            llvm::Value* comparison = impl->builder.CreateCall(
                impl->module->getOrInsertFunction("strcmp", compareType),
                {left, right}, "string.compare");
            llvm::Value* equal = impl->builder.CreateICmpEQ(
                comparison, impl->builder.getInt32(0), "string.equal");
            impl->value = expr->op == "=="
                ? equal : impl->builder.CreateNot(equal, "string.not.equal");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if ((leftRaw || rightRaw) && (expr->op == "+" || expr->op == "-")) {
            if (leftRaw && rightRaw && expr->op == "-") {
                llvm::Value* leftAddress = impl->builder.CreatePtrToInt(left, impl->builder.getInt64Ty());
                llvm::Value* rightAddress = impl->builder.CreatePtrToInt(right, impl->builder.getInt64Ty());
                llvm::Value* bytes = impl->builder.CreateSub(leftAddress, rightAddress, "pointer.byte.diff");
                impl->value = impl->builder.CreateSDiv(bytes,
                    impl->builder.getInt64(impl->SizeOfTypeName(PointerPointeeName(leftType))),
                    "pointer.diff");
                impl->valueCreatesManagedOwner = false;
                return;
            }
            const bool pointerOnLeft = leftRaw;
            llvm::Value* pointer = pointerOnLeft ? left : right;
            llvm::Value* index = pointerOnLeft ? right : left;
            index = impl->Coerce(index, impl->builder.getInt64Ty());
            if (expr->op == "-" && pointerOnLeft)
                index = impl->builder.CreateNeg(index, "pointer.negative.offset");
            const std::string pointerType = pointerOnLeft ? leftType : rightType;
            impl->value = impl->builder.CreateGEP(
                impl->TypeFromName(PointerPointeeName(pointerType)), pointer, index, "pointer.offset");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if (equality && ((leftManaged && rightType == "null") || (rightManaged && leftType == "null"))) {
            llvm::Value* handle = leftManaged ? left : right;
            llvm::Value* valid = impl->builder.CreateCall(impl->ManagedValid(), {handle}, "managed.valid");
            impl->value = expr->op == "==" ? impl->builder.CreateNot(valid, "managed.is.null") : valid;
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if ((leftRaw || rightRaw) &&
            (equality || expr->op == "<" || expr->op == "<=" || expr->op == ">" || expr->op == ">=")) {
            if (expr->op == "==") impl->value = impl->builder.CreateICmpEQ(left, right, "pointer.equal");
            else if (expr->op == "!=") impl->value = impl->builder.CreateICmpNE(left, right, "pointer.not.equal");
            else if (expr->op == "<") impl->value = impl->builder.CreateICmpULT(left, right, "pointer.less");
            else if (expr->op == "<=") impl->value = impl->builder.CreateICmpULE(left, right, "pointer.less.equal");
            else if (expr->op == ">") impl->value = impl->builder.CreateICmpUGT(left, right, "pointer.greater");
            else impl->value = impl->builder.CreateICmpUGE(left, right, "pointer.greater.equal");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if (leftManaged && rightManaged && equality) {
            impl->value = expr->op == "=="
                ? impl->builder.CreateICmpEQ(left, right, "managed.equal")
                : impl->builder.CreateICmpNE(left, right, "managed.not.equal");
            impl->valueCreatesManagedOwner = false;
            return;
        }
        impl->value = impl->ApplyBinary(expr->op, left, right);
    }

    void CodeGenerator::Visit(TernaryExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("ternary expression outside a function");

        llvm::Value* condition = impl->AsCondition(impl->Evaluate(expr->condition.get()));
        llvm::BasicBlock* trueBlock = llvm::BasicBlock::Create(impl->context, "ternary.true", function);
        llvm::BasicBlock* falseBlock = llvm::BasicBlock::Create(impl->context, "ternary.false", function);
        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(impl->context, "ternary.end", function);
        impl->builder.CreateCondBr(condition, trueBlock, falseBlock);

        impl->builder.SetInsertPoint(trueBlock);
        llvm::Value* trueValue = impl->Evaluate(expr->trueExpr.get());
        trueBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(mergeBlock);

        impl->builder.SetInsertPoint(falseBlock);
        llvm::Value* falseValue = impl->Evaluate(expr->falseExpr.get());
        falseBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(mergeBlock);

        llvm::Type* resultType = impl->CommonNumericType(trueValue->getType(), falseValue->getType());
        impl->builder.SetInsertPoint(trueBlock->getTerminator());
        trueValue = impl->Coerce(trueValue, resultType);
        impl->builder.SetInsertPoint(falseBlock->getTerminator());
        falseValue = impl->Coerce(falseValue, resultType);
        impl->builder.SetInsertPoint(mergeBlock);
        llvm::PHINode* result = impl->builder.CreatePHI(resultType, 2, "ternary.result");
        result->addIncoming(trueValue, trueBlock);
        result->addIncoming(falseValue, falseBlock);
        impl->value = result;
    }

    void CodeGenerator::Visit(NullExpr* expr) {
        (void)expr;
        impl->value = llvm::ConstantPointerNull::get(impl->builder.getPtrTy());
    }

    void CodeGenerator::Visit(BooleanLiteralExpr* expr) {
        impl->value = impl->builder.getInt1(expr->value);
    }

    void CodeGenerator::Visit(NumberLiteralExpr* expr) {
        if (expr->value.find('.') != std::string::npos) {
            impl->value = llvm::ConstantFP::get(impl->builder.getDoubleTy(), std::stod(expr->value));
        }
        else {
            impl->value = llvm::ConstantInt::get(impl->builder.getInt32Ty(), std::stoll(expr->value), true);
        }
    }

    void CodeGenerator::Visit(StringLiteralExpr* expr) {
        impl->value = impl->builder.CreateGlobalStringPtr(expr->value, "string.literal");
    }

    void CodeGenerator::Visit(CharLiteralExpr* expr) {
        impl->value = impl->builder.getInt8(static_cast<unsigned char>(expr->value));
    }

    void CodeGenerator::Visit(ArrayExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("array literal outside a function is not a runtime expression");

        const std::string typeName = impl->SemanticType(expr);
        const size_t rank = ArrayRankName(typeName);
        if (rank == 0) impl->Fail("array literal does not have an array type");
        const auto shape = InferArrayShape(*expr);
        if (!shape) impl->Fail("array literal must be rectangular");
        if (shape->size() != rank)
            impl->Fail("array literal rank does not match its semantic type");

        const std::string elementTypeName = ArrayElementTypeName(typeName, rank);
        llvm::Type* elementType = impl->TypeFromName(elementTypeName);
        std::vector<Expression*> values;
        FlattenArrayValues(*expr, values);

        llvm::Value* elementCount = impl->builder.getInt64(values.size());
        llvm::AllocaInst* address = impl->builder.CreateAlloca(
            elementType, elementCount, "array.literal.storage");
        address->setAlignment(llvm::Align(16));
        llvm::Value* byteCount = impl->builder.getInt64(
            static_cast<std::uint64_t>(values.size()) * impl->SizeOfTypeName(elementTypeName));
        impl->builder.CreateMemSet(address, impl->builder.getInt8(0),
            byteCount, llvm::MaybeAlign(16));

        for (size_t index = 0; index < values.size(); ++index) {
            llvm::Value* initial = impl->Coerce(impl->Evaluate(values[index]), elementType);
            llvm::Value* destination = impl->builder.CreateInBoundsGEP(
                elementType, address, impl->builder.getInt64(index), "array.literal.element");
            impl->builder.CreateStore(initial, destination);
        }

        std::vector<llvm::Value*> dimensions;
        dimensions.reserve(shape->size());
        for (size_t size : *shape) dimensions.push_back(impl->builder.getInt64(size));
        impl->value = impl->BuildArrayDescriptor(
            {address, elementType, typeName, std::move(dimensions)});
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
    }

}
