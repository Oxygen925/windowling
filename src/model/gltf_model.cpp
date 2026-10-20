#include "model/gltf_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <cgltf.h>

namespace dp::model {

// ---------------------------------------------------------------------------
// 수학
// ---------------------------------------------------------------------------

Mat4 Mat4::FromTrs(const std::array<float, 3>& t, const std::array<float, 4>& q, const std::array<float, 3>& s) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 r;
    r.m = {(1 - 2 * (y * y + z * z)) * s[0], (2 * (x * y + z * w)) * s[0], (2 * (x * z - y * w)) * s[0], 0,
           (2 * (x * y - z * w)) * s[1], (1 - 2 * (x * x + z * z)) * s[1], (2 * (y * z + x * w)) * s[1], 0,
           (2 * (x * z + y * w)) * s[2], (2 * (y * z - x * w)) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0,
           t[0], t[1], t[2], 1};
    return r;
}

Mat4 Mat4::RotationY(float a) {
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.m = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += m[k * 4 + row] * o.m[c * 4 + k];
            r.m[c * 4 + row] = v;
        }
    return r;
}

std::array<float, 3> Mat4::TransformPoint(const std::array<float, 3>& p) const {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

namespace {

std::array<float, 4> Nlerp(const float* a, const float* b, float t) {
    float bb[4] = {b[0], b[1], b[2], b[3]};
    if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0)  // 짧은 쪽으로
        for (float& v : bb) v = -v;
    std::array<float, 4> r;
    for (int i = 0; i < 4; ++i) r[i] = a[i] + (bb[i] - a[i]) * t;
    const float len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    if (len > 0) for (float& v : r) v /= len;
    return r;
}

// ---------------------------------------------------------------------------
// 로드
// ---------------------------------------------------------------------------

std::vector<float> ReadFloats(const cgltf_accessor* a, int comps) {
    std::vector<float> out(a->count * comps);
    for (cgltf_size i = 0; i < a->count; ++i) cgltf_accessor_read_float(a, i, out.data() + i * comps, comps);
    return out;
}

}  // namespace

int Model::FindAnimation(const std::string& name) const {
    for (size_t i = 0; i < animations.size(); ++i)
        if (animations[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string LoadGlb(const std::filesystem::path& path, const ModelLimits& lim, Model& out) {
    out = {};
    cgltf_options opt{};
    opt.type = cgltf_file_type_glb;  // .glb 만
    cgltf_data* data = nullptr;
    const std::string p = path.string();
    if (cgltf_parse_file(&opt, p.c_str(), &data) != cgltf_result_success) return "glb 해석 실패";
    struct Free {
        cgltf_data* d;
        ~Free() { cgltf_free(d); }
    } guard{data};
    // 외부 파일 참조 거부: 모든 버퍼는 GLB BIN 청크여야 한다 (패키지 밖 경로 차단)
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
        if (data->buffers[i].uri) return "외부 버퍼 파일은 지원하지 않음 (.glb 안에 넣어야 함)";
    for (cgltf_size i = 0; i < data->images_count; ++i)
        if (data->images[i].uri) return "외부 이미지 파일은 지원하지 않음 (.glb 안에 넣어야 함)";
    if (cgltf_load_buffers(&opt, data, p.c_str()) != cgltf_result_success) return "glb 버퍼 읽기 실패";
    if (cgltf_validate(data) != cgltf_result_success) return "glTF 검증 실패";
    if (data->skins_count > 1) out.warnings.push_back("스킨이 여러 개 — 첫 번째만 사용");
    if (data->animations_count > lim.maxAnimations) return "애니메이션이 너무 많음";
    if (data->images_count > lim.maxImages) return "이미지가 너무 많음";

    // 노드
    out.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        Node& d = out.nodes[i];
        d.name = n.name ? n.name : "";
        d.parent = n.parent ? static_cast<int>(cgltf_node_index(data, n.parent)) : -1;
        if (n.has_matrix) {
            d.hasMatrix = true;
            std::memcpy(d.matrix.m.data(), n.matrix, sizeof(float) * 16);
        }
        if (n.has_translation) std::copy_n(n.translation, 3, d.t.begin());
        if (n.has_rotation) std::copy_n(n.rotation, 4, d.r.begin());
        if (n.has_scale) std::copy_n(n.scale, 3, d.s.begin());
    }
    // 스킨
    const cgltf_skin* skin = data->skins_count ? &data->skins[0] : nullptr;
    if (skin) {
        if (skin->joints_count > lim.maxJoints) return "관절이 너무 많음";
        for (cgltf_size j = 0; j < skin->joints_count; ++j)
            out.skinJoints.push_back(static_cast<int>(cgltf_node_index(data, skin->joints[j])));
        out.inverseBind.resize(skin->joints_count);
        if (skin->inverse_bind_matrices)
            for (cgltf_size j = 0; j < skin->joints_count; ++j)
                cgltf_accessor_read_float(skin->inverse_bind_matrices, j, out.inverseBind[j].m.data(), 16);
    }
    // 재질·이미지
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& im = data->images[i];
        Image d;
        d.mimeType = im.mime_type ? im.mime_type : "";
        if (im.buffer_view) {
            const auto* bytes = static_cast<const uint8_t*>(im.buffer_view->buffer->data) + im.buffer_view->offset;
            d.bytes.assign(bytes, bytes + im.buffer_view->size);
        }
        out.images.push_back(std::move(d));
    }
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        Material d;
        if (m.has_pbr_metallic_roughness) {
            std::copy_n(m.pbr_metallic_roughness.base_color_factor, 4, d.baseColor.begin());
            if (const cgltf_texture* t = m.pbr_metallic_roughness.base_color_texture.texture) {
                if (t->image) d.image = static_cast<int>(cgltf_image_index(data, t->image));
                if (t->sampler) d.linearFilter = t->sampler->mag_filter != 9728;  // NEAREST
            }
        }
        d.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                    : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
        d.alphaCutoff = m.alpha_cutoff;
        d.doubleSided = m.double_sided;
        out.materials.push_back(d);
    }
    // 메시 (노드에 붙은 것만 = 장면에 보이는 것)
    bool warnedMorph = false;
    size_t vertexTotal = 0;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& n = data->nodes[ni];
        if (!n.mesh) continue;
        const bool skinned = n.skin == skin && skin;
        for (cgltf_size pi = 0; pi < n.mesh->primitives_count; ++pi) {
            const cgltf_primitive& pr = n.mesh->primitives[pi];
            if (pr.type != cgltf_primitive_type_triangles) {
                out.warnings.push_back("삼각형이 아닌 프리미티브 무시");
                continue;
            }
            if (pr.targets_count && !warnedMorph) {
                out.warnings.push_back("morph target 은 지원하지 않음 — 무시");
                warnedMorph = true;
            }
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jnt = nullptr, *wgt = nullptr;
            for (cgltf_size a = 0; a < pr.attributes_count; ++a) {
                const auto& at = pr.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) jnt = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) wgt = at.data;
            }
            if (!pos) continue;
            // 개수는 할당하기 전에 누적으로 확인한다 (선언만 크게 해 수백 MB 를 잡게 하는 파일 방지)
            const cgltf_size indexCount = pr.indices ? pr.indices->count : pos->count;
            if (pos->count > lim.maxVertices || vertexTotal + pos->count > lim.maxVertices) return "정점이 너무 많음";
            if (indexCount / 3 > lim.maxTriangles || out.triangles + indexCount / 3 > lim.maxTriangles) return "삼각형이 너무 많음";
            vertexTotal += pos->count;
            Primitive d;
            d.node = static_cast<int>(ni);
            d.material = pr.material ? static_cast<int>(cgltf_material_index(data, pr.material)) : -1;
            d.skinned = skinned && jnt && wgt;
            const auto P = ReadFloats(pos, 3);
            const auto N = nrm ? ReadFloats(nrm, 3) : std::vector<float>{};
            const auto U = uv ? ReadFloats(uv, 2) : std::vector<float>{};
            const auto W = d.skinned ? ReadFloats(wgt, 4) : std::vector<float>{};
            d.vertices.resize(pos->count);
            for (cgltf_size v = 0; v < pos->count; ++v) {
                Vertex& x = d.vertices[v];
                std::copy_n(&P[v * 3], 3, x.pos);
                if (!N.empty()) std::copy_n(&N[v * 3], 3, x.normal);
                else std::fill_n(x.normal, 3, 0.0f);
                if (!U.empty()) std::copy_n(&U[v * 2], 2, x.uv);
                else x.uv[0] = x.uv[1] = 0;
                if (d.skinned) {
                    cgltf_uint j[4] = {};
                    cgltf_accessor_read_uint(jnt, v, j, 4);
                    for (int k = 0; k < 4; ++k) {
                        if (j[k] >= out.skinJoints.size()) return "관절 인덱스가 범위를 벗어남";
                        x.joints[k] = static_cast<uint16_t>(j[k]);
                        x.weights[k] = W[v * 4 + k];
                    }
                    const float sum = x.weights[0] + x.weights[1] + x.weights[2] + x.weights[3];
                    if (sum > 0) for (float& w : x.weights) w /= sum;
                } else {
                    std::fill_n(x.joints, 4, uint16_t{0});
                    x.weights[0] = 1;
                    x.weights[1] = x.weights[2] = x.weights[3] = 0;
                }
            }
            if (pr.indices) {
                d.indices.resize(pr.indices->count);
                for (cgltf_size k = 0; k < pr.indices->count; ++k) {
                    d.indices[k] = static_cast<uint32_t>(cgltf_accessor_read_index(pr.indices, k));
                    if (d.indices[k] >= d.vertices.size()) return "인덱스가 범위를 벗어남";
                }
            } else {
                d.indices.resize(d.vertices.size());
                for (uint32_t k = 0; k < d.indices.size(); ++k) d.indices[k] = k;
            }
            if (N.empty()) {  // 법선 없음 → 면 법선 (정점을 삼각형마다 분리)
                std::vector<Vertex> flat;
                flat.reserve(d.indices.size());
                for (size_t k = 0; k + 2 < d.indices.size(); k += 3) {
                    Vertex a = d.vertices[d.indices[k]], b = d.vertices[d.indices[k + 1]], c = d.vertices[d.indices[k + 2]];
                    const float e1[3] = {b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
                    const float e2[3] = {c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]};
                    float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    const float len = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
                    if (len > 0) for (float& v : fn) v /= len;
                    for (Vertex* v : {&a, &b, &c}) std::copy_n(fn, 3, v->normal);
                    flat.push_back(a);
                    flat.push_back(b);
                    flat.push_back(c);
                }
                d.vertices = std::move(flat);
                d.indices.resize(d.vertices.size());
                for (uint32_t k = 0; k < d.indices.size(); ++k) d.indices[k] = k;
            }
            out.triangles += d.indices.size() / 3;
            if (out.triangles > lim.maxTriangles) return "삼각형이 너무 많음";
            out.primitives.push_back(std::move(d));
        }
    }
    if (out.primitives.empty()) return "그릴 메시가 없음";
    // 애니메이션
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& a = data->animations[ai];
        Animation d;
        d.name = a.name ? a.name : "animation" + std::to_string(ai);
        for (cgltf_size ci = 0; ci < a.channels_count; ++ci) {
            const cgltf_animation_channel& ch = a.channels[ci];
            if (!ch.target_node || !ch.sampler) continue;
            Channel c;
            c.node = static_cast<int>(cgltf_node_index(data, ch.target_node));
            int comps = 3;
            if (ch.target_path == cgltf_animation_path_type_translation) c.path = Channel::Path::T;
            else if (ch.target_path == cgltf_animation_path_type_rotation) c.path = Channel::Path::R, comps = 4;
            else if (ch.target_path == cgltf_animation_path_type_scale) c.path = Channel::Path::S;
            else continue;  // weights (morph) 무시
            c.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
            c.times = ReadFloats(ch.sampler->input, 1);
            auto values = ReadFloats(ch.sampler->output, comps);
            if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
                // [in-tangent, value, out-tangent] 중 value 만 → LINEAR 근사
                std::vector<float> v;
                for (size_t k = 0; k < c.times.size(); ++k)
                    v.insert(v.end(), values.begin() + (k * 3 + 1) * comps, values.begin() + (k * 3 + 2) * comps);
                values = std::move(v);
            }
            if (values.size() < c.times.size() * comps || c.times.empty()) continue;
            c.values = std::move(values);
            d.duration = std::max(d.duration, c.times.back());
            d.channels.push_back(std::move(c));
        }
        out.animations.push_back(std::move(d));
    }
    return {};
}

// ---------------------------------------------------------------------------
// 포즈
// ---------------------------------------------------------------------------

void EvaluateWorld(const Model& model, int animation, double timeSec, std::vector<Mat4>& world) {
    std::vector<Node> local = model.nodes;
    if (animation >= 0 && animation < static_cast<int>(model.animations.size())) {
        const Animation& a = model.animations[animation];
        const float t = a.duration > 0 ? static_cast<float>(std::fmod(timeSec, static_cast<double>(a.duration))) : 0.0f;
        for (const Channel& c : a.channels) {
            const int comps = c.path == Channel::Path::R ? 4 : 3;
            // 구간 찾기
            size_t k = 0;
            while (k + 1 < c.times.size() && c.times[k + 1] <= t) ++k;
            const size_t k2 = std::min(k + 1, c.times.size() - 1);
            float u = 0;
            if (!c.step && k2 != k && t > c.times[k]) u = (t - c.times[k]) / (c.times[k2] - c.times[k]);
            if (t < c.times.front()) u = 0;
            const float* va = &c.values[k * comps];
            const float* vb = &c.values[k2 * comps];
            Node& n = local[c.node];
            n.hasMatrix = false;  // 애니메이션이 있는 노드는 TRS 를 쓴다
            if (c.path == Channel::Path::R) n.r = Nlerp(va, vb, u);
            else {
                auto& dst = c.path == Channel::Path::T ? n.t : n.s;
                for (int i = 0; i < 3; ++i) dst[i] = va[i] + (vb[i] - va[i]) * u;
            }
        }
    }
    world.assign(local.size(), Mat4::Identity());
    // 부모가 먼저 계산되도록 (glTF 노드 순서는 보장 없음) — 필요할 때 재귀 대신 반복
    std::vector<char> done(local.size(), 0);
    for (size_t i = 0; i < local.size(); ++i) {
        // i 의 조상부터 차례로
        std::vector<int> chain;
        for (int n = static_cast<int>(i); n >= 0 && !done[n]; n = local[n].parent) chain.push_back(n);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const Node& n = local[*it];
            const Mat4 l = n.hasMatrix ? n.matrix : Mat4::FromTrs(n.t, n.r, n.s);
            world[*it] = n.parent >= 0 ? world[n.parent] * l : l;
            done[*it] = 1;
        }
    }
}

void SkinPalette(const Model& model, const std::vector<Mat4>& world, std::vector<Mat4>& palette) {
    palette.resize(model.skinJoints.size());
    for (size_t j = 0; j < model.skinJoints.size(); ++j) palette[j] = world[model.skinJoints[j]] * model.inverseBind[j];
}

Bounds RestBounds(const Model& model, float yaw) {
    std::vector<Mat4> world, palette;
    EvaluateWorld(model, -1, 0, world);
    SkinPalette(model, world, palette);
    const Mat4 rot = Mat4::RotationY(yaw);
    Bounds b;
    b.min.fill(std::numeric_limits<float>::max());
    b.max.fill(std::numeric_limits<float>::lowest());
    for (const Primitive& p : model.primitives) {
        for (const Vertex& v : p.vertices) {
            std::array<float, 3> q{0, 0, 0};
            const std::array<float, 3> src{v.pos[0], v.pos[1], v.pos[2]};
            if (p.skinned) {
                for (int k = 0; k < 4; ++k) {
                    if (v.weights[k] == 0) continue;
                    const auto s = palette[v.joints[k]].TransformPoint(src);
                    for (int i = 0; i < 3; ++i) q[i] += s[i] * v.weights[k];
                }
            } else {
                q = world[p.node].TransformPoint(src);
            }
            q = rot.TransformPoint(q);
            for (int i = 0; i < 3; ++i) {
                b.min[i] = std::min(b.min[i], q[i]);
                b.max[i] = std::max(b.max[i], q[i]);
            }
        }
    }
    return b;
}

}  // namespace dp::model
