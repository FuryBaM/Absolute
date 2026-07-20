#pragma once

namespace Absolute {
    inline const std::string& IndexerMemberName() {
        static const std::string name = "__absolute_indexer";
        return name;
    }

    inline const std::string& IndexerGetterName() {
        static const std::string name = "__absolute_indexer_get";
        return name;
    }

    inline const std::string& IndexerSetterName() {
        static const std::string name = "__absolute_indexer_set";
        return name;
    }

    struct IndexerDeclStmt : Statement {
        std::unique_ptr<TypeExpr> type;
        std::vector<std::unique_ptr<VarDeclExpr>> parameters;
        std::unique_ptr<FunctionDeclStmt> getter;
        std::unique_ptr<FunctionDeclStmt> setter;

        IndexerDeclStmt(std::unique_ptr<TypeExpr> elementType,
            std::vector<std::unique_ptr<VarDeclExpr>> indexParameters,
            std::unique_ptr<FunctionDeclStmt> indexGetter,
            std::unique_ptr<FunctionDeclStmt> indexSetter)
            : type(std::move(elementType)), parameters(std::move(indexParameters)),
              getter(std::move(indexGetter)), setter(std::move(indexSetter)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Indexer declaration\n";
            if (type) type->print(indent + 1);
            for (const auto& parameter : parameters)
                if (parameter) parameter->print(indent + 1);
            if (getter) getter->print(indent + 1);
            if (setter) setter->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
