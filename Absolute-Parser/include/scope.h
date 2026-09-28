#pragma once
namespace Absolute {
    enum class ScopeType { Global, Class, Struct, Interface, Enum, Group, Function, Namespace };

    struct Scope {
        ScopeType type;      // Scope kind (Class, Function, Struct, etc.)
        std::string name;    // Scope name (MyClass, myFunction, etc.)

        Scope(ScopeType t, std::string n = "") : type(t), name(std::move(n)) {}
    };

    extern std::vector<Scope> scopeStack;

    void EnterScope(ScopeType type, const std::string& name = "");
    void ExitScope();
    std::string GetCurrentScopeName();
    ScopeType GetCurrentScopeType();
}