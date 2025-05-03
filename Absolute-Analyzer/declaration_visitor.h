#pragma once
#include "expression_visitor.h"

namespace Absolute {
	class DeclarationVisitor : public ExpressionVisitor {
	public:
		void Visit(VarDeclExpr* expr) override {
			expr->type->Accept(*this);
			expr->name = expr->name;
			expr->value->Accept(*this);
		}
	};
}

