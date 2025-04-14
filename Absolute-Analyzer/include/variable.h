#pragma once

#include "type.h"

namespace Absolute {
    struct Variable {
        std::string name;  // Имя переменной
        Scope scope;       // Область видимости
        Type* type;        // Тип переменной (ссылка на Type)

        Variable(std::string name, Scope scope, Type* type)
            : name(std::move(name)), scope(std::move(scope)), type(type) {
        }
    };
}