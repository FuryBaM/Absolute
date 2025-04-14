#pragma once

namespace Absolute {
    struct EnumDeclStmt : Statement {
        std::string name;
        std::vector<std::string> members;

        EnumDeclStmt(std::string name, std::vector<std::string> members)
            : name(std::move(name)), members(std::move(members)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Enum: " << name << "\n";
            if (members.size()) {
                for (const auto& member : members) {
                    std::cout << std::string(indent + 1, ' ') << "Member: " << member << "\n";;
                }
            }
        }
    };

    struct GroupDeclStmt : Statement {
        std::string name;
        std::vector<std::unique_ptr<EnumDeclStmt>> enums;
        std::vector<std::unique_ptr<GroupDeclStmt>> subgroups;

        GroupDeclStmt(std::string name,
            std::vector<std::unique_ptr<EnumDeclStmt>> enums,
            std::vector<std::unique_ptr<GroupDeclStmt>> subgroups)
            : name(std::move(name)), enums(std::move(enums)), subgroups(std::move(subgroups)) { }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Group: " << name << "\n";
            if (subgroups.size()) {
                std::cout << std::string(indent + 1, ' ') << "Subgroups: \n";
                for (const auto& subgroup : subgroups) {
                    subgroup->print(indent + 2);
                }
            }
            if (enums.size()) {
                std::cout << std::string(indent + 1, ' ') << "Enums: \n";
                for (const auto& en : enums) {
                    en->print(indent + 2);
                }
            }
        }
    };
}