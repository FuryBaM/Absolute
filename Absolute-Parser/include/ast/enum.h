#pragma once

namespace Absolute {
    // A member either takes the number written after it or the one after its
    // predecessor, so the declaration has to keep the two apart: `Missing`
    // following `Error = 404` is 405, and the same member written first is 0.
    struct EnumMemberDecl {
        std::string name;
        bool hasValue = false;
        long long value = 0;
        std::string sourceFile;
        int line = 0;
        int column = 0;
    };

    struct EnumDeclStmt : Statement {
        std::string name;
        std::vector<EnumMemberDecl> members;

        EnumDeclStmt(std::string name, std::vector<EnumMemberDecl> members)
            : name(std::move(name)), members(std::move(members)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Enum: " << name << "\n";
            if (members.size()) {
                for (const auto& member : members) {
                    std::cout << std::string(indent + 1, ' ') << "Member: " << member.name;
                    if (member.hasValue) std::cout << " = " << member.value;
                    std::cout << "\n";
                }
            }
        }

        void Accept(StatementVisitor& visitor) override;
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

        void Accept(StatementVisitor& visitor) override;
    };
}
