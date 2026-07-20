#pragma once

namespace Absolute {
    struct ArrayExpr : Expression {
        std::vector<std::unique_ptr<Expression>> sizes;
        std::vector<std::unique_ptr<Expression>> values;

        explicit ArrayExpr(std::vector<std::unique_ptr<Expression>> sizes,
            std::vector<std::unique_ptr<Expression>> values)
            : sizes(std::move(sizes)), values(std::move(values)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Array:";

            if (!sizes.empty()) {
                result += "\n" + std::string(indent + 1, ' ') + "Sizes:";
                for (const auto& size : sizes) {
                    result += "\n" + size->ToString(indent + 2);
                }
            }

            if (!values.empty()) {
                result += "\n" + std::string(indent + 1, ' ') + "Values:";
                for (const auto& value : values) {
                    result += "\n" + value->ToString(indent + 2);
                }
            }

            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) + "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct ArrayAccessExpr : Expression {
        std::unique_ptr<Expression> base;
        std::vector<std::unique_ptr<Expression>> indexes;

        ArrayAccessExpr(std::unique_ptr<Expression> base, std::vector<std::unique_ptr<Expression>> indexes)
            : base(std::move(base)), indexes(std::move(indexes)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Array Access";

            if (!indexes.empty()) {
                result += " [";
                for (size_t i = 0; i < indexes.size(); i++) {
                    if (i > 0) result += ", ";
                    result += indexes[i] ? indexes[i]->ToString(0) : "*"; // `*` вместо `NaN`
                }
                result += "]";
            }

            result += ":\n" + base->ToString(indent + 1); // Отступ для `base`
            return result;
        }

        IdentifierExpr* GetIdentifier();

        void print(int indent = 0) override {
            std::cout << ToString(indent) + "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct SliceExpr : Expression {
        std::unique_ptr<Expression> base;
        std::unique_ptr<Expression> begin;
        std::unique_ptr<Expression> end;

        SliceExpr(std::unique_ptr<Expression> base,
            std::unique_ptr<Expression> begin,
            std::unique_ptr<Expression> end)
            : base(std::move(base)), begin(std::move(begin)), end(std::move(end)) {}

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Slice [";
            result += begin ? begin->ToString(0) : std::string{};
            result += ":";
            result += end ? end->ToString(0) : std::string{};
            result += "]:\n";
            result += base ? base->ToString(indent + 1) : std::string(indent + 1, ' ') + "<missing>";
            return result;
        }

        void print(int indent = 0) override { std::cout << ToString(indent) << "\n"; }
        void Accept(ExpressionVisitor& visitor) override;
    };
}
