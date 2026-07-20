#pragma once

#include "plugin_api.h"

namespace Absolute {
    struct OpaquePluginStmt : Statement {
        std::string pluginName;
        std::string keyword;
        AbsoluteOpaqueAstNodeV1 node{};

        OpaquePluginStmt(std::string pluginName, std::string keyword, AbsoluteOpaqueAstNodeV1 node)
            : pluginName(std::move(pluginName)), keyword(std::move(keyword)), node(node) {}

        ~OpaquePluginStmt() override {
            if (node.payload && node.vtable && node.vtable->destroy)
                node.vtable->destroy(node.payload);
        }

        OpaquePluginStmt(const OpaquePluginStmt&) = delete;
        OpaquePluginStmt& operator=(const OpaquePluginStmt&) = delete;

        std::string ToString(int indent = 0) const override {
            std::string detail;
            if (node.payload && node.vtable && node.vtable->debug_string) {
                const char* text = node.vtable->debug_string(node.payload);
                if (text) detail = text;
            }
            std::string result = std::string(indent, ' ') +
                "Opaque plugin statement [" + pluginName + ":" + keyword + "]";
            if (!detail.empty()) result += ": " + detail;
            return result;
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
