#include "plugin_api.h"

namespace {
    const AbsoluteSyntaxPluginV1 plugin = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        "absolute.desktop",
        0,
        nullptr
    };
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1* absolute_syntax_plugin_init_v1() {
    return &plugin;
}
