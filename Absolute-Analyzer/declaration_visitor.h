#pragma once
#include "expression_visitor.h"

namespace Absolute {
	class DeclarationVisitor : public BaseIdentifierVisitor {
	public:
		void Visit(VarDeclExpr* expr) override {
			if (expr->type) expr->type->Accept(*this);
			if (expr->name) expr->name->Accept(*this);
			if (expr->value) expr->value->Accept(*this);
		}
	};
}
