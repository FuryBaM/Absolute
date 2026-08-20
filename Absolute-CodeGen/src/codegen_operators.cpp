#include "codegen_internal.h"

namespace Absolute {
    void CodeGenerator::Visit(BinaryExpr* expr) {
        const std::string leftType = impl->SemanticType(expr->left.get());
        const std::string rightType = impl->SemanticType(expr->right.get());
        llvm::Value* left = impl->Evaluate(expr->left.get());
        const bool leftCreatesManagedOwner = impl->valueCreatesManagedOwner;

        if (expr->op == "&&" || expr->op == "||") {
            llvm::Function* function = impl->CurrentFunction();
            if (!function) impl->Fail("logical expression outside a function");

            // The left operand always runs, so it can wait for the end of the
            // statement; the right one cannot, and is handled below.
            if (leftCreatesManagedOwner) impl->RegisterTemporaryOwner(left, leftType);
            impl->RegisterIfFreshString(expr->left.get(), left);
            left = impl->AsCondition(left);
            llvm::BasicBlock* leftBlock = impl->builder.GetInsertBlock();
            llvm::BasicBlock* rightBlock =
                llvm::BasicBlock::Create(impl->context, "logical.rhs", function);
            llvm::BasicBlock* mergeBlock =
                llvm::BasicBlock::Create(impl->context, "logical.end", function);
            if (expr->op == "&&")
                impl->builder.CreateCondBr(left, rightBlock, mergeBlock);
            else
                impl->builder.CreateCondBr(left, mergeBlock, rightBlock);

            impl->builder.SetInsertPoint(rightBlock);
            llvm::Value* right = nullptr;
            {
                // The right operand only runs on one path, so an owner it
                // produces has to be released on that path: a destroy after the
                // merge would name a value that does not reach it.
                Impl::TemporaryOwnerScope shortCircuit(*impl);
                right = impl->AsCondition(impl->EvaluateBorrowed(expr->right.get()));
            }
            rightBlock = impl->builder.GetInsertBlock();
            impl->BranchIfNeeded(mergeBlock);

            impl->builder.SetInsertPoint(mergeBlock);
            llvm::PHINode* result =
                impl->builder.CreatePHI(impl->builder.getInt1Ty(), 2, "logical.result");
            result->addIncoming(
                expr->op == "&&" ? impl->builder.getFalse() : impl->builder.getTrue(),
                leftBlock);
            result->addIncoming(right, rightBlock);
            impl->value = result;
            impl->valueCreatesManagedOwner = false;
            impl->valueManagedPointee = nullptr;
            return;
        }

        llvm::Value* right = impl->Evaluate(expr->right.get());
        const bool rightCreatesManagedOwner = impl->valueCreatesManagedOwner;

        if (const PluginBinaryOperator* pluginOperator =
                FindPluginBinaryOperator(leftType, expr->op, rightType)) {
            llvm::Function* function = impl->module->getFunction(impl->ResolvedName(expr));
            if (!function)
                impl->Fail("missing plugin operator function '" + pluginOperator->functionName + "'");
            const std::vector<std::string> parameterTypes{leftType, rightType};
            impl->value = impl->EmitAbiCall(
                function->getFunctionType(), function,
                pluginOperator->resultType, {}, parameterTypes,
                {left, right}, "plugin.operator");
            impl->EmitExceptionCheck();
            // Plugin operators borrow their operands. Destroy temporary managed
            // owners after the call, while named variables remain owned by their
            // enclosing scope. This makes chained expressions such as
            // translation(...) * rotation(...) leak-free.
            llvm::Value* result = impl->value;
            if (leftCreatesManagedOwner)
                impl->builder.CreateCall(impl->ManagedDestroy(), {left});
            if (rightCreatesManagedOwner)
                impl->builder.CreateCall(impl->ManagedDestroy(), {right});
            impl->value = result;
            impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(pluginOperator->resultType);
            impl->valueManagedPointee = nullptr;
            return;
        }

        // Everything from here borrows its operands: a comparison, a pointer
        // difference, arithmetic. None of them takes the owner, so an operand
        // that produced one -- `make() != null` -- waits for the end of the
        // statement instead of being dropped. The plugin path above is
        // unchanged: it destroys its operands as soon as the call returns.
        if (leftCreatesManagedOwner) impl->RegisterTemporaryOwner(left, leftType);
        if (rightCreatesManagedOwner) impl->RegisterTemporaryOwner(right, rightType);
        // The same for a string an operand made: `made == build(n)` compares
        // the bytes and keeps neither side, so the one that was allocated for
        // the comparison is released with the statement.
        impl->RegisterIfFreshString(expr->left.get(), left);
        impl->RegisterIfFreshString(expr->right.get(), right);

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
        impl->value = impl->ApplyBinary(expr->op, left, right, leftType, rightType);
    }

    void CodeGenerator::Visit(TernaryExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("ternary expression outside a function");

        llvm::Value* condition = impl->AsCondition(impl->Evaluate(expr->condition.get()));
        llvm::BasicBlock* trueBlock = llvm::BasicBlock::Create(impl->context, "ternary.true", function);
        llvm::BasicBlock* falseBlock = llvm::BasicBlock::Create(impl->context, "ternary.false", function);
        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(impl->context, "ternary.end", function);
        impl->builder.CreateCondBr(condition, trueBlock, falseBlock);

        const std::string resultTypeName = impl->SemanticType(expr);

        // Each arm runs on its own path, so an owner borrowed inside one is
        // released inside it. The arm's own result is not touched: it may be
        // the owner the ternary yields. What it is made to do is hold a count
        // of its own, so that both paths hand the merge the same thing.
        impl->builder.SetInsertPoint(trueBlock);
        llvm::Value* trueValue = nullptr;
        {
            Impl::TemporaryOwnerScope arm(*impl);
            trueValue = impl->CountedArmValue(expr->trueExpr.get(),
                impl->Evaluate(expr->trueExpr.get()), resultTypeName);
        }
        trueBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(mergeBlock);

        impl->builder.SetInsertPoint(falseBlock);
        llvm::Value* falseValue = nullptr;
        {
            Impl::TemporaryOwnerScope arm(*impl);
            falseValue = impl->CountedArmValue(expr->falseExpr.get(),
                impl->Evaluate(expr->falseExpr.get()), resultTypeName);
        }
        falseBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(mergeBlock);

        const std::string trueType = impl->SemanticType(expr->trueExpr.get());
        const std::string falseType = impl->SemanticType(expr->falseExpr.get());
        // The type of the merge is the type of the expression, which the
        // analyzer has already worked out from both arms. Asking for a common
        // *numeric* type instead refused everything that is not a number or a
        // handle: `cond ? "a" : "b"` and every struct and tuple failed in the
        // backend, with a message about binary operators.
        llvm::Type* resultType = resultTypeName.empty()
            ? impl->CommonNumericType(trueValue->getType(), falseValue->getType())
            : impl->TypeFromName(resultTypeName);
        impl->builder.SetInsertPoint(trueBlock->getTerminator());
        trueValue = impl->Coerce(trueValue, resultType, trueType, resultTypeName);
        impl->builder.SetInsertPoint(falseBlock->getTerminator());
        falseValue = impl->Coerce(falseValue, resultType, falseType, resultTypeName);
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
        if (IsFloatingLiteral(expr->value)) {
            impl->value = llvm::ConstantFP::get(impl->builder.getDoubleTy(), impl->ParseFloatingLiteral(expr->value));
        }
        else {
            // The value was parsed as 64-bit and then truncated into an i32
            // constant, so any literal wider than 32 bits was silently wrong:
            // 2^31 came out negative, 2^32 came out zero, 10^10 came out
            // 1410065408. Widen only when the value does not fit, so every
            // literal that worked before keeps exactly the type it had.
            // Parsed unsigned, because the whole 64-bit range has to be
            // reachable and a signed parse cannot get there. The literal is
            // always positive here -- a minus sign is a separate unary
            // expression -- so int64's minimum arrives as 2^63 and negating its
            // bit pattern yields the minimum again, and uint64's maximum has no
            // signed spelling at all. A signed parse threw out_of_range on both
            // and the exception reached the user as a bare "Error: stoll".
            unsigned long long parsed = 0;
            try {
                parsed = std::stoull(expr->value);
            }
            catch (const std::exception&) {
                impl->Fail("integer literal '" + expr->value +
                    "' does not fit in 64 bits");
            }
            const bool fitsIn32 =
                parsed <= static_cast<unsigned long long>(
                    std::numeric_limits<int32_t>::max());
            impl->value = llvm::ConstantInt::get(
                fitsIn32 ? impl->builder.getInt32Ty() : impl->builder.getInt64Ty(),
                parsed, false);
        }
    }

    void CodeGenerator::Visit(StringLiteralExpr* expr) {
        impl->value = impl->EmitStringConstant(expr->value, "string.literal");
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
        const auto shape = InferArrayStorageShape(*expr, rank);
        if (!shape) impl->Fail("array literal must be rectangular");
        if (shape->size() != rank)
            impl->Fail("array literal rank does not match its semantic type");

        const std::string elementTypeName = ArrayElementTypeName(typeName, rank);
        llvm::Type* elementType = impl->TypeFromName(elementTypeName);
        std::vector<Expression*> values;
        FlattenArrayValues(*expr, values);

        llvm::Value* byteCount = impl->builder.getInt64(
            static_cast<std::uint64_t>(values.size()) * impl->SizeOfTypeName(elementTypeName));

        // A literal is an owner. The owner pointer is what says an array owns
        // anything: the drop is guarded by it, and `move` clears it to hand
        // the elements on. A stack literal has no owner pointer, so its
        // elements were never released -- and giving the drop a different
        // guard instead makes a moved-from array release what the destination
        // now holds.
        //
        // It was allocated only when its elements owned something, which left
        // the analyzer and the backend disagreeing about what a literal is:
        // the backend handed out an owner for `Cell*[]` and a pointer into the
        // frame for `int32[]`, so `move` worked on one and not the other for a
        // reason nobody could read off the source. Storage that belongs to the
        // frame is now written as such -- `int32[3] a = { ... }` is the
        // declaration that says the frame holds it -- and every literal that
        // is an expression makes storage of its own.
        llvm::Value* address = impl->builder.CreateCall(
            impl->Malloc(), {byteCount}, "array.literal.allocation");
        llvm::Value* owner = address;
        impl->builder.CreateMemSet(address, impl->builder.getInt8(0),
            byteCount, llvm::MaybeAlign(16));

        for (size_t index = 0; index < values.size(); ++index) {
            llvm::Value* initial = impl->Coerce(
                impl->Evaluate(values[index]), elementType,
                impl->SemanticType(values[index]), elementTypeName);
            llvm::Value* destination = impl->builder.CreateInBoundsGEP(
                elementType, address, impl->builder.getInt64(index), "array.literal.element");
            impl->builder.CreateStore(initial, destination);
        }

        std::vector<llvm::Value*> dimensions;
        dimensions.reserve(shape->size());
        for (size_t size : *shape) dimensions.push_back(impl->builder.getInt64(size));
        impl->value = impl->BuildArrayDescriptor(
            {address, elementType, typeName, std::move(dimensions), owner});
        impl->valueCreatesArrayOwner = true;
        // Named as well as reported: whoever takes the literal over -- a
        // declaration, an argument, `copy` -- frees it through this pointer,
        // and while a literal was sometimes the frame's storage nobody ever
        // asked for it.
        impl->valueArrayOwner = owner;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
    }

}
