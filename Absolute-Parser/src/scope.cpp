#include "pch.h"
#include "scope.h"

std::vector<Scope> scopeStack;

void EnterScope(ScopeType type, const std::string& name) {
    scopeStack.emplace_back(type, name);
}

void ExitScope() {
    if (!scopeStack.empty()) {
        scopeStack.pop_back();
    }
}

std::string GetCurrentScopeName() {
    return scopeStack.empty() ? "" : scopeStack.back().name;
}

ScopeType GetCurrentScopeType() {
    return scopeStack.empty() ? ScopeType::Global : scopeStack.back().type;
}