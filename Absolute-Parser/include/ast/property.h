#pragma once

namespace Absolute {
    inline std::string PropertyGetterName(const std::string& propertyName) {
        return "__absolute_property_get_" + propertyName;
    }

    inline std::string PropertySetterName(const std::string& propertyName) {
        return "__absolute_property_set_" + propertyName;
    }

    inline std::string PropertyBackingFieldName(const std::string& propertyName) {
        return "__absolute_property_storage_" + propertyName;
    }

    struct PropertyDeclStmt : Statement {
        std::unique_ptr<TypeExpr> type;
        std::string name;
        std::unique_ptr<FunctionDeclStmt> getter;
        std::unique_ptr<FunctionDeclStmt> setter;

        PropertyDeclStmt(std::unique_ptr<TypeExpr> propertyType, std::string propertyName,
            std::unique_ptr<FunctionDeclStmt> propertyGetter,
            std::unique_ptr<FunctionDeclStmt> propertySetter)
            : type(std::move(propertyType)), name(std::move(propertyName)),
              getter(std::move(propertyGetter)), setter(std::move(propertySetter)) {
        }

        bool HasAutoAccessor() const {
            return (getter && getter->autoPropertyAccessor) ||
                (setter && setter->autoPropertyAccessor);
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Property declaration: " << name << "\n";
            if (type) type->print(indent + 1);
            if (getter) getter->print(indent + 1);
            if (setter) setter->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
