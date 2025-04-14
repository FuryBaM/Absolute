#pragma once
namespace Absolute {
    enum class ScopeType { Global, Class, Struct, Interface, Enum, Group, Function, Namespace };

    struct Scope {
        ScopeType type;      // “ип области (Class, Function, Struct и т. д.)
        std::string name;    // »м€ области (MyClass, myFunction и т. д.)

        Scope(ScopeType t, std::string n = "") : type(t), name(std::move(n)) {}
    };

    extern std::vector<Scope> scopeStack;

    void EnterScope(ScopeType type, const std::string& name = "");
    void ExitScope();
    std::string GetCurrentScopeName();
    ScopeType GetCurrentScopeType();
}