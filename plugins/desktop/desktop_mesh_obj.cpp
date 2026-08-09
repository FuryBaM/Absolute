// Wavefront OBJ mesh loader (portable).
// Interleaved vertex format: pos.xyz + normal.xyz + uv.xy  (8 floats / vertex).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    struct MeshData {
        std::vector<float> vertices; // 8 floats per vertex
        std::vector<int32_t> indices;
        bool valid = false;
    };

    MeshData* MeshFromHandle(int64_t handle) {
        return reinterpret_cast<MeshData*>(static_cast<intptr_t>(handle));
    }

    int64_t MeshToHandle(MeshData* mesh) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(mesh));
    }

    struct Vec3 {
        float x = 0, y = 0, z = 0;
    };
    struct Vec2 {
        float u = 0, v = 0;
    };

    struct FaceCorner {
        int32_t v = 0;
        int32_t vt = 0;
        int32_t vn = 0;
    };

    struct VertexKey {
        int32_t v = 0;
        int32_t vt = 0;
        int32_t vn = 0;
        bool operator==(const VertexKey& o) const {
            return v == o.v && vt == o.vt && vn == o.vn;
        }
    };

    struct VertexKeyHash {
        std::size_t operator()(const VertexKey& k) const {
            return (static_cast<std::size_t>(k.v) * 73856093u)
                ^ (static_cast<std::size_t>(k.vt) * 19349663u)
                ^ (static_cast<std::size_t>(k.vn) * 83492791u);
        }
    };

    int32_t ResolveIndex(int32_t idx, int32_t count) {
        if (idx > 0) return idx - 1;
        if (idx < 0) return count + idx;
        return -1;
    }

    bool ParseCorner(const std::string& token, FaceCorner& out) {
        // v, v/vt, v//vn, v/vt/vn
        out = {};
        if (token.empty()) return false;
        int32_t parts[3] = {0, 0, 0};
        int part = 0;
        std::string cur;
        for (std::size_t i = 0; i <= token.size(); ++i) {
            const char c = i < token.size() ? token[i] : '/';
            if (c == '/' || i == token.size()) {
                if (!cur.empty()) {
                    parts[part] = std::atoi(cur.c_str());
                    cur.clear();
                }
                if (i < token.size()) {
                    ++part;
                    if (part > 2) return false;
                }
            } else {
                cur.push_back(c);
            }
        }
        out.v = parts[0];
        out.vt = parts[1];
        out.vn = parts[2];
        return out.v != 0;
    }

    Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    Vec3 Sub(const Vec3& a, const Vec3& b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }

    Vec3 Normalize(const Vec3& v) {
        const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 1e-8f) return {0, 1, 0};
        return {v.x / len, v.y / len, v.z / len};
    }

    MeshData* LoadObjFile(const char* path) {
        if (!path || !path[0]) return nullptr;
        std::ifstream file(path);
        if (!file) return nullptr;

        std::vector<Vec3> positions;
        std::vector<Vec2> texcoords;
        std::vector<Vec3> normals;
        std::vector<std::vector<FaceCorner>> faces;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "v") {
                Vec3 p{};
                ss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            } else if (tag == "vt") {
                Vec2 t{};
                ss >> t.u >> t.v;
                texcoords.push_back(t);
            } else if (tag == "vn") {
                Vec3 n{};
                ss >> n.x >> n.y >> n.z;
                normals.push_back(Normalize(n));
            } else if (tag == "f") {
                std::vector<FaceCorner> face;
                std::string tok;
                while (ss >> tok) {
                    FaceCorner c{};
                    if (!ParseCorner(tok, c)) continue;
                    face.push_back(c);
                }
                if (face.size() >= 3) faces.push_back(std::move(face));
            }
        }

        if (positions.empty() || faces.empty()) return nullptr;

        auto* mesh = new MeshData();
        std::unordered_map<VertexKey, int32_t, VertexKeyHash> remap;
        remap.reserve(positions.size() * 2);

        auto emitVertex = [&](const FaceCorner& c) -> int32_t {
            const int32_t vi = ResolveIndex(c.v, static_cast<int32_t>(positions.size()));
            if (vi < 0 || vi >= static_cast<int32_t>(positions.size())) return -1;
            int32_t ti = ResolveIndex(c.vt, static_cast<int32_t>(texcoords.size()));
            int32_t ni = ResolveIndex(c.vn, static_cast<int32_t>(normals.size()));
            if (c.vt == 0) ti = -1;
            if (c.vn == 0) ni = -1;

            VertexKey key{vi, ti, ni};
            const auto it = remap.find(key);
            if (it != remap.end()) return it->second;

            const Vec3& p = positions[static_cast<std::size_t>(vi)];
            Vec3 n = {0, 1, 0};
            if (ni >= 0 && ni < static_cast<int32_t>(normals.size())) {
                n = normals[static_cast<std::size_t>(ni)];
            }
            Vec2 t = {0, 0};
            if (ti >= 0 && ti < static_cast<int32_t>(texcoords.size())) {
                t = texcoords[static_cast<std::size_t>(ti)];
            }

            const int32_t index = static_cast<int32_t>(mesh->vertices.size() / 8);
            mesh->vertices.push_back(p.x);
            mesh->vertices.push_back(p.y);
            mesh->vertices.push_back(p.z);
            mesh->vertices.push_back(n.x);
            mesh->vertices.push_back(n.y);
            mesh->vertices.push_back(n.z);
            mesh->vertices.push_back(t.u);
            mesh->vertices.push_back(t.v);
            remap.emplace(key, index);
            return index;
        };

        // Generate face normals when vn missing (per-triangle).
        for (const auto& face : faces) {
            // Fan triangulation.
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                FaceCorner c0 = face[0];
                FaceCorner c1 = face[i];
                FaceCorner c2 = face[i + 1];

                const bool needN = (c0.vn == 0 || c1.vn == 0 || c2.vn == 0);
                if (needN) {
                    const int32_t i0 = ResolveIndex(c0.v, static_cast<int32_t>(positions.size()));
                    const int32_t i1 = ResolveIndex(c1.v, static_cast<int32_t>(positions.size()));
                    const int32_t i2 = ResolveIndex(c2.v, static_cast<int32_t>(positions.size()));
                    if (i0 >= 0 && i1 >= 0 && i2 >= 0) {
                        const Vec3 fn = Normalize(Cross(
                            Sub(positions[static_cast<std::size_t>(i1)], positions[static_cast<std::size_t>(i0)]),
                            Sub(positions[static_cast<std::size_t>(i2)], positions[static_cast<std::size_t>(i0)])));
                        // Temporarily store synthetic normal index via vn channel override
                        // by pushing into normals vector once per triangle for corners without vn.
                        // Simpler: write face normal into vertices after emit if vn missing —
                        // handled by emitVertex using default; fix after emit by patching.
                        // Use unique synthetic normals:
                        if (c0.vn == 0) {
                            normals.push_back(fn);
                            c0.vn = static_cast<int32_t>(normals.size());
                        }
                        if (c1.vn == 0) {
                            normals.push_back(fn);
                            c1.vn = static_cast<int32_t>(normals.size());
                        }
                        if (c2.vn == 0) {
                            normals.push_back(fn);
                            c2.vn = static_cast<int32_t>(normals.size());
                        }
                    }
                }

                const int32_t a = emitVertex(c0);
                const int32_t b = emitVertex(c1);
                const int32_t c = emitVertex(c2);
                if (a < 0 || b < 0 || c < 0) continue;
                mesh->indices.push_back(a);
                mesh->indices.push_back(b);
                mesh->indices.push_back(c);
            }
        }

        if (mesh->indices.empty() || mesh->vertices.empty()) {
            delete mesh;
            return nullptr;
        }
        mesh->valid = true;
        return mesh;
    }
}

extern "C" int64_t absolute_desktop_mesh_load_obj(const char* path) {
    MeshData* mesh = LoadObjFile(path);
    return mesh ? MeshToHandle(mesh) : 0;
}

extern "C" void absolute_desktop_mesh_destroy(int64_t handle) {
    delete MeshFromHandle(handle);
}

extern "C" int32_t absolute_desktop_mesh_is_valid(int64_t handle) {
    const MeshData* mesh = MeshFromHandle(handle);
    return mesh && mesh->valid ? 1 : 0;
}

extern "C" int32_t absolute_desktop_mesh_vertex_float_count(int64_t handle) {
    const MeshData* mesh = MeshFromHandle(handle);
    return mesh ? static_cast<int32_t>(mesh->vertices.size()) : 0;
}

extern "C" int32_t absolute_desktop_mesh_index_count(int64_t handle) {
    const MeshData* mesh = MeshFromHandle(handle);
    return mesh ? static_cast<int32_t>(mesh->indices.size()) : 0;
}

extern "C" int32_t absolute_desktop_mesh_stride_floats() {
    return 8; // pos3 + normal3 + uv2
}

// Copies into caller buffers (must be large enough). Returns 1 on success.
extern "C" int32_t absolute_desktop_mesh_copy_vertices(int64_t handle, float* out, int32_t floatCapacity) {
    const MeshData* mesh = MeshFromHandle(handle);
    if (!mesh || !out) return 0;
    const int32_t n = static_cast<int32_t>(mesh->vertices.size());
    if (floatCapacity < n) return 0;
    if (n > 0) {
        std::memcpy(out, mesh->vertices.data(), static_cast<std::size_t>(n) * sizeof(float));
    }
    return 1;
}

extern "C" int32_t absolute_desktop_mesh_copy_indices(int64_t handle, int32_t* out, int32_t indexCapacity) {
    const MeshData* mesh = MeshFromHandle(handle);
    if (!mesh || !out) return 0;
    const int32_t n = static_cast<int32_t>(mesh->indices.size());
    if (indexCapacity < n) return 0;
    if (n > 0) {
        std::memcpy(out, mesh->indices.data(), static_cast<std::size_t>(n) * sizeof(int32_t));
    }
    return 1;
}

// Direct GPU upload helpers (avoid Absolute array round-trip).
extern "C" int64_t absolute_desktop_gpu_buffer_create(
    int64_t gpuHandle, const float* data, int32_t floatCount);
extern "C" int64_t absolute_desktop_gpu_index_buffer_create(
    int64_t gpuHandle, const int32_t* indices, int32_t indexCount);

extern "C" int64_t absolute_desktop_mesh_create_vertex_buffer(int64_t gpuHandle, int64_t meshHandle) {
    const MeshData* mesh = MeshFromHandle(meshHandle);
    if (!mesh || !mesh->valid || mesh->vertices.empty()) return 0;
    return absolute_desktop_gpu_buffer_create(
        gpuHandle, mesh->vertices.data(), static_cast<int32_t>(mesh->vertices.size()));
}

extern "C" int64_t absolute_desktop_mesh_create_index_buffer(int64_t gpuHandle, int64_t meshHandle) {
    const MeshData* mesh = MeshFromHandle(meshHandle);
    if (!mesh || !mesh->valid || mesh->indices.empty()) return 0;
    return absolute_desktop_gpu_index_buffer_create(
        gpuHandle, mesh->indices.data(), static_cast<int32_t>(mesh->indices.size()));
}
