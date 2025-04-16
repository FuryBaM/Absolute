#pragma once
#include <optional>
#include <unordered_map>
#include <string>
#include "scope.h"

namespace Absolute {
    // Модификаторы доступа
    enum class AccessModifier {
        PUBLIC,
        PRIVATE,
        PROTECTED,
        INTERNAL
    };

    // Перечисление примитивных типов
    enum class PrimitiveTypeEnum {
        INT8, INT16, INT32, INT64,
        UINT8, UINT16, UINT32, UINT64,
        FLOAT, DOUBLE, STRING, CHAR, VOID, DYNAMIC, AUTO
    };


    const std::unordered_map<std::string, PrimitiveTypeEnum> StringToPrimitiveType = {
        {"int8", PrimitiveTypeEnum::INT8},
        {"int16", PrimitiveTypeEnum::INT16},
        {"int32", PrimitiveTypeEnum::INT32},
        {"int64", PrimitiveTypeEnum::INT64},
        {"uint8", PrimitiveTypeEnum::UINT8},
        {"uint16", PrimitiveTypeEnum::UINT16},
        {"uint32", PrimitiveTypeEnum::UINT32},
        {"uint64", PrimitiveTypeEnum::UINT64},
        {"float", PrimitiveTypeEnum::FLOAT},
        {"double", PrimitiveTypeEnum::DOUBLE},
        {"string", PrimitiveTypeEnum::STRING},
        {"char", PrimitiveTypeEnum::CHAR},
        {"void", PrimitiveTypeEnum::VOID},
        {"dynamic", PrimitiveTypeEnum::DYNAMIC},
        {"auto", PrimitiveTypeEnum::AUTO}
    };

    const std::unordered_map<PrimitiveTypeEnum, std::string> PrimitiveTypeToString = {
        {PrimitiveTypeEnum::INT8, "int8"},
        {PrimitiveTypeEnum::INT16, "int16"},
        {PrimitiveTypeEnum::INT32, "int32"},
        {PrimitiveTypeEnum::INT64, "int64"},
        {PrimitiveTypeEnum::UINT8, "uint8"},
        {PrimitiveTypeEnum::UINT16, "uint16"},
        {PrimitiveTypeEnum::UINT32, "uint32"},
        {PrimitiveTypeEnum::UINT64, "uint64"},
        {PrimitiveTypeEnum::FLOAT, "float"},
        {PrimitiveTypeEnum::DOUBLE, "double"},
        {PrimitiveTypeEnum::STRING, "string"},
        {PrimitiveTypeEnum::CHAR, "char"},
        {PrimitiveTypeEnum::VOID, "void"},
        {PrimitiveTypeEnum::DYNAMIC, "dynamic"},
        {PrimitiveTypeEnum::AUTO, "auto"}
    };

    // Функция для получения Enum из строки
    inline std::optional<PrimitiveTypeEnum> PrimitiveStringToEnum(const std::string& str) {
        auto it = StringToPrimitiveType.find(str);
        if (it != StringToPrimitiveType.end()) {
            return it->second;
        }
        return std::nullopt; // Если строка не найдена, вернём пустой результат
    }

    // Функция для получения строки
    inline std::string PrimitiveEnumToString(PrimitiveTypeEnum type) {
        return PrimitiveTypeToString.at(type);
    }

    // Абстрактный базовый класс для типов
    struct Type {
        virtual ~Type() = default;
    };

    // Примитивный тип (int, float и т. д.)
    struct PrimitiveType : Type {
    public:
        PrimitiveTypeEnum type;

        PrimitiveType(PrimitiveTypeEnum type) : type(type) {}
		PrimitiveType(const PrimitiveType& other) : type(other.type) {}
    };

    // Поля класса
    struct ClassField {
        AccessModifier access;
        Type* type;

        ClassField(AccessModifier access, Type* type)
            : access(access), type(type) {
        }
    };

    // Методы класса
    struct ClassMethod {
        AccessModifier access;
        std::string name;
        Type* returnType;
        std::vector<Type*> parameters;

        ClassMethod(AccessModifier access, std::string name, Type* returnType)
            : access(access), name(std::move(name)), returnType(returnType) {
        }
    };

    // Пользовательский тип (класс, структура)
    struct UserType : Type {
    public:
        std::string name;
        AccessModifier access;  // Уровень доступа (private, public и т.д.)
        Scope scope;            // Область, в которой объявлен

        std::unordered_map<std::string, ClassField> fields;
        std::unordered_map<std::string, ClassMethod> methods;

        UserType(AccessModifier access, Scope scope, std::string name)
            : access(access), scope(scope) {
            name = std::move(name);
        }
    };
}