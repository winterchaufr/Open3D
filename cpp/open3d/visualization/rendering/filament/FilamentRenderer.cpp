// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2019 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/visualization/rendering/filament/FilamentRenderer.h"

#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>

#include "open3d/utility/Console.h"
#include "open3d/visualization/rendering/filament/FilamentCamera.h"
#include "open3d/visualization/rendering/filament/FilamentEntitiesMods.h"
#include "open3d/visualization/rendering/filament/FilamentRenderToBuffer.h"
#include "open3d/visualization/rendering/filament/FilamentResourceManager.h"
#include "open3d/visualization/rendering/filament/FilamentScene.h"
#include "open3d/visualization/rendering/filament/FilamentView.h"

namespace open3d {
namespace visualization {
namespace rendering {

FilamentRenderer::FilamentRenderer(filament::Engine& engine,
                                   void* native_drawable,
                                   FilamentResourceManager& resource_mgr)
    : engine_(engine), resource_mgr_(resource_mgr) {
    swap_chain_ = engine_.createSwapChain(native_drawable);
    renderer_ = engine_.createRenderer();

    materials_modifier_ = std::make_unique<FilamentMaterialModifier>();
}

FilamentRenderer::~FilamentRenderer() {
    scenes_.clear();

    engine_.destroy(renderer_);
    engine_.destroy(swap_chain_);
}

SceneHandle FilamentRenderer::CreateScene() {
    auto handle = SceneHandle::Next();
    scenes_[handle] =
            std::make_unique<FilamentScene>(engine_, resource_mgr_, *this);

    return handle;
}

Scene* FilamentRenderer::GetScene(const SceneHandle& id) const {
    auto found = scenes_.find(id);
    if (found != scenes_.end()) {
        return found->second.get();
    }

    return nullptr;
}

void FilamentRenderer::DestroyScene(const SceneHandle& id) {
    scenes_.erase(id);
}

void FilamentRenderer::UpdateSwapChain() {
    void* native_win = swap_chain_->getNativeWindow();
    engine_.destroy(swap_chain_);

#if defined(__APPLE__)
    auto resize_metal_layer = [](void* native_win) -> void* {
        utility::LogError(
                "::resizeMetalLayer() needs to be implemented. Please see "
                "filament/samples/app/NativeWindowHelperCocoa.mm for "
                "reference.");
        return native_win;
    };

    void* native_swap_chain = native_win;
    void* metal_layer = nullptr;
    auto backend = engine_.getBackend();
    if (backend == filament::Engine::Backend::METAL) {
        metal_layer = resize_metal_layer(native_win);
        // The swap chain on Metal is a CAMetalLayer.
        native_swap_chain = metal_layer;
    }

#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
    if (backend == filament::Engine::Backend::VULKAN) {
        resize_native_layer(native_win);
    }
#endif  // vulkan
#endif  // __APPLE__

    swap_chain_ = engine_.createSwapChain(native_win);
}

void FilamentRenderer::BeginFrame() {
    // We will complete render to buffer requests first
    for (auto& br : buffer_renderers_) {
        if (br->pending_) {
            br->Render();
        }
    }

    frame_started_ = renderer_->beginFrame(swap_chain_);
}

void FilamentRenderer::Draw() {
    if (frame_started_) {
        for (const auto& pair : scenes_) {
            pair.second->Draw(*renderer_);
        }

        if (gui_scene_) {
            gui_scene_->Draw(*renderer_);
        }
    }
}

void FilamentRenderer::EndFrame() {
    if (frame_started_) {
        renderer_->endFrame();
    }
}

MaterialHandle FilamentRenderer::AddMaterial(
        const ResourceLoadRequest& request) {
    return resource_mgr_.CreateMaterial(request);
}

MaterialInstanceHandle FilamentRenderer::AddMaterialInstance(
        const MaterialHandle& material) {
    return resource_mgr_.CreateMaterialInstance(material);
}

MaterialInstanceHandle FilamentRenderer::AddMaterialInstance(
        const geometry::TriangleMesh::Material& material) {
    return resource_mgr_.CreateFromDescriptor(material);
}

MaterialModifier& FilamentRenderer::ModifyMaterial(const MaterialHandle& id) {
    materials_modifier_->Reset();

    auto instance_id = resource_mgr_.CreateMaterialInstance(id);

    if (instance_id) {
        auto w_material_instance =
                resource_mgr_.GetMaterialInstance(instance_id);
        materials_modifier_->Init(w_material_instance.lock(), instance_id);
    } else {
        utility::LogWarning(
                "Failed to create material instance for material handle {}.",
                id);
    }

    return *materials_modifier_;
}

MaterialModifier& FilamentRenderer::ModifyMaterial(
        const MaterialInstanceHandle& id) {
    materials_modifier_->Reset();

    auto w_material_instance = resource_mgr_.GetMaterialInstance(id);
    if (!w_material_instance.expired()) {
        materials_modifier_->Init(w_material_instance.lock(), id);
    } else {
        utility::LogWarning(
                "Failed to modify material instance: unknown instance handle "
                "{}.",
                id);
    }

    return *materials_modifier_;
}

void FilamentRenderer::RemoveMaterialInstance(
        const MaterialInstanceHandle& id) {
    resource_mgr_.Destroy(id);
}

TextureHandle FilamentRenderer::AddTexture(const ResourceLoadRequest& request) {
    if (request.path_.empty()) {
        request.error_callback_(request, -1,
                                "Texture can be loaded only from file");
        return {};
    }

    return resource_mgr_.CreateTexture(request.path_.data());
}

void FilamentRenderer::RemoveTexture(const TextureHandle& id) {
    resource_mgr_.Destroy(id);
}

IndirectLightHandle FilamentRenderer::AddIndirectLight(
        const ResourceLoadRequest& request) {
    if (request.path_.empty()) {
        request.error_callback_(
                request, -1, "Indirect lights can be loaded only from files");
        return {};
    }

    return resource_mgr_.CreateIndirectLight(request);
}

void FilamentRenderer::RemoveIndirectLight(const IndirectLightHandle& id) {
    resource_mgr_.Destroy(id);
}

SkyboxHandle FilamentRenderer::AddSkybox(const ResourceLoadRequest& request) {
    if (request.path_.empty()) {
        request.error_callback_(request, -1,
                                "Skyboxes can be loaded only from files");
        return {};
    }

    return resource_mgr_.CreateSkybox(request);
}

void FilamentRenderer::RemoveSkybox(const SkyboxHandle& id) {
    resource_mgr_.Destroy(id);
}

std::shared_ptr<RenderToBuffer> FilamentRenderer::CreateBufferRenderer() {
    auto renderer = std::make_shared<FilamentRenderToBuffer>(engine_, *this);
    buffer_renderers_.insert(renderer.get());
    return std::move(renderer);
}

void FilamentRenderer::ConvertToGuiScene(const SceneHandle& id) {
    auto found = scenes_.find(id);
    // TODO: assert(found != scenes_.end())
    if (found != scenes_.end()) {
        if (gui_scene_ != nullptr) {
            utility::LogWarning(
                    "FilamentRenderer::ConvertToGuiScene: guiScene_ is already "
                    "set");
        }
        gui_scene_ = std::move(found->second);
        scenes_.erase(found);
    }
}

TextureHandle FilamentRenderer::AddTexture(
        const std::shared_ptr<geometry::Image>& image) {
    return resource_mgr_.CreateTexture(image);
}

void FilamentRenderer::OnBufferRenderDestroyed(FilamentRenderToBuffer* render) {
    buffer_renderers_.erase(render);
}

}  // namespace rendering
}  // namespace visualization
}  // namespace open3d
