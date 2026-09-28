#pragma once

#include "type.h"

namespace Absolute {
    struct Variable {
        std::string name;  // Variable name
        Scope scope;       // Declaring scope
        Type* type;        // Variable type (non-owning Type pointer)

        Variable(std::string name, Scope scope, Type* type)
            : name(std::move(name)), scope(std::move(scope)), type(type) {
        }
    };
}