#pragma once

namespace Absolute {
	struct IfStmt : Statement {
		struct Branch {
			std::unique_ptr<Expression> condition;
			std::unique_ptr<Statement> body;

			Branch(std::unique_ptr<Expression> cond, std::unique_ptr<Statement> stmt)
				: condition(std::move(cond)), body(std::move(stmt)) {
			}
		};

		std::vector<Branch> branches; // if + else if
		std::unique_ptr<Statement> elseBranch; // else (если есть)

		IfStmt(std::vector<Branch> branches, std::unique_ptr<Statement> elseBranch)
			: branches(std::move(branches)), elseBranch(std::move(elseBranch)) {
		}
		void print(int indent = 0) override {
			std::cout << std::string(indent, ' ') << "If statement: " << "\n";
			for (const auto& branch : branches) {
				std::cout << std::string(indent + 1, ' ') << "Branch: " << "\n";
				branch.condition->print(indent + 2);
				branch.body->print(indent + 2);
			}
			if (elseBranch) {
				std::cout << std::string(indent + 1, ' ') << "Else branch: " << "\n";
				elseBranch->print(indent + 2);
			}
		}
	};
}