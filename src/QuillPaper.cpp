/*
 * Ink & Quill - a Skyrim SKSE writing framework: the player writes in books in the
 * book menu, with quill, ink or blood, for any mod that gives the text a meaning.
 * Copyright (C) 2026 Zevick
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "QuillPaper.h"

namespace InkAndQuill::QuillPaper {

    namespace {

        struct Vertex {
            RE::NiPoint3 position;  // in the skin's space
            float u = 0.f, v = 0.f;
            float weights[4] = {};
            std::uint16_t bones[4] = {};  // the skin's bone indices
        };

        struct Sheet {
            RE::NiPointer<RE::BSGeometry> shape;  // keeps the skin alive with it
            std::vector<Vertex> vertices;
            std::vector<std::array<std::uint16_t, 3>> triangles;
        };

        std::array<Sheet, 4> g_sheets;  // by page slot: "Base Page1" shows slot 0

        // A skinned sheet's vertices and triangles, from its skin partition's CPU copy (the shape keeps none).  One
        // partition only: several share one buffer in ways not read here, so such a sheet gets the flat quad.
        Sheet ReadSheet(RE::BSGeometry* shape)
        {
            Sheet sheet;
            auto* skin = shape->GetGeometryRuntimeData().skinInstance.get();
            auto* partitions = skin ? skin->skinPartition.get() : nullptr;
            if (!partitions || partitions->numPartitions != 1) {
                SKSE::log::warn("[Quill] '{}' has {} skin partitions: the quill uses the flat quad on it", shape->name.c_str(),
                                partitions ? partitions->numPartitions : 0);
                return sheet;
            }
            const auto& part = partitions->partitions[0];
            const auto* buffer = part.buffData;
            const auto desc = part.vertexDesc;
            if (!buffer || !buffer->rawVertexData || !buffer->rawIndexData || !desc.HasFlag(RE::BSGraphics::Vertex::VF_UV) ||
                !desc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED)) {
                return sheet;
            }
            const auto stride = Stride(desc);
            const auto uvAt = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
            const auto skinAt = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
            const bool full = uvAt >= 16;  // a skinned sheet's are full floats, though its flags say halves
            for (std::uint16_t i = 0; i < part.vertices; ++i) {
                const auto* raw = buffer->rawVertexData + i * stride;
                Vertex vertex;
                vertex.position = Position(raw, full);
                std::uint16_t uv[2], weights[4];
                std::memcpy(uv, raw + uvAt, sizeof(uv));
                std::memcpy(weights, raw + skinAt, sizeof(weights));
                vertex.u = HalfToFloat(uv[0]), vertex.v = HalfToFloat(uv[1]);
                for (int k = 0; k < 4; ++k) {
                    vertex.weights[k] = HalfToFloat(weights[k]);
                    const auto local = raw[skinAt + 8 + k];  // into the partition's bones
                    vertex.bones[k] = local < part.numBones ? part.bones[local] : 0;
                }
                sheet.vertices.push_back(vertex);
            }
            for (std::uint32_t t = 0; t < part.triangles; ++t) {
                const auto* index = buffer->rawIndexData + t * 3;
                if (index[0] >= part.vertices || index[1] >= part.vertices || index[2] >= part.vertices) continue;
                sheet.triangles.push_back({ index[0], index[1], index[2] });
            }
            sheet.shape.reset(shape);
            return sheet;
        }

        // A vertex where the sheet is now: its bones' current transforms, by its weights.
        RE::NiPoint3 Skinned(const Vertex& vertex, RE::NiSkinInstance* skin)
        {
            auto* data = skin->skinData.get();
            RE::NiPoint3 out;
            float total = 0.f;
            for (int k = 0; k < 4; ++k) {
                const float w = vertex.weights[k];
                auto* bone = skin->bones[vertex.bones[k]];
                if (w <= 0.f || !bone || !data) continue;
                out += (bone->world * data->GetBoneDataSkinToBone(vertex.bones[k])) * vertex.position * w;
                total += w;
            }
            return total > 0.f ? out / total : vertex.position;
        }

    }

    void Read(RE::NiAVObject* book)
    {
        Clear();
        if (!book) return;
        for (int slot = 0; slot < 4; ++slot) {
            auto* object = book->GetObjectByName(std::format("Base Page{}", slot + 1));
            auto* shape = object ? object->AsGeometry() : nullptr;
            if (!shape) continue;
            g_sheets[slot] = ReadSheet(shape);
            float u0 = FLT_MAX, u1 = -FLT_MAX, v0 = FLT_MAX, v1 = -FLT_MAX;
            for (const auto& vertex : g_sheets[slot].vertices) u0 = std::min(u0, vertex.u), v0 = std::min(v0, vertex.v);
            // A book's sheets are shifted whole textures along (u 2.0 to 2.74 shows u 0 to 0.74: the texture repeats):
            // taken off, so their UVs are the movie's (a hair below a whole number isn't a shift).
            const float shiftU = std::floor(u0 + 0.01f), shiftV = std::floor(v0 + 0.01f);
            for (auto& vertex : g_sheets[slot].vertices) {
                vertex.u -= shiftU, vertex.v -= shiftV;
                u1 = std::max(u1, vertex.u), v1 = std::max(v1, vertex.v);
            }
            u0 -= shiftU, v0 -= shiftV;
            SKSE::log::debug("[Quill] Page slot {}: '{}', {} vertices, {} triangles, UVs u {:.3f}-{:.3f} v {:.3f}-{:.3f}", slot,
                            object->name.c_str(), g_sheets[slot].vertices.size(), g_sheets[slot].triangles.size(), u0, u1, v0, v1);
        }
    }

    void Clear()
    {
        for (auto& sheet : g_sheets) sheet = {};
    }

    std::optional<RE::NiPoint3> At(int slot, float u, float v)
    {
        if (slot < 0 || slot >= static_cast<int>(g_sheets.size())) return std::nullopt;
        const auto& sheet = g_sheets[slot];
        auto* skin = sheet.shape ? sheet.shape->GetGeometryRuntimeData().skinInstance.get() : nullptr;
        if (!skin) return std::nullopt;
        // The triangle whose UVs hold (u, v), and where in it: its barycentric weights.
        constexpr float kEdge = -1e-4f;
        for (const auto& [i0, i1, i2] : sheet.triangles) {
            const auto &a = sheet.vertices[i0], &b = sheet.vertices[i1], &c = sheet.vertices[i2];
            const float det = (b.v - c.v) * (a.u - c.u) + (c.u - b.u) * (a.v - c.v);
            if (std::abs(det) < 1e-12f) continue;
            const float wa = ((b.v - c.v) * (u - c.u) + (c.u - b.u) * (v - c.v)) / det;
            const float wb = ((c.v - a.v) * (u - c.u) + (a.u - c.u) * (v - c.v)) / det;
            const float wc = 1.f - wa - wb;
            if (wa < kEdge || wb < kEdge || wc < kEdge) continue;
            return Skinned(a, skin) * wa + Skinned(b, skin) * wb + Skinned(c, skin) * wc;
        }
        static std::array<bool, 4> said{};
        if (!std::exchange(said[slot], true)) SKSE::log::warn("[Quill] Page slot {}: no triangle shows UV ({:.3f}, {:.3f})", slot, u, v);
        return std::nullopt;
    }

    std::uint32_t Stride(const RE::BSGraphics::VertexDesc& desc)
    {
        return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(desc) & 0xF) * 4;
    }

    RE::NiPoint3 Position(const std::uint8_t* vertex, bool full)
    {
        if (full) {
            RE::NiPoint3 p;
            std::memcpy(&p, vertex, sizeof(p));
            return p;
        }
        std::uint16_t h[3];
        std::memcpy(h, vertex, sizeof(h));
        return { HalfToFloat(h[0]), HalfToFloat(h[1]), HalfToFloat(h[2]) };
    }

    float HalfToFloat(std::uint16_t h)
    {
        const std::uint32_t sign = (h & 0x8000u) << 16;
        std::uint32_t exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
        if (exponent == 0) {
            if (mantissa == 0) return std::bit_cast<float>(sign);
            while (!(mantissa & 0x400)) {  // subnormal: normalise
                mantissa <<= 1;
                --exponent;
            }
            ++exponent;
            mantissa &= 0x3FF;
        } else if (exponent == 31) {
            return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
        }
        return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
    }

}
