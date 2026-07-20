#include "analyzer_build_pch.h"
#include "analyzer.h"

namespace Absolute {
    SymbolTable::SymbolTable() { Reset(); }

    void SymbolTable::Reset() {
        symbols.clear();
        scopes.clear();
        scopes.emplace_back();
    }

    void SymbolTable::EnterScope() { scopes.emplace_back(); }

    void SymbolTable::ExitScope() {
        if (scopes.size() > 1) scopes.pop_back();
    }

    size_t SymbolTable::ScopeDepth() const { return scopes.empty() ? 0 : scopes.size() - 1; }

    std::optional<SymbolId> SymbolTable::Declare(
        SymbolKind kind, std::string name, std::string type,
        std::vector<std::string> parameterTypes) {
        if (scopes.empty()) scopes.emplace_back();
        if (const auto found = scopes.back().find(name); found != scopes.back().end()) {
            const Symbol* existing = Get(found->second);
            const bool callable = kind == SymbolKind::Function || kind == SymbolKind::Method;
            const bool existingCallable = existing &&
                (existing->kind == SymbolKind::Function || existing->kind == SymbolKind::Method);
            if (!callable || !existingCallable) return std::nullopt;
            for (const Symbol& symbol : symbols) {
                if (symbol.scopeDepth == ScopeDepth() && symbol.name == name &&
                    (symbol.kind == SymbolKind::Function || symbol.kind == SymbolKind::Method) &&
                    symbol.parameterTypes == parameterTypes)
                    return std::nullopt;
            }
        }
        const SymbolId id = static_cast<SymbolId>(symbols.size());
        symbols.push_back({id, kind, std::move(name), std::move(type),
            std::move(parameterTypes), ScopeDepth()});
        scopes.back().try_emplace(symbols.back().name, id);
        return id;
    }

    SymbolId SymbolTable::Lookup(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return InvalidSymbolId;
    }

    SymbolId SymbolTable::LookupCurrent(const std::string& name) const {
        if (scopes.empty()) return InvalidSymbolId;
        const auto found = scopes.back().find(name);
        return found == scopes.back().end() ? InvalidSymbolId : found->second;
    }

    Symbol* SymbolTable::Get(SymbolId id) {
        return id < symbols.size() ? &symbols[id] : nullptr;
    }

    const Symbol* SymbolTable::Get(SymbolId id) const {
        return id < symbols.size() ? &symbols[id] : nullptr;
    }

    const std::vector<Symbol>& SymbolTable::All() const { return symbols; }
}
