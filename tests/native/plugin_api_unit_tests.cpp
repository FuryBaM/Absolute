#include "package_manager.h"
#include "plugin_api.h"
#include "syntax_plugins.h"

#include <cassert>
#include <fstream>
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

void TestP2ArtifactsAndBackends() {
    AbsoluteArtifactV1 artifact{};
    artifact.struct_size = sizeof(AbsoluteArtifactV1);
    artifact.kind = ABSOLUTE_ARTIFACT_SPIRV;
    artifact.file_path = "shader.spv";
    artifact.target_triple = ABSOLUTE_TARGET_TRIPLE_SPIRV;

    assert(artifact.kind == ABSOLUTE_ARTIFACT_SPIRV);
    assert(std::string(artifact.target_triple) == ABSOLUTE_TARGET_TRIPLE_SPIRV);

    // GPU IR kinds are first-class and distinct from host object paths.
    assert(ABSOLUTE_ARTIFACT_SPIRV == 5);
    assert(ABSOLUTE_ARTIFACT_DXIL == 6);
    assert(ABSOLUTE_ARTIFACT_SOURCE_TEXT == 10);
    assert(ABSOLUTE_ARTIFACT_METAL_IR == 12);
    assert(std::string(ABSOLUTE_TARGET_TRIPLE_DXIL) == "dxil-ms-dx");
    assert(std::string(ABSOLUTE_TARGET_TRIPLE_METAL) == "metal");
    assert(std::string(ABSOLUTE_TARGET_TRIPLE_HLSL) == "hlsl");
    assert(std::string(ABSOLUTE_TARGET_TRIPLE_MSL) == "msl");
    assert(std::string(ABSOLUTE_TARGET_TRIPLE_GLSL) == "glsl");

    AbsoluteArtifactV1 dxil{};
    dxil.struct_size = sizeof(AbsoluteArtifactV1);
    dxil.kind = ABSOLUTE_ARTIFACT_DXIL;
    dxil.target_triple = ABSOLUTE_TARGET_TRIPLE_DXIL;
    assert(dxil.kind == ABSOLUTE_ARTIFACT_DXIL);

    AbsoluteArtifactV1 metalIr{};
    metalIr.struct_size = sizeof(AbsoluteArtifactV1);
    metalIr.kind = ABSOLUTE_ARTIFACT_METAL_IR;
    metalIr.target_triple = ABSOLUTE_TARGET_TRIPLE_METAL;
    assert(metalIr.kind == ABSOLUTE_ARTIFACT_METAL_IR);

    AbsoluteArtifactV1 hlslText{};
    hlslText.struct_size = sizeof(AbsoluteArtifactV1);
    hlslText.kind = ABSOLUTE_ARTIFACT_SOURCE_TEXT;
    hlslText.target_triple = ABSOLUTE_TARGET_TRIPLE_HLSL;
    assert(hlslText.kind == ABSOLUTE_ARTIFACT_SOURCE_TEXT);

    AbsoluteBackendDescriptorV1 backend{};
    backend.struct_size = sizeof(AbsoluteBackendDescriptorV1);
    backend.abi_version = ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION;
    backend.target_name = "spirv";

    assert(std::string(backend.target_name) == "spirv");

    AbsoluteCacheKeyV1 cacheKey{"pluginA", "1.0.0", 0x12345678, "hash123"};
    assert(std::string(cacheKey.plugin_name) == "pluginA");
    assert(cacheKey.schema_hash == 0x12345678);

    std::cout << "[UNIT TEST] P2 Artifacts, Backends, and Cache keys unit tests passed.\n";
}

#include "plugin_loader.h"

void TestP3IdeDebuggerAndIsolation() {
    AbsoluteIdeCapabilitiesV1 ideCaps{};
    ideCaps.struct_size = sizeof(AbsoluteIdeCapabilitiesV1);

    AbsoluteDebuggerHooksV1 debugHooks{};
    debugHooks.struct_size = sizeof(AbsoluteDebuggerHooksV1);

    AbsolutePermissionPolicyV1 permPolicy{};
    permPolicy.struct_size = sizeof(AbsolutePermissionPolicyV1);
    permPolicy.allow_filesystem_read = 1;
    permPolicy.allow_network = 0;

    assert(permPolicy.allow_filesystem_read == 1);
    assert(permPolicy.allow_network == 0);

    PluginManager pm;
    pm.SetPermissionPolicy(permPolicy);
    assert(pm.GetPermissionPolicy().allow_network == 0);

    pm.SetIsolationMode(ABSOLUTE_ISOLATION_SANDBOX);
    assert(pm.GetIsolationMode() == ABSOLUTE_ISOLATION_SANDBOX);

    // Test permission enforcement
    std::filesystem::path tempManifest = std::filesystem::temp_directory_path() / "test_perm.absplugin";
    std::ofstream out(tempManifest);
    out << R"({
        "name": "perm_test_plugin",
        "version": "1.0.0",
        "abi": 1,
        "library": "nonexistent.dll",
        "permissions": ["network.http"]
    })";
    out.close();

    bool caught = false;
    try {
        pm.Load(tempManifest);
    } catch (const std::exception& ex) {
        std::string msg = ex.what();
        if (msg.find("disallowed permission") != std::string::npos) {
            caught = true;
        }
    }
    std::filesystem::remove(tempManifest);
    assert(caught == true);

    std::cout << "[UNIT TEST] P3 IDE, Debugger, and Permission policies unit tests passed.\n";
}

int main() {
    try {
        TestSemVerEngine();
        TestPluginCAbiLayout();
        TestSourceMapperStructures();
        TestP2ArtifactsAndBackends();
        TestP3IdeDebuggerAndIsolation();
        std::cout << "All P0/P1/P2/P3 C++ plugin API unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Unit test failure: " << ex.what() << '\n';
        return 1;
    }
}
