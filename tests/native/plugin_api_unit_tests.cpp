#include "package_manager.h"
#include "plugin_api.h"
#include "syntax_plugins.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace Absolute;

void TestSemVerEngine() {
    SemVer v1 = SemVer::Parse("1.2.3");
    assert(v1.major == 1 && v1.minor == 2 && v1.patch == 3);

    assert(SemVer::Satisfies("1.2.3", "^1.0.0") == true);
    assert(SemVer::Satisfies("2.0.0", "^1.0.0") == false);
    assert(SemVer::Satisfies("1.5.0", ">=1.2.0 <2.0.0") == true);
    assert(SemVer::Satisfies("1.1.0", ">=1.2.0") == false);

    std::cout << "[UNIT TEST] SemVer engine tests passed.\n";
}

void TestPluginCAbiLayout() {
    assert(sizeof(AbsoluteCompilerPluginV1) >= sizeof(uint32_t) * 4);
    assert(sizeof(AbsoluteLanguagePluginV1) >= sizeof(uint32_t) * 2);
    assert(sizeof(AbsoluteEditorPluginV1) >= sizeof(uint32_t) * 2);
    assert(sizeof(AbsoluteRuntimePluginV1) >= sizeof(uint32_t) * 2);
    assert(sizeof(AbsoluteResourceDescriptorV1) >= sizeof(uint32_t));

    AbsoluteResourceDescriptorV1 desc1{};
    desc1.struct_size = sizeof(AbsoluteResourceDescriptorV1);
    desc1.copy_message_function = "copy_msg";

    PluginResourceDescriptor resDesc;
    resDesc.pluginName = "test_plugin";
    resDesc.typeName = "ResourceA";
    resDesc.copyMessageFunction = "copy_msg";

    assert(resDesc.canCrossIsolateBoundary() == true);

    PluginResourceDescriptor localResDesc;
    localResDesc.pluginName = "test_plugin";
    localResDesc.typeName = "ResourceB";

    assert(localResDesc.canCrossIsolateBoundary() == false);

    std::cout << "[UNIT TEST] Plugin C ABI layout & isolate boundary tests passed.\n";
}

void TestSourceMapperStructures() {
    AbsoluteSourceSpanV1 hostSpan{"main.abs", 10, 5, 10, 25};
    AbsoluteSourceSpanV1 genSpan{"generated.cpp", 1, 1, 5, 40};

    AbsoluteSourceMapEntryV1 entry{hostSpan, genSpan, "hlsl"};
    assert(std::string(entry.foreign_language) == "hlsl");
    assert(entry.host_span.start_line == 10);
    assert(entry.generated_span.end_column == 40);

    std::cout << "[UNIT TEST] Source mapper structures unit tests passed.\n";
}

int main() {
    try {
        TestSemVerEngine();
        TestPluginCAbiLayout();
        TestSourceMapperStructures();
        std::cout << "All P0/P1 C++ plugin API unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Unit test failure: " << ex.what() << '\n';
        return 1;
    }
}
