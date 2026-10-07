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

#include "QuillCursor.h"

#include "QuillPaper.h"

#include "BookMovie.h"
#include "Keys.h"
#include "Settings.h"

#include <Windows.h>
#include <numbers>
#include <sstream>

namespace InkAndQuill::QuillCursor {

    namespace {

        constexpr RE::FormID kQuill = 0x04C3C8;  // Skyrim.esm Quill01: its model, or a replacer's
        // The nib in the model's own space: its tip, the vertex furthest down its long axis (-z; FindNib).
        RE::NiPoint3 g_nib{ 0.f, 0.f, -12.f };

        RE::NiPointer<RE::NiNode> g_parent;
        RE::NiPointer<RE::NiAVObject> g_quill;
        float g_size = 0.f;  // the model's radius at scale 1

        // The quill's pose in the page's space: translate is from the caret's point, or with tracking
        // calibrated, where it is with the caret at the calibration's first point.
        struct Pose {
            RE::NiPoint3 translate;
            RE::NiMatrix3 rotate;
            float scale = 1.f;
        };
        Pose g_pose;

        // Calibrated tracking (adjust mode, numpad 1 twice): the quill moves kx, ky page units per stage
        // unit the caret moves from (gx, gy).  Corrects the UV mapping and perspective together.
        struct Track {
            bool valid = false;
            float gx = 0.f, gy = 0.f, kx = 0.f, ky = 0.f;
        };
        Track g_track;
        std::optional<std::pair<RE::NiPoint2, RE::NiPoint3>> g_firstPoint;  // the caret, the quill's translate
        std::string g_model;  // its pose's section in the pose file: "Book" or "Note"

        // The page text's quad (PageText), in its own space: the corners the render target's
        // corners land on, and the UVs they have.  Read once per book.
        struct PageQuad {
            bool valid = false;
            RE::NiPoint3 p00, p10, p01, p11;  // by UV: (min, min), (max, min), (min, max), (max, max)
            float u0 = 0.f, u1 = 1.f, v0 = 0.f, v1 = 1.f;
        };
        PageQuad g_quad;
        RE::GRectF g_visible;  // book.swf's visible stage, which the page texture holds
        RE::NiPoint3 g_anchor;                           // the caret's point, in the page's space
        std::string g_caret;                             // book.swf's last answer (EditCaretPoint)
        std::optional<RE::NiPoint2> g_stagePoint;        // the caret on the stage, from it
        int g_slot = 0;                                  // the engine's page slot the caret is on
        std::optional<RE::NiPoint2> g_uv;                // the caret's point in the page texture

        // Adjust mode (Settings::kQuillAdjust): the numpad moves or turns the quill.
        bool g_turning = false;
        int g_step = 1;
        constexpr float kMoveSteps[] = { 0.02f, 0.1f, 0.5f, 2.f };
        constexpr float kTurnSteps[] = { 0.5f, 1.f, 5.f, 15.f };  // degrees
        constexpr float kScaleSteps[] = { 1.005f, 1.01f, 1.05f, 1.2f };
        constexpr int kSteps = static_cast<int>(std::size(kMoveSteps));
        int g_samples = 0;

        // The configured pose, for books and for notes, each the other's when it has none (adjust mode: numpad
        // Enter saves, "." restores): "Pose = x y z scale" and the rotation's rows, under [Book] or [Note].
        std::string PosePath() { return std::filesystem::absolute("Data/SKSE/Plugins/InkAndQuill_QuillPose.ini").string(); }

        std::optional<Pose> ReadPose(const char* key, const std::string& section = g_model)
        {
            char text[512] = {};
            GetPrivateProfileStringA(section.c_str(), key, "", text, sizeof(text), PosePath().c_str());
            std::istringstream in(text);
            Pose pose;
            auto& r = pose.rotate.entry;
            if (!(in >> pose.translate.x >> pose.translate.y >> pose.translate.z >> pose.scale >> r[0][0] >> r[0][1] >> r[0][2] >>
                  r[1][0] >> r[1][1] >> r[1][2] >> r[2][0] >> r[2][1] >> r[2][2])) {
                return std::nullopt;
            }
            return pose;
        }

        Track ReadTrack(const std::string& section)
        {
            char text[256] = {};
            GetPrivateProfileStringA(section.c_str(), "Track", "", text, sizeof(text), PosePath().c_str());
            Track track;
            track.valid = std::sscanf(text, "%f %f %f %f", &track.gx, &track.gy, &track.kx, &track.ky) == 4;
            return track;
        }

        bool LoadPose()
        {
            // Books and notes share a pose until one is saved for the other.
            std::string section = g_model;
            auto pose = ReadPose("Pose");
            if (!pose) {
                section = g_model == "Note" ? "Book" : "Note";
                pose = ReadPose("Pose", section);
            }
            if (pose) {
                g_pose = *pose;
                g_track = ReadTrack(section);
            }
            return pose.has_value();
        }

        void SavePose()
        {
            const auto& t = g_pose.translate;
            const auto& r = g_pose.rotate.entry;
            const auto text = std::format("{} {} {} {} {} {} {} {} {} {} {} {} {}", t.x, t.y, t.z, g_pose.scale, r[0][0], r[0][1],
                                          r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
            if (!WritePrivateProfileStringA(g_model.c_str(), "Pose", text.c_str(), PosePath().c_str())) {
                SKSE::log::error("[Quill] Couldn't write the pose file");
            }
            const auto track = g_track.valid ? std::format("{} {} {} {}", g_track.gx, g_track.gy, g_track.kx, g_track.ky) : "";
            WritePrivateProfileStringA(g_model.c_str(), "Track", g_track.valid ? track.c_str() : nullptr, PosePath().c_str());
        }

        using Book::Menu;

        void Log(const char* what, RE::NiAVObject* object)
        {
            if (!object) return;
            const auto& w = object->world;
            const auto& b = object->worldBound;
            SKSE::log::info("[Quill] {} '{}': world ({:.2f}, {:.2f}, {:.2f}) x{:.3f}, bound ({:.2f}, {:.2f}, {:.2f}) r{:.2f}",
                            what, object->name.c_str(), w.translate.x, w.translate.y, w.translate.z, w.scale,
                            b.center.x, b.center.y, b.center.z, b.radius);
        }

        // The quill's tip in its own space: the vertex furthest down its long axis (-z), over its shapes' CPU copies.
        void FindNib(RE::NiAVObject* quill)
        {
            std::optional<RE::NiPoint3> tip;
            const auto toQuill = quill->world.Invert();
            RE::BSVisit::TraverseScenegraphGeometries(quill, [&](RE::BSGeometry* geometry) {
                auto* shape = geometry->AsTriShape();
                const auto* renderer = shape ? geometry->GetGeometryRuntimeData().rendererData : nullptr;
                const auto* raw = renderer ? renderer->rawVertexData : nullptr;
                if (!raw) return RE::BSVisit::BSVisitControl::kContinue;
                const auto desc = geometry->GetGeometryRuntimeData().vertexDesc;
                const auto stride = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(desc) & 0xF) * 4;
                const bool full = desc.HasFlag(RE::BSGraphics::Vertex::VF_UV) ? desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0) >= 16
                                                                             : desc.HasFlag(RE::BSGraphics::Vertex::VF_FULLPREC);
                const auto toModel = toQuill * geometry->world;
                for (std::uint32_t i = 0; i < shape->GetTrishapeRuntimeData().vertexCount; ++i) {
                    RE::NiPoint3 p;
                    if (full) {
                        std::memcpy(&p, raw + i * stride, sizeof(p));
                    } else {
                        std::uint16_t h[3];
                        std::memcpy(h, raw + i * stride, sizeof(h));
                        p = { QuillPaper::HalfToFloat(h[0]), QuillPaper::HalfToFloat(h[1]), QuillPaper::HalfToFloat(h[2]) };
                    }
                    p = toModel * p;
                    if (!tip || p.z < tip->z) tip = p;
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            if (tip) g_nib = *tip;
            SKSE::log::info("[Quill] Nib {} at ({:.3f}, {:.3f}, {:.3f}) in the model", tip ? "found" : "not found (a guess)", g_nib.x, g_nib.y,
                            g_nib.z);
        }

        // The model's collision would put it back where its physics body is on every update.
        void DropCollision(RE::NiAVObject* object)
        {
            if (!object) return;
            object->collisionObject.reset();
            if (auto* node = object->AsNode()) {
                for (const auto& child : node->GetChildren()) DropCollision(child.get());
            }
        }

        // PageText's vertices: their positions and UVs give where any point of the page texture is.
        PageQuad ReadQuad(RE::BSGeometry* geometry)
        {
            PageQuad quad;
            auto* shape = geometry ? geometry->AsTriShape() : nullptr;
            const auto* renderer = shape ? geometry->GetGeometryRuntimeData().rendererData : nullptr;
            const auto* raw = renderer ? renderer->rawVertexData : nullptr;
            if (!raw) {
                SKSE::log::warn("[Quill] PageText has no vertex data on the CPU (shape {})", shape != nullptr);
                return quad;
            }
            auto desc = geometry->GetGeometryRuntimeData().vertexDesc;
            if (!desc.HasFlag(RE::BSGraphics::Vertex::VF_UV)) {
                SKSE::log::warn("[Quill] PageText has no UVs");
                return quad;
            }
            // The descriptor's low nibble: the stride in 4 bytes (GetSize counts a position as 16 even when it's halves).
            const auto stride = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(desc) & 0xF) * 4;
            const auto uvAt = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
            // The UVs follow the position: 16 bytes after it are floats (x, y, z, w), 8 halves.
            const bool full = uvAt >= 16;
            const auto count = shape->GetTrishapeRuntimeData().vertexCount;
            float best[4] = { FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };  // min u+v, max u+v, max u-v, max v-u
            quad.u0 = quad.v0 = FLT_MAX;
            quad.u1 = quad.v1 = -FLT_MAX;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto* vertex = raw + i * stride;
                RE::NiPoint3 p;
                if (full) {
                    std::memcpy(&p, vertex, sizeof(p));
                } else {
                    std::uint16_t h[3];
                    std::memcpy(h, vertex, sizeof(h));
                    p = { QuillPaper::HalfToFloat(h[0]), QuillPaper::HalfToFloat(h[1]), QuillPaper::HalfToFloat(h[2]) };
                }
                std::uint16_t uv[2];
                std::memcpy(uv, vertex + uvAt, sizeof(uv));
                const float u = QuillPaper::HalfToFloat(uv[0]), v = QuillPaper::HalfToFloat(uv[1]);
                SKSE::log::info("[Quill] PageText vertex {}: ({:.3f}, {:.3f}, {:.3f}) uv ({:.3f}, {:.3f}); stride {}, uv at {}", i, p.x,
                                p.y, p.z, u, v, stride, uvAt);
                quad.u0 = std::min(quad.u0, u), quad.u1 = std::max(quad.u1, u);
                quad.v0 = std::min(quad.v0, v), quad.v1 = std::max(quad.v1, v);
                if (u + v < best[0]) best[0] = u + v, quad.p00 = p;
                if (u + v > best[1]) best[1] = u + v, quad.p11 = p;
                if (u - v > best[2]) best[2] = u - v, quad.p10 = p;
                if (v - u > best[3]) best[3] = v - u, quad.p01 = p;
            }
            quad.valid = count >= 4 && quad.u1 > quad.u0 && quad.v1 > quad.v0;
            return quad;
        }

        // The caret, from book.swf: "side,page,x,y,gx,gy" (EditCaretPoint).
        std::string CaretPoint()
        {
            RE::GFxValue point;
            return Book::Call("EditCaretPoint", &point) && point.IsString() ? point.GetString() : "unknown";
        }

        // The page's text quad: the quill's pose is in its space, so it sits the same on any book model.
        RE::NiAVObject* Page()
        {
            auto* menu = Menu();
            return menu ? menu->GetRuntimeData().pageTextGeo.get() : nullptr;
        }

        // A point on the stage, on the page in its space.  The engine sets the movie's viewport to PageText's UV range
        // (BookMenu setup, AE 0x1408f1ed0) and draws the texture on the "Base Page" sheets: the point is where the
        // caret's slot's sheet shows that UV, as it's bent now.  Without one, on PageText's flat quad.
        std::optional<RE::NiPoint3> OnPage(int slot, float gx, float gy)
        {
            const float width = g_visible.right - g_visible.left, height = g_visible.bottom - g_visible.top;
            if (!g_quad.valid || width <= 0.f || height <= 0.f) return std::nullopt;
            const auto& q = g_quad;
            const float u = q.u0 + (gx - g_visible.left) / width * (q.u1 - q.u0), v = q.v0 + (gy - g_visible.top) / height * (q.v1 - q.v0);
            g_uv = RE::NiPoint2{ u, v };
            if (const auto onSheet = QuillPaper::At(slot, u, v); onSheet && Page()) {
                // On the page, or not used: a stray point once put the quill thousands of units off and hid the book.
                const auto point = Page()->world.Invert() * *onSheet;
                if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && std::abs(point.z) < 5.f) return point;
                static bool said = false;
                if (!std::exchange(said, true)) {
                    SKSE::log::warn("[Quill] The sheet's point ({}, {}, {}) is off the page: the flat quad instead", point.x, point.y, point.z);
                }
            }
            // The page shows the texture turned half round on the quad (seen in game): both run the other way.
            const float s = std::clamp(1.f - (gx - g_visible.left) / width, 0.f, 1.f);
            const float t = std::clamp(1.f - (gy - g_visible.top) / height, 0.f, 1.f);
            const auto top = q.p00 * (1.f - s) + q.p10 * s, bottom = q.p01 * (1.f - s) + q.p11 * s;
            return top * (1.f - t) + bottom * t;
        }

        // Where the caret is on the page.
        void FindAnchor(RE::NiAVObject* page)
        {
            g_caret = CaretPoint();
            float side = 0.f, number = 0.f, x = 0.f, y = 0.f, gx = 0.f, gy = 0.f, slot = 0.f;
            std::optional<RE::NiPoint3> point;
            g_stagePoint.reset();
            g_uv.reset();
            if (std::sscanf(g_caret.c_str(), "%f,%f,%f,%f,%f,%f,%f", &side, &number, &x, &y, &gx, &gy, &slot) >= 6) {
                g_stagePoint = RE::NiPoint2{ gx, gy };
                g_slot = static_cast<int>(slot);
                point = OnPage(g_slot, gx, gy);
            }
            g_anchor = point ? *point : page->world.Invert() * page->worldBound.center;
        }

        // The book menu's 3D scene camera (UI3DSceneManager's: the book menu places the book with it).
        RE::NiCamera* Camera()
        {
            auto* scene = RE::UI3DSceneManager::GetSingleton();
            return scene ? scene->camera.get() : nullptr;
        }

        // The page's centre on the paper: the pose is the quill's place with the caret there.
        // The caret's slot's sheet at texture point (u, v), in the page's space, if it's on the page.
        std::optional<RE::NiPoint3> SheetPoint(float u, float v)
        {
            const auto onSheet = QuillPaper::At(g_slot, u, v);
            if (!onSheet || !Page()) return std::nullopt;
            const auto point = Page()->world.Invert() * *onSheet;
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) || std::abs(point.z) >= 5.f) return std::nullopt;
            return point;
        }

        RE::NiPoint2 CentreUv() { return { (g_quad.u0 + g_quad.u1) / 2.f, (g_quad.v0 + g_quad.v1) / 2.f }; }

        RE::NiPoint3 PageCentre()
        {
            const auto uv = CentreUv();
            if (const auto point = SheetPoint(uv.x, uv.y)) return *point;
            return (g_quad.p00 + g_quad.p11) / 2.f;
        }

        // The spot `beside` (page units, from the centre's point) as a texture offset there, so it bends with the paper.
        std::optional<RE::NiPoint2> UvOffset(const RE::NiPoint3& beside)
        {
            constexpr float kStep = 0.01f;
            const auto uv = CentreUv();
            const auto at = SheetPoint(uv.x, uv.y), du = SheetPoint(uv.x + kStep, uv.y), dv = SheetPoint(uv.x, uv.y + kStep);
            if (!at || !du || !dv) return std::nullopt;
            const auto ju = (*du - *at) / kStep, jv = (*dv - *at) / kStep;  // page units per texture unit
            const float det = ju.x * jv.y - jv.x * ju.y;
            if (std::abs(det) < 1e-6f) return std::nullopt;
            return RE::NiPoint2{ (beside.x * jv.y - jv.x * beside.y) / det, (ju.x * beside.y - beside.x * ju.y) / det };
        }

        // Where the quill is on the page for the caret now.  The nib rides the camera's ray through a point beside the
        // caret's (on the paper, at the caret's depth), at the depth the pose gives it: its image stays on the caret
        // wherever the paper bends (docs/EDITOR.md#quill-cursor).
        RE::NiPoint3 Translate()
        {
            if (g_track.valid) {
                if (!g_stagePoint) return g_pose.translate;
                return g_pose.translate +
                       RE::NiPoint3{ g_track.kx * (g_stagePoint->x - g_track.gx), g_track.ky * (g_stagePoint->y - g_track.gy), 0.f };
            }
            auto* page = Page();
            auto* camera = Camera();
            const auto centre = PageCentre();
            if (!page || !camera || camera->GetRuntimeData2().viewFrustum.bOrtho) return g_anchor + g_pose.translate;
            const auto eye = page->world.Invert() * camera->world.translate;
            const auto nibOffset = g_pose.rotate * g_nib * g_pose.scale;
            const auto nibThere = centre + g_pose.translate + nibOffset;  // with the caret at the centre
            // Where the ray through the nib meets the paper's plane there, and how far along it the nib is.
            const float run = nibThere.z - eye.z;
            if (std::abs(run) < 1e-4f) return g_anchor + g_pose.translate;
            const float t = (centre.z - eye.z) / run;
            if (t <= 1.f) return g_anchor + g_pose.translate;
            const auto beside = (eye + (nibThere - eye) * t) - centre;  // the nib's spot on the paper, from the caret's
            // The spot keeps its place against the text: the same texture offset from the caret's, on the bent paper.
            auto spot = g_anchor + beside;
            if (const auto offset = g_uv ? UvOffset(beside) : std::nullopt) {
                if (const auto onSheet = SheetPoint(g_uv->x + offset->x, g_uv->y + offset->y)) spot = *onSheet;
            }
            const auto nib = eye + (spot - eye) / t;
            return nib - nibOffset;
        }

        // Numpad 1: the nib lined up on the caret here.  The second, far from the first, calibrates tracking.
        void CalibratePoint()
        {
            if (!g_stagePoint) {
                SKSE::log::info("[Quill] Calibrate: no caret point ({})", g_caret);
                return;
            }
            const auto here = Translate();
            if (!g_firstPoint) {
                g_firstPoint = { *g_stagePoint, here };
                SKSE::log::info("[Quill] Calibrate: point 1 at ({}, {})", g_stagePoint->x, g_stagePoint->y);
                RE::SendHUDMessage::ShowHUDMessage("Quill point 1 set: move the caret right and down, line the nib up, numpad 1");
                return;
            }
            const auto [from, at] = *g_firstPoint;
            const float dx = g_stagePoint->x - from.x, dy = g_stagePoint->y - from.y;
            if (std::abs(dx) < 30.f || std::abs(dy) < 30.f) {
                SKSE::log::info("[Quill] Calibrate: point 2 at ({}, {}) is too close to point 1", g_stagePoint->x, g_stagePoint->y);
                RE::SendHUDMessage::ShowHUDMessage("Quill point 2 too close: move the caret further right and down");
                return;
            }
            g_track = { true, from.x, from.y, (here.x - at.x) / dx, (here.y - at.y) / dy };
            g_pose.translate = { at.x, at.y, here.z };
            g_firstPoint.reset();
            SKSE::log::info("[Quill] Tracking calibrated: from ({}, {}), {} and {} per stage unit", g_track.gx, g_track.gy, g_track.kx,
                            g_track.ky);
            RE::SendHUDMessage::ShowHUDMessage("Quill tracking calibrated: numpad Enter saves");
        }

        void Apply()
        {
            auto* page = Page();
            if (!g_parent || !g_quill || !page) return;
            FindAnchor(page);
            RE::NiTransform onPage;
            onPage.translate = Translate();
            if (!std::isfinite(onPage.translate.x) || !std::isfinite(onPage.translate.y) || !std::isfinite(onPage.translate.z)) return;
            onPage.rotate = g_pose.rotate;
            onPage.scale = g_pose.scale;
            g_quill->local = g_parent->world.Invert() * (page->world * onPage);
            RE::NiUpdateData update{ 0.f, RE::NiUpdateData::Flag::kDisableCollision };
            g_quill->Update(update);
            g_quill->world = g_parent->world * g_quill->local;
            g_quill->UpdateDownwardPass(update, 0);
        }

        void LogPose(const std::string& what)
        {
            const auto& t = g_pose.translate;
            const auto& r = g_pose.rotate.entry;
            SKSE::log::info("[Quill] {} pos ({:.3f}, {:.3f}, {:.3f}) scale {:.4f} rot [{:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}; "
                            "{:.4f} {:.4f} {:.4f}] caret {}",
                            what, t.x, t.y, t.z, g_pose.scale, r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0],
                            r[2][1], r[2][2], g_caret);
            SKSE::log::info("[Quill]   caret's point on the page ({:.3f}, {:.3f}, {:.3f}), slot {}", g_anchor.x, g_anchor.y, g_anchor.z,
                            g_slot);
            // Where the scene camera puts them on screen (0-1), to compare with a screenshot.
            auto* camera = Camera();
            auto* page = Page();
            if (!camera || !page || !g_quill) return;
            const auto screen = [camera](const RE::NiPoint3& world) {
                float x = 0.f, y = 0.f, z = 0.f;
                camera->WorldPtToScreenPt3(world, x, y, z, 1e-5f);
                return std::format("({:.4f}, {:.4f})", x, y);
            };
            const auto& w = page->world;
            SKSE::log::info("[Quill]   on screen: caret {} nib {}; page corners uv00 {} uv10 {} uv01 {} uv11 {}", screen(w * g_anchor),
                            screen(g_quill->world * g_nib), screen(w * g_quad.p00), screen(w * g_quad.p10), screen(w * g_quad.p01),
                            screen(w * g_quad.p11));
        }

        // A move or turn along a world axis (the camera's: x across, y toward it, z up), in the page's space.
        void Move(const RE::NiPoint3& world)
        {
            const auto& page = Page()->world;
            g_pose.translate += page.rotate.Transpose() * world / page.scale;
        }

        void Turn(const RE::NiPoint3& axis, float degrees)
        {
            RE::NiMatrix3 turn;
            turn.MakeRotation(degrees * std::numbers::pi_v<float> / 180.f, axis);
            const auto& page = Page()->world.rotate;
            g_pose.rotate = page.Transpose() * turn * page * g_pose.rotate;
        }

        // The start: on the caret, a third of the page's size, lifted off the text toward the camera (+y)
        // so none of it is inside the book.
        void ResetPose()
        {
            auto* page = Page();
            if (!page) return;
            const auto& world = page->world;
            const float scale = g_size > 0.f ? page->worldBound.radius / 3.f / g_size / world.scale : 1.f;
            g_pose.translate = world.rotate.Transpose() * RE::NiPoint3{ 0.f, g_size * scale, 0.f };
            g_pose.rotate = world.rotate.Transpose();  // upright to the camera, as the model is
            g_pose.scale = scale;
        }

    }

    void Show()
    {
        Hide();
        if (!Settings::QuillAdjust()) return;  // deferred past 1.0: development only (docs/EDITOR.md#quill-cursor)
        auto* menu = Menu();
        if (!menu) return;
        auto& data = menu->GetRuntimeData();
        auto* book = data.bookModel ? data.bookModel->AsNode() : nullptr;
        auto* item = RE::TESForm::LookupByID<RE::TESObjectMISC>(kQuill);
        if (!book || !data.pageTextGeo || !item || !item->GetModel() || !*item->GetModel()) {
            SKSE::log::warn("[Quill] No book model, page or quill model");
            return;
        }
        RE::NiPointer<RE::NiNode> model;
        const RE::BSModelDB::DBTraits::ArgsType args{};
        if (RE::BSModelDB::Demand(item->GetModel(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
            SKSE::log::warn("[Quill] Can't load {}", item->GetModel());
            return;
        }
        // The model database shares its copy, which comes hidden: the book gets its own, shown.
        auto* clone = model->Clone();
        auto* quill = clone ? clone->AsNode() : nullptr;
        if (!quill) return;
        quill->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
        DropCollision(quill);
        quill->local = {};
        RE::NiUpdateData update{};
        quill->Update(update);  // its own size, before it's on the book
        g_size = quill->worldBound.radius;
        FindNib(quill);
        book->AttachChild(quill);
        g_parent.reset(book);
        g_quill.reset(quill);
        g_model = data.isNote ? "Note" : "Book";
        g_quad = ReadQuad(data.pageTextGeo.get());
        QuillPaper::Read(book);
        g_visible = data.book ? data.book->GetVisibleFrameRect() : RE::GRectF{};
        SKSE::log::info("[Quill] Visible stage ({}, {}) to ({}, {}); quad {}, UVs u {:.3f}-{:.3f} v {:.3f}-{:.3f}", g_visible.left,
                        g_visible.top, g_visible.right, g_visible.bottom, g_quad.valid ? "read" : "not read", g_quad.u0, g_quad.u1,
                        g_quad.v0, g_quad.v1);
        g_track = {};
        g_firstPoint.reset();
        if (!LoadPose()) ResetPose();
        Apply();
        Log("book", book);
        Log("page", data.pageTextGeo.get());
        const auto inBook = book->world.Invert() * data.pageTextGeo->world;
        const auto& r = inBook.rotate.entry;
        SKSE::log::info("[Quill] page in the book: ({:.4f}, {:.4f}, {:.4f}) x{:.4f} rot [{:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}]",
                        inBook.translate.x, inBook.translate.y, inBook.translate.z, inBook.scale, r[0][0], r[0][1], r[0][2], r[1][0],
                        r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
        LogPose(std::format("on '{}' (note {}):", book->name.c_str(), data.isNote));
        if (auto* camera = Camera()) {
            const auto& w = camera->world;
            const auto& f = camera->GetRuntimeData2().viewFrustum;
            const auto onPage = data.pageTextGeo->world.Invert() * w.translate;
            SKSE::log::info("[Quill] camera '{}': world ({:.2f}, {:.2f}, {:.2f}) rot [{:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} "
                            "{:.4f}]; frustum l {:.4f} r {:.4f} t {:.4f} b {:.4f} near {:.2f} far {:.2f} ortho {}; on the page ({:.2f}, "
                            "{:.2f}, {:.2f})",
                            camera->name.c_str(), w.translate.x, w.translate.y, w.translate.z, w.rotate.entry[0][0], w.rotate.entry[0][1],
                            w.rotate.entry[0][2], w.rotate.entry[1][0], w.rotate.entry[1][1], w.rotate.entry[1][2], w.rotate.entry[2][0],
                            w.rotate.entry[2][1], w.rotate.entry[2][2], f.fLeft, f.fRight, f.fTop, f.fBottom, f.fNear, f.fFar, f.bOrtho,
                            onPage.x, onPage.y, onPage.z);
        } else {
            SKSE::log::warn("[Quill] No 3D scene camera");
        }
    }

    void Follow() { Apply(); }

    void Hide()
    {
        if (g_parent && g_quill) g_parent->DetachChild(g_quill.get());
        g_quill.reset();
        g_parent.reset();
        QuillPaper::Clear();
    }

    bool Adjust(std::uint32_t scanCode)
    {
        if (!Settings::QuillAdjust() || !g_parent || !g_quill || !Page()) return false;
        // Development only: the HUD text isn't translated.
        using namespace Keys;
        // Numpad: 4/6 x, 2/8 z (up), 7/9 y (toward the camera); "/" switches moving and turning; 1 calibrates.
        RE::NiPoint3 axis;
        float sign = 1.f;
        switch (scanCode) {
        case kNumpad4: axis = { 1.f, 0.f, 0.f }; sign = -1.f; break;
        case kNumpad6: axis = { 1.f, 0.f, 0.f }; break;
        case kNumpad2: axis = { 0.f, 0.f, 1.f }; sign = -1.f; break;
        case kNumpad8: axis = { 0.f, 0.f, 1.f }; break;
        case kNumpad7: axis = { 0.f, 1.f, 0.f }; sign = -1.f; break;
        case kNumpad9: axis = { 0.f, 1.f, 0.f }; break;
        case kNumpadPlus: g_pose.scale *= kScaleSteps[g_step]; break;
        case kNumpadMinus: g_pose.scale /= kScaleSteps[g_step]; break;
        case kNumpad0: ResetPose(); break;
        case kNumpadSlash:
            g_turning = !g_turning;
            SKSE::log::info("[Quill] Adjust: {}", g_turning ? "turning" : "moving");
            return true;
        case kNumpadStar:
            g_step = (g_step + 1) % kSteps;
            SKSE::log::info("[Quill] Adjust: step {} units, {} degrees", kMoveSteps[g_step], kTurnSteps[g_step]);
            return true;
        case kKeypadEnter:
            SavePose();
            LogPose("saved:");
            RE::SendHUDMessage::ShowHUDMessage(std::format("Quill pose saved for {}", g_model).c_str());
            return true;
        case kNumpadDot:
            if (!LoadPose()) {
                SKSE::log::info("[Quill] No saved pose for {}", g_model);
                RE::SendHUDMessage::ShowHUDMessage(std::format("No quill pose saved for {}", g_model).c_str());
                return true;
            }
            RE::SendHUDMessage::ShowHUDMessage(std::format("Quill pose restored for {}", g_model).c_str());
            break;
        case kNumpad5:
            LogPose(std::format("sample {}:", ++g_samples));
            RE::SendHUDMessage::ShowHUDMessage(std::format("Quill sample {} logged", g_samples).c_str());
            return true;
        case kNumpad1:
            CalibratePoint();
            return true;
        default:
            return scanCode == kNumpad3;  // does nothing (it would type)
        }
        if (axis.x != 0.f || axis.y != 0.f || axis.z != 0.f) {
            if (g_turning) {
                Turn(axis, sign * kTurnSteps[g_step]);
            } else {
                Move(axis * (sign * kMoveSteps[g_step]));
            }
        }
        Apply();
        LogPose("adjusted:");
        Log("quill", g_quill.get());
        Log("book", g_parent.get());
        return true;
    }

}
