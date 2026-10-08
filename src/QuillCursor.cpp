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

        // The pose against the camera, the same on screen on any book or note: the nib's spot from the caret's (stage units),
        // its height above it toward the camera, the rotation in the camera's space, the world scale (docs/EDITOR.md).
        struct View {
            float dx = 0.f, dy = 0.f, hover = 0.f, scale = 1.f;
            RE::NiMatrix3 rotate;
        };
        std::optional<View> g_view;

        // The pose tuned in game (2026-10-07, a letter; books look right with it too): the INI's, if any, is used instead.
        const View kDefaultView{ 0.0677f, -7.877f, 6.116f, 0.7578f,
                                 RE::NiMatrix3{ RE::NiPoint3{ -0.40820244f, -0.48276395f, -0.77473265f },
                                                RE::NiPoint3{ 0.25884753f, -0.87511283f, 0.40892613f },
                                                RE::NiPoint3{ -0.8754f, -0.0336f, 0.4822f } } };

        int g_still = 0;           // frames the page hasn't moved (the quill shows once it's in place)
        int g_waited = 0;          // frames since the quill was made, until it's placed
        constexpr int kStillFrames = 10;     // the page still this long, the caret's point on the paper: the quill shows
        constexpr int kStillAnyway = 30;     // ... or this long, without the paper
        constexpr int kPlaceTimeout = 120;   // not shown by then: the real caret comes back (logged)
        RE::NiPoint3 g_lastPage;   // the page's world position last frame
        RE::NiPoint3 g_lastAnchor; // the caret's point last frame: it moves with a turning sheet, the page doesn't
        bool g_caretOnSheet = false;  // the caret's point did
        bool g_placed = false;     // the quill has been shown: it stays hidden until it's in place
        bool g_caretHidden = false;  // book.swf draws no caret: the quill is it (from the start, not once it's placed)
        std::string g_model;       // its pose's key in the INI: "Book" or "Note"

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

        // Adjust mode (Settings::kQuillAdjust): the numpad moves or turns the quill.
        bool g_turning = false;
        int g_step = 1;
        constexpr float kMoveSteps[] = { 0.02f, 0.1f, 0.5f, 2.f };     // world units (hover)
        constexpr float kStageSteps[] = { 0.5f, 2.5f, 12.5f, 50.f };   // stage units (the spot), about the same on a page
        constexpr float kTurnSteps[] = { 0.5f, 1.f, 5.f, 15.f };  // degrees
        constexpr float kScaleSteps[] = { 1.005f, 1.01f, 1.05f, 1.2f };
        constexpr int kSteps = static_cast<int>(std::size(kMoveSteps));
        int g_samples = 0;

        // A pose saved in adjust mode (numpad Enter), for books or notes, each the other's when it has none:
        // "dx dy hover scale" and the rotation's rows, under [QuillPose] in Ink & Quill's INI.  Else kDefaultView.
        constexpr auto kPoseSection = "QuillPose";

        std::optional<View> ReadView(const std::string& key)
        {
            char text[512] = {};
            GetPrivateProfileStringA(kPoseSection, key.c_str(), "", text, sizeof(text), Settings::IniPath().c_str());
            std::istringstream in(text);
            View view;
            auto& r = view.rotate.entry;
            if (!(in >> view.dx >> view.dy >> view.hover >> view.scale >> r[0][0] >> r[0][1] >> r[0][2] >> r[1][0] >> r[1][1] >> r[1][2] >>
                  r[2][0] >> r[2][1] >> r[2][2])) {
                return std::nullopt;
            }
            return view;
        }

        // True when the INI has one, else the built-in pose.
        bool LoadPose()
        {
            // This kind of book's, then the other kind's.
            for (const auto& key : { g_model, std::string(g_model == "Note" ? "Book" : "Note") }) {
                if ((g_view = ReadView(key))) return true;
            }
            g_view = kDefaultView;
            return false;
        }

        void SavePose()
        {
            if (!g_view) return;
            const auto& v = *g_view;
            const auto& r = v.rotate.entry;
            const auto text = std::format("{} {} {} {} {} {} {} {} {} {} {} {} {}", v.dx, v.dy, v.hover, v.scale, r[0][0], r[0][1], r[0][2],
                                          r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
            if (!WritePrivateProfileStringA(kPoseSection, g_model.c_str(), text.c_str(), Settings::IniPath().c_str())) {
                SKSE::log::error("[Quill] Couldn't write the pose to the INI");
            }
        }

        using Book::Menu;

        void Log(const char* what, RE::NiAVObject* object)
        {
            if (!object) return;
            const auto& w = object->world;
            const auto& b = object->worldBound;
            SKSE::log::debug("[Quill] {} '{}': world ({:.2f}, {:.2f}, {:.2f}) x{:.3f}, bound ({:.2f}, {:.2f}, {:.2f}) r{:.2f}",
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
                const auto stride = QuillPaper::Stride(desc);
                const bool full = desc.HasFlag(RE::BSGraphics::Vertex::VF_UV) ? desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0) >= 16
                                                                             : desc.HasFlag(RE::BSGraphics::Vertex::VF_FULLPREC);
                const auto toModel = toQuill * geometry->world;
                for (std::uint32_t i = 0; i < shape->GetTrishapeRuntimeData().vertexCount; ++i) {
                    const auto p = toModel * QuillPaper::Position(raw + i * stride, full);
                    if (!tip || p.z < tip->z) tip = p;
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            if (tip) g_nib = *tip;
            SKSE::log::info("[Quill] Nib {} at ({:.3f}, {:.3f}, {:.3f}) in the model", tip ? "found" : "not found (a guess)", g_nib.x, g_nib.y,
                            g_nib.z);
        }

        // The book menu's scene ignores alpha testing (HFs' feather card drew solid): such shapes are blended and write no
        // depth instead, on copies of their properties so the world's quills keep theirs.
        void BlendCutouts(RE::NiAVObject* quill)
        {
            RE::BSVisit::TraverseScenegraphGeometries(quill, [](RE::BSGeometry* geometry) {
                auto& data = geometry->GetGeometryRuntimeData();
                auto* alpha = data.alphaProperty.get();
                SKSE::log::debug("[Quill] Shape '{}': alpha {} (blending {}, testing {}, threshold {})", geometry->name.c_str(),
                                 alpha != nullptr, alpha && alpha->GetAlphaBlending(), alpha && alpha->GetAlphaTesting(),
                                 alpha ? alpha->alphaThreshold : 0);
                if (!alpha || !alpha->GetAlphaTesting() || alpha->GetAlphaBlending()) return RE::BSVisit::BSVisitControl::kContinue;
                const RE::NiPointer<RE::NiObject> alphaCopy{ alpha->Clone() };
                auto* blend = alphaCopy ? skyrim_cast<RE::NiAlphaProperty*>(alphaCopy.get()) : nullptr;
                if (!blend) return RE::BSVisit::BSVisitControl::kContinue;
                blend->SetAlphaBlending(true);
                blend->SetSrcBlendMode(RE::NiAlphaProperty::AlphaFunction::kSrcAlpha);
                blend->SetDestBlendMode(RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha);
                data.alphaProperty.reset(blend);
                if (const RE::NiPointer<RE::NiObject> shaderCopy{ data.shaderProperty ? data.shaderProperty->Clone() : nullptr }) {
                    if (auto* shader = skyrim_cast<RE::BSShaderProperty*>(shaderCopy.get())) {
                        shader->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kZBufferWrite, false);
                        data.shaderProperty.reset(shader);
                    }
                }
                SKSE::log::info("[Quill] Shape '{}' is alpha tested: blended instead", geometry->name.c_str());
                return RE::BSVisit::BSVisitControl::kContinue;
            });
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
            const auto stride = QuillPaper::Stride(desc);  // not GetSize: it counts a position as 16 even when it's halves
            const auto uvAt = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
            // The UVs follow the position: 16 bytes after it are floats (x, y, z, w), 8 halves.
            const bool full = uvAt >= 16;
            const auto count = shape->GetTrishapeRuntimeData().vertexCount;
            float best[4] = { FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };  // min u+v, max u+v, max u-v, max v-u
            quad.u0 = quad.v0 = FLT_MAX;
            quad.u1 = quad.v1 = -FLT_MAX;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto* vertex = raw + i * stride;
                const auto p = QuillPaper::Position(vertex, full);
                std::uint16_t uv[2];
                std::memcpy(uv, vertex + uvAt, sizeof(uv));
                const float u = QuillPaper::HalfToFloat(uv[0]), v = QuillPaper::HalfToFloat(uv[1]);
                SKSE::log::debug("[Quill] PageText vertex {}: ({:.3f}, {:.3f}, {:.3f}) uv ({:.3f}, {:.3f}); stride {}, uv at {}", i, p.x,
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

        // The caret, from book.swf: "side,page,x,y,gx,gy,slot", or "" with none on a shown page (EditCaretPoint).
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

        // A stage point on the page, in its space: where the slot's sheet shows its UV, as it's bent now, else on PageText's
        // flat quad (docs/EDITOR.md#quill-cursor).
        struct PagePoint {
            RE::NiPoint3 point;
            bool onSheet = false;  // from the paper, not the flat quad
        };

        std::optional<PagePoint> OnPage(int slot, float gx, float gy)
        {
            const float width = g_visible.right - g_visible.left, height = g_visible.bottom - g_visible.top;
            if (!g_quad.valid || width <= 0.f || height <= 0.f) return std::nullopt;
            const auto& q = g_quad;
            const float u = q.u0 + (gx - g_visible.left) / width * (q.u1 - q.u0), v = q.v0 + (gy - g_visible.top) / height * (q.v1 - q.v0);
            if (const auto onSheet = QuillPaper::At(slot, u, v); onSheet && Page()) {
                // On the page, or not used: a stray point once put the quill thousands of units off and hid the book.
                const auto point = Page()->world.Invert() * *onSheet;
                if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && std::abs(point.z) < 5.f) {
                    return PagePoint{ point, true };
                }
                static bool said = false;
                if (!std::exchange(said, true)) {
                    SKSE::log::warn("[Quill] The sheet's point ({}, {}, {}) is off the page: the flat quad instead", point.x, point.y, point.z);
                }
            }
            // The page shows the texture turned half round on the quad (seen in game): both run the other way.
            const float s = std::clamp(1.f - (gx - g_visible.left) / width, 0.f, 1.f);
            const float t = std::clamp(1.f - (gy - g_visible.top) / height, 0.f, 1.f);
            const auto top = q.p00 * (1.f - s) + q.p10 * s, bottom = q.p01 * (1.f - s) + q.p11 * s;
            return PagePoint{ top * (1.f - t) + bottom * t, false };
        }

        // Where the caret is on the page.
        void FindAnchor(RE::NiAVObject* page)
        {
            g_caret = CaretPoint();
            float side = 0.f, number = 0.f, x = 0.f, y = 0.f, gx = 0.f, gy = 0.f, slot = 0.f;
            std::optional<RE::NiPoint3> point;
            g_stagePoint.reset();
            g_caretOnSheet = false;
            if (std::sscanf(g_caret.c_str(), "%f,%f,%f,%f,%f,%f,%f", &side, &number, &x, &y, &gx, &gy, &slot) >= 6) {
                g_stagePoint = RE::NiPoint2{ gx, gy };
                g_slot = static_cast<int>(slot);
                if (const auto at = OnPage(g_slot, gx, gy)) {
                    point = at->point;
                    g_caretOnSheet = at->onSheet;
                }
            }
            g_anchor = point ? *point : page->world.Invert() * page->worldBound.center;
        }

        // The book menu's 3D scene camera (UI3DSceneManager's: the book menu places the book with it).
        RE::NiCamera* Camera()
        {
            auto* scene = RE::UI3DSceneManager::GetSingleton();
            return scene ? scene->camera.get() : nullptr;
        }

        bool Perspective(RE::NiCamera* camera) { return camera && !camera->GetRuntimeData2().viewFrustum.bOrtho; }

        // The quill in the world for a View: the nib `hover` above the spot beside the caret's, toward the camera.
        std::optional<RE::NiTransform> ViewWorld(const View& view)
        {
            auto* page = Page();
            auto* camera = Camera();
            if (!page || !camera) return std::nullopt;
            const auto spot = g_stagePoint ? OnPage(g_slot, g_stagePoint->x + view.dx, g_stagePoint->y + view.dy) : std::nullopt;
            const auto spotWorld = page->world * (spot ? spot->point : g_anchor);
            auto up = camera->world.translate - spotWorld;
            const float length = up.Length();
            if (!Perspective(camera) || length < 1e-3f) up = camera->world.rotate * RE::NiPoint3{ -1.f, 0.f, 0.f };  // its look is +x
            else up = up / length;
            RE::NiTransform world;
            world.rotate = camera->world.rotate * view.rotate;
            world.scale = view.scale;
            world.translate = spotWorld + up * view.hover - world.rotate * (g_nib * view.scale);
            return world;
        }

        // The quill isn't placed in time: the real caret back, and why, once.
        void RestoreCaret()
        {
            if (std::exchange(g_caretHidden, false)) Book::Call("EditHideCaret", nullptr, "0");
            const char* why = !Camera() ? "no 3D scene camera" : g_still < kStillAnyway ? "the book kept moving" : "its place couldn't be worked out";
            SKSE::log::info("[Quill] Not shown after {} frames ({}): the real caret instead until it is", kPlaceTimeout, why);
        }

        void Apply()
        {
            auto* page = Page();
            if (!g_parent || !g_quill || !page) return;
            FindAnchor(page);
            const bool still = (page->world.translate - g_lastPage).Length() < 1e-3f && (g_anchor - g_lastAnchor).Length() < 1e-3f;
            g_lastPage = page->world.translate;
            g_lastAnchor = g_anchor;
            g_still = still ? g_still + 1 : 0;
            // No caret, or one on a page that isn't shown (paging to a title page): no quill either, and once the caret is
            // back it waits again for the page to stop moving (it turns).  Waiting there doesn't count toward the timeout.
            if (!g_stagePoint) {
                if (std::exchange(g_placed, false)) g_quill->GetFlags().set(RE::NiAVObject::Flag::kHidden);
                return;
            }
            if (!g_placed && ++g_waited == kPlaceTimeout) RestoreCaret();
            const auto world = g_view ? ViewWorld(*g_view) : std::nullopt;
            if (!world) return;
            const auto& t = world->translate;
            if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) return;
            g_quill->local = g_parent->world.Invert() * *world;
            RE::NiUpdateData update{ 0.f, RE::NiUpdateData::Flag::kDisableCollision };
            g_quill->Update(update);
            g_quill->world = g_parent->world * g_quill->local;
            g_quill->UpdateDownwardPass(update, 0);
            // Shown once the book has stopped moving (until then it jumps: the book opening, the caret's point not yet found).
            if (!g_placed && ((g_still >= kStillFrames && g_caretOnSheet) || g_still >= kStillAnyway)) g_placed = true;
            if (!g_placed) return;
            g_quill->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
            if (!g_caretHidden && !Settings::QuillAdjust()) g_caretHidden = Book::Call("EditHideCaret", nullptr, "1");
        }

        void LogPose(const std::string& what, bool info = true)
        {
            const auto level = info ? spdlog::level::info : spdlog::level::debug;
            if (g_view) {
                const auto& v = *g_view;
                const auto& r = v.rotate.entry;
                spdlog::log(level, "[Quill] {} spot ({:.2f}, {:.2f}) hover {:.3f} scale {:.4f} rot [{:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}; "
                                "{:.4f} {:.4f} {:.4f}] caret {}",
                                what, v.dx, v.dy, v.hover, v.scale, r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1],
                                r[2][2], g_caret);
            }
            SKSE::log::debug("[Quill]   caret's point on the page ({:.3f}, {:.3f}, {:.3f}), slot {}", g_anchor.x, g_anchor.y, g_anchor.z,
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
            SKSE::log::debug("[Quill]   on screen: caret {} nib {}; page corners uv00 {} uv10 {} uv01 {} uv11 {}", screen(w * g_anchor),
                            screen(g_quill->world * g_nib), screen(w * g_quad.p00), screen(w * g_quad.p10), screen(w * g_quad.p01),
                            screen(w * g_quad.p11));
        }

        // The start: on the caret, upright to the camera, a third of the page's size, floating its own size above it.
        void ResetView()
        {
            auto* page = Page();
            auto* camera = Camera();
            if (!page || !camera) return;
            View view;
            view.scale = g_size > 0.f ? page->worldBound.radius / 3.f / g_size : 1.f;
            view.hover = g_size * view.scale;
            view.rotate = camera->world.rotate.Transpose();  // the model's own axes in the world
            g_view = view;
        }

    }

    void Show()
    {
        Hide();
        if (!Settings::QuillCursor() && !Settings::QuillAdjust()) return;
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
        // The quill's own model: a replacer's, or Model Swapper's variant, when one is installed.
        if (RE::BSModelDB::Demand(item->GetModel(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
            SKSE::log::warn("[Quill] Can't load {}", item->GetModel());
            return;
        }
        // The model database shares its copy: the book gets its own.
        auto* clone = model->Clone();
        auto* quill = clone ? clone->AsNode() : nullptr;
        if (!quill) return;
        quill->GetFlags().set(RE::NiAVObject::Flag::kHidden);  // until it's in place (Apply)
        DropCollision(quill);
        BlendCutouts(quill);
        quill->local = {};
        RE::NiUpdateData update{};
        quill->Update(update);  // its own size, before it's on the book
        g_size = quill->worldBound.radius;
        FindNib(quill);
        book->AttachChild(quill);
        g_parent.reset(book);
        g_quill.reset(quill);
        g_placed = false;
        g_waited = 0;
        // The quill is the caret: none from the start, so it doesn't blink before the quill is in place.  Adjust mode
        // keeps the real one, to line the nib up against.
        if (!Settings::QuillAdjust()) g_caretHidden = Book::Call("EditHideCaret", nullptr, "1");
        g_still = 0;
        g_model = data.isNote ? "Note" : "Book";
        g_quad = ReadQuad(data.pageTextGeo.get());
        QuillPaper::Read(book);
        g_visible = data.book ? data.book->GetVisibleFrameRect() : RE::GRectF{};
        SKSE::log::debug("[Quill] Visible stage ({}, {}) to ({}, {}); quad {}, UVs u {:.3f}-{:.3f} v {:.3f}-{:.3f}", g_visible.left,
                        g_visible.top, g_visible.right, g_visible.bottom, g_quad.valid ? "read" : "not read", g_quad.u0, g_quad.u1,
                        g_quad.v0, g_quad.v1);
        LoadPose();
        Apply();
        Log("book", book);
        Log("page", data.pageTextGeo.get());
        const auto inBook = book->world.Invert() * data.pageTextGeo->world;
        const auto& r = inBook.rotate.entry;
        SKSE::log::debug("[Quill] page in the book: ({:.4f}, {:.4f}, {:.4f}) x{:.4f} rot [{:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}]",
                        inBook.translate.x, inBook.translate.y, inBook.translate.z, inBook.scale, r[0][0], r[0][1], r[0][2], r[1][0],
                        r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
        LogPose(std::format("on '{}' (note {}):", book->name.c_str(), data.isNote));
        if (auto* camera = Camera()) {
            const auto& w = camera->world;
            const auto& f = camera->GetRuntimeData2().viewFrustum;
            const auto onPage = data.pageTextGeo->world.Invert() * w.translate;
            SKSE::log::debug("[Quill] camera '{}': world ({:.2f}, {:.2f}, {:.2f}) rot [{:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} {:.4f}; {:.4f} {:.4f} "
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
        g_placed = false;
        if (std::exchange(g_caretHidden, false)) Book::Call("EditHideCaret", nullptr, "0");
        g_quill.reset();
        g_parent.reset();
        QuillPaper::Clear();
    }

    bool Adjust(std::uint32_t scanCode)
    {
        if (!Settings::QuillAdjust() || !g_parent || !g_quill || !Page()) return false;
        // Development only: the HUD text isn't translated.
        using namespace Keys;
        // Numpad, against the screen: 4/6 across, 2/8 up and down, 7/9 toward the camera and away; "/" switches moving
        // and turning (about the same axes in the world: x across, y toward the camera, z up).
        if (!g_view) ResetView();
        if (!g_view) return true;
        auto& view = *g_view;
        RE::NiPoint3 axis;
        float sign = 1.f;
        switch (scanCode) {
        case kNumpad4: axis = { 1.f, 0.f, 0.f }; break;
        case kNumpad6: axis = { 1.f, 0.f, 0.f }; sign = -1.f; break;
        case kNumpad2: axis = { 0.f, 0.f, 1.f }; sign = -1.f; break;
        case kNumpad8: axis = { 0.f, 0.f, 1.f }; break;
        case kNumpad7: axis = { 0.f, 1.f, 0.f }; sign = -1.f; break;
        case kNumpad9: axis = { 0.f, 1.f, 0.f }; break;
        case kNumpadPlus: view.scale *= kScaleSteps[g_step]; break;
        case kNumpadMinus: view.scale /= kScaleSteps[g_step]; break;
        case kNumpad0: ResetView(); break;
        case kNumpadSlash:
            g_turning = !g_turning;
            SKSE::log::info("[Quill] Adjust: {}", g_turning ? "turning" : "moving");
            return true;
        case kNumpadStar:
            g_step = (g_step + 1) % kSteps;
            SKSE::log::info("[Quill] Adjust: step {} stage units, {} units toward the camera, {} degrees", kStageSteps[g_step],
                            kMoveSteps[g_step], kTurnSteps[g_step]);
            return true;
        case kKeypadEnter:
            SavePose();
            LogPose("saved:");
            RE::SendHUDMessage::ShowHUDMessage(std::format("Quill pose saved for {}", g_model).c_str());
            return true;
        case kNumpadDot:
            RE::SendHUDMessage::ShowHUDMessage(LoadPose() ? std::format("Quill pose restored for {}", g_model).c_str()
                                                          : "No quill pose saved: the built-in one");
            break;
        case kNumpad5:
            LogPose(std::format("sample {}:", ++g_samples));
            RE::SendHUDMessage::ShowHUDMessage(std::format("Quill sample {} logged", g_samples).c_str());
            return true;
        default:
            return scanCode == kNumpad1 || scanCode == kNumpad3;  // do nothing (they would type)
        }
        if (axis.x != 0.f || axis.y != 0.f || axis.z != 0.f) {
            if (g_turning && Camera()) {
                RE::NiMatrix3 turn;
                turn.MakeRotation(sign * kTurnSteps[g_step] * std::numbers::pi_v<float> / 180.f, axis);
                const auto& camera = Camera()->world.rotate;
                view.rotate = camera.Transpose() * turn * camera * view.rotate;
            } else if (axis.x != 0.f) {
                view.dx += sign * kStageSteps[g_step];
            } else if (axis.z != 0.f) {
                view.dy -= sign * kStageSteps[g_step];  // up the screen is up the stage
            } else {
                view.hover = std::max(0.f, view.hover + sign * kMoveSteps[g_step]);
            }
        }
        Apply();
        LogPose("adjusted:", false);
        Log("quill", g_quill.get());
        Log("book", g_parent.get());
        return true;
    }

}
