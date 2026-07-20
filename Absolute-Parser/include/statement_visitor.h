#pragma once

#include "syntax_tree.h"

namespace Absolute {
    class StatementVisitor {
    public:
        virtual ~StatementVisitor() = default;

        virtual void Visit(SingleStatement* stmt) = 0;
        virtual void Visit(CompoundStmt* stmt) = 0;
        virtual void Visit(FunctionCallStmt* stmt) = 0;
        virtual void Visit(FunctionDeclStmt* stmt) = 0;
        virtual void Visit(ReturnStmt* stmt) = 0;
        virtual void Visit(AssignmentStmt* stmt) = 0;
        virtual void Visit(VarDeclStmt* stmt) = 0;
        virtual void Visit(StructDeclStmt* stmt) = 0;
        virtual void Visit(ClassDeclStmt* stmt) = 0;
        virtual void Visit(InterfaceDeclStmt* stmt) = 0;
        virtual void Visit(ConstructorDeclStmt* stmt) = 0;
        virtual void Visit(EnumDeclStmt* stmt) = 0;
        virtual void Visit(GroupDeclStmt* stmt) = 0;
        virtual void Visit(IfStmt* stmt) = 0;
        virtual void Visit(SwitchStmt* stmt) = 0;
        virtual void Visit(ThrowStmt* stmt) = 0;
        virtual void Visit(TryStmt* stmt) = 0;
        virtual void Visit(DeferStmt* stmt) = 0;
        virtual void Visit(ForStmt* stmt) = 0;
        virtual void Visit(WhileStmt* stmt) = 0;
        virtual void Visit(DoWhileStmt* stmt) = 0;
        virtual void Visit(ForEachStmt* stmt) = 0;
        virtual void Visit(ContinueStmt* stmt) = 0;
        virtual void Visit(BreakStmt* stmt) = 0;
        virtual void Visit(TypeAliasStmt* stmt) = 0;
        virtual void Visit(ImportStmt* stmt) = 0;
        virtual void Visit(NamespaceDeclStmt* stmt) = 0;
        virtual void Visit(OpaquePluginStmt* stmt) = 0;
    };
}
