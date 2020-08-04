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

#include "open3d/visualization/rendering/filament/FilamentResourceManager.h"

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <image/KtxBundle.h>
#include <image/KtxUtility.h>

#include "open3d/io/ImageIO.h"
#include "open3d/utility/Console.h"
#include "open3d/utility/FileSystem.h"
#include "open3d/visualization/gui/Application.h"
#include "open3d/visualization/rendering/filament/FilamentEntitiesMods.h"

namespace open3d {
namespace visualization {
namespace rendering {

namespace {
template <class ResourceType>
using ResourcesContainer =
        FilamentResourceManager::ResourcesContainer<ResourceType>;

// We need custom shared pointer make function to
// use engine deleter for allocated filament entities
template <class ResourceType>
std::shared_ptr<ResourceType> MakeShared(ResourceType* pointer,
                                         filament::Engine& engine) {
    return std::move(std::shared_ptr<ResourceType>(
            pointer, [&engine](ResourceType* p) { engine.destroy(p); }));
}

template <class Handle, class ResourceType>
Handle RegisterResource(filament::Engine& engine,
                        ResourceType* resource,
                        ResourcesContainer<ResourceType>& container) {
    if (!resource) {
        utility::LogError("Trying to register empty resource!");
        return Handle::kBad;
    }

    auto new_handle = Handle::Next();
    container[new_handle] = std::move(MakeShared(resource, engine));

    return new_handle;
}

template <class ResourceType>
std::weak_ptr<ResourceType> FindResource(
        const REHandle_abstract& id,
        ResourcesContainer<ResourceType>& container) {
    auto found = container.find(id);
    if (found != container.end()) {
        return found->second;
    }

    utility::LogWarning("Resource {} not found.", id);
    return std::weak_ptr<ResourceType>();
}

template <class ResourceType>
void DestroyResource(const REHandle_abstract& id,
                     ResourcesContainer<ResourceType>& container) {
    auto found = container.find(id);
    if (found == container.end()) {
        utility::LogError("Trying to destroy nonexistent resource ({})!", id);
        return;
    }

    container.erase(found);
}

// Image data that is retained by renderer thread,
// will be freed on PixelBufferDescriptor callback
std::unordered_map<std::uint32_t, std::shared_ptr<geometry::Image>>
        pending_images;

std::intptr_t RetainImageForLoading(
        const std::shared_ptr<geometry::Image>& img) {
    static std::intptr_t img_id = 1;

    const auto id = img_id;
    pending_images[img_id] = img;
    ++img_id;

    return id;
}

void FreeRetainedImage(void* buffer, size_t size, void* user_ptr) {
    const auto id = reinterpret_cast<std::intptr_t>(user_ptr);
    auto found = pending_images.find(id);
    if (found != pending_images.end()) {
        pending_images.erase(found);
    } else {
        utility::LogDebug(
                "Trying to release non existent image shared pointer, id: {}",
                id);
    }
}

filament::Material* LoadMaterialFromFile(const std::string& path,
                                         filament::Engine& engine) {
    std::vector<char> material_data;
    std::string error_str;

    if (utility::filesystem::FReadToBuffer(path, material_data, &error_str)) {
        using namespace filament;
        return Material::Builder()
                .package(material_data.data(), material_data.size())
                .build(engine);
    }

    utility::LogDebug("Failed to load default material from {}. Error: {}",
                      path, error_str);

    return nullptr;
}

struct TextureSettings {
    filament::Texture::Format image_format = filament::Texture::Format::RGB;
    filament::Texture::Type image_type = filament::Texture::Type::UBYTE;
    filament::Texture::InternalFormat format =
            filament::Texture::InternalFormat::RGB8;
    std::uint32_t texel_width = 0;
    std::uint32_t texel_height = 0;
};

TextureSettings GetSettingsFromImage(const geometry::Image& image) {
    TextureSettings settings;

    settings.texel_width = image.width_;
    settings.texel_height = image.height_;

    switch (image.num_of_channels_) {
        case 1:
            settings.image_format = filament::Texture::Format::R;
            settings.format = filament::Texture::InternalFormat::R8;
            break;
        case 3:
            settings.image_format = filament::Texture::Format::RGB;
            settings.format = filament::Texture::InternalFormat::RGB8;
            break;
        case 4:
            settings.image_format = filament::Texture::Format::RGBA;
            settings.format = filament::Texture::InternalFormat::RGBA8;
            break;
        default:
            utility::LogError("Unsupported image number of channels: {}",
                              image.num_of_channels_);
            break;
    }

    switch (image.bytes_per_channel_) {
        case 1:
            settings.image_type = filament::Texture::Type::UBYTE;
            break;
        default:
            utility::LogError("Unsupported image bytes per channel: {}",
                              image.bytes_per_channel_);
            break;
    }

    return settings;
}

}  // namespace

const MaterialHandle FilamentResourceManager::kDefaultLit =
        MaterialHandle::Next();
const MaterialHandle FilamentResourceManager::kDefaultUnlit =
        MaterialHandle::Next();
const MaterialHandle FilamentResourceManager::kDefaultNormalShader =
        MaterialHandle::Next();
const MaterialHandle FilamentResourceManager::kDefaultDepthShader =
        MaterialHandle::Next();
const MaterialInstanceHandle FilamentResourceManager::kDepthMaterial =
        MaterialInstanceHandle::Next();
const MaterialInstanceHandle FilamentResourceManager::kNormalsMaterial =
        MaterialInstanceHandle::Next();
const MaterialInstanceHandle FilamentResourceManager::kColorMapMaterial =
        MaterialInstanceHandle::Next();
const TextureHandle FilamentResourceManager::kDefaultTexture =
        TextureHandle::Next();
const TextureHandle FilamentResourceManager::kDefaultColorMap =
        TextureHandle::Next();
const TextureHandle FilamentResourceManager::kDefaultNormalMap =
        TextureHandle::Next();

static const std::unordered_set<REHandle_abstract> kDefaultResources = {
        FilamentResourceManager::kDefaultLit,
        FilamentResourceManager::kDefaultUnlit,
        FilamentResourceManager::kDefaultNormalShader,
        FilamentResourceManager::kDefaultDepthShader,
        FilamentResourceManager::kDepthMaterial,
        FilamentResourceManager::kNormalsMaterial,
        FilamentResourceManager::kDefaultTexture,
        FilamentResourceManager::kDefaultColorMap,
        FilamentResourceManager::kDefaultNormalMap};

FilamentResourceManager::FilamentResourceManager(filament::Engine& engine)
    : engine_(engine) {
    LoadDefaults();
}

FilamentResourceManager::~FilamentResourceManager() { DestroyAll(); }

MaterialHandle FilamentResourceManager::CreateMaterial(
        const void* material_data, const size_t data_size) {
    using namespace filament;

    Material* material = Material::Builder()
                                 .package(material_data, data_size)
                                 .build(engine_);

    MaterialHandle handle;
    if (material) {
        handle =
                RegisterResource<MaterialHandle>(engine_, material, materials_);
    }

    return handle;
}

MaterialHandle FilamentResourceManager::CreateMaterial(
        const ResourceLoadRequest& request) {
    MaterialHandle handle;

    if (!request.path_.empty()) {
        std::vector<char> material_data;
        std::string error_str;

        if (utility::filesystem::FReadToBuffer(request.path_, material_data,
                                               &error_str)) {
            handle = CreateMaterial(material_data.data(), material_data.size());
        } else {
            request.error_callback_(request, errno, error_str);
        }
    } else if (request.data_size_ > 0) {
        // TODO: Filament throws an exception if it can't parse the
        // material. Handle this exception across library boundary
        // to avoid aborting.
        handle = CreateMaterial(request.data_, request.data_size_);
    } else {
        request.error_callback_(request, -1, "");
    }

    return handle;
}

MaterialInstanceHandle FilamentResourceManager::CreateMaterialInstance(
        const MaterialHandle& id) {
    auto found = materials_.find(id);
    if (found != materials_.end()) {
        auto material_instance = found->second->createInstance();
        return RegisterResource<MaterialInstanceHandle>(
                engine_, material_instance, material_instances_);
    }

    utility::LogWarning("Material ({}) for creating instance not found", id);
    return {};
}

MaterialInstanceHandle FilamentResourceManager::CreateFromDescriptor(
        const geometry::TriangleMesh::Material& descriptor) {
    MaterialInstanceHandle handle;
    auto pbr_ref = materials_[kDefaultLit];
    auto material_instance = pbr_ref->createInstance();
    handle = RegisterResource<MaterialInstanceHandle>(
            engine_, material_instance, material_instances_);

    static const auto sampler =
            FilamentMaterialModifier::SamplerFromSamplerParameters(
                    TextureSamplerParameters::Pretty());

    auto base_color = filament::math::float3{descriptor.baseColor.r(),
                                             descriptor.baseColor.g(),
                                             descriptor.baseColor.b()};
    material_instance->setParameter("baseColor", filament::RgbType::sRGB,
                                    base_color);

#define TRY_ASSIGN_MAP(map)                                       \
    {                                                             \
        if (descriptor.map && descriptor.map->HasData()) {        \
            auto hmaptex = CreateTexture(descriptor.map);         \
            if (hmaptex) {                                        \
                material_instance->setParameter(                  \
                        #map, textures_[hmaptex].get(), sampler); \
                dependencies_[handle].insert(hmaptex);            \
            }                                                     \
        }                                                         \
    }

    material_instance->setParameter("baseRoughness", descriptor.baseRoughness);
    material_instance->setParameter("baseMetallic", descriptor.baseMetallic);

    TRY_ASSIGN_MAP(albedo);
    TRY_ASSIGN_MAP(normalMap);
    TRY_ASSIGN_MAP(ambientOcclusion);
    TRY_ASSIGN_MAP(metallic);
    TRY_ASSIGN_MAP(roughness);

#undef TRY_ASSIGN_MAP

    return handle;
}

TextureHandle FilamentResourceManager::CreateTexture(const char* path) {
    std::shared_ptr<geometry::Image> img;

    if (path) {
        img = io::CreateImageFromFile(path);
    } else {
        utility::LogWarning("Empty path for texture loading provided");
    }

    return CreateTexture(img);
}

TextureHandle FilamentResourceManager::CreateTexture(
        const std::shared_ptr<geometry::Image>& img) {
    TextureHandle handle;
    if (img->HasData()) {
        auto texture = LoadTextureFromImage(img);

        handle = RegisterResource<TextureHandle>(engine_, texture, textures_);
    }

    return handle;
}

TextureHandle FilamentResourceManager::CreateTexture(
        const geometry::Image& image) {
    TextureHandle handle;
    if (image.HasData()) {
        auto copy = std::make_shared<geometry::Image>(image);

        auto texture = LoadTextureFromImage(copy);

        handle = RegisterResource<TextureHandle>(engine_, texture, textures_);
    }

    return handle;
}

TextureHandle FilamentResourceManager::CreateTextureFilled(
        const Eigen::Vector3f& color, size_t dimension) {
    TextureHandle handle;
    auto texture = LoadFilledTexture(color, dimension);
    handle = RegisterResource<TextureHandle>(engine_, texture, textures_);

    return handle;
}

IndirectLightHandle FilamentResourceManager::CreateIndirectLight(
        const ResourceLoadRequest& request) {
    IndirectLightHandle handle;

    if (!request.path_.empty()) {
        std::vector<char> ibl_data;
        std::string error_str;

        if (utility::filesystem::FReadToBuffer(request.path_, ibl_data,
                                               &error_str)) {
            using namespace filament;
            // will be destroyed later by image::ktx::createTexture
            auto* ibl_ktx = new image::KtxBundle(
                    reinterpret_cast<std::uint8_t*>(ibl_data.data()),
                    ibl_data.size());
            auto* ibl_texture =
                    image::ktx::createTexture(&engine_, ibl_ktx, false);

            filament::math::float3 bands[9] = {};
            if (!ibl_ktx->getSphericalHarmonics(bands)) {
                engine_.destroy(ibl_texture);
                request.error_callback_(
                        request, 2,
                        "Failed to read spherical harmonics from ktx");
                return handle;
            }

            auto indirect_light = IndirectLight::Builder()
                                          .reflections(ibl_texture)
                                          .irradiance(3, bands)
                                          .intensity(30000.f)
                                          .build(engine_);

            if (indirect_light) {
                handle = RegisterResource<IndirectLightHandle>(
                        engine_, indirect_light, ibls_);

                auto htexture = RegisterResource<TextureHandle>(
                        engine_, ibl_texture, textures_);
                dependencies_[handle].insert(htexture);
            } else {
                request.error_callback_(
                        request, 3, "Failed to create indirect light from ktx");
                engine_.destroy(ibl_texture);
            }
        } else {
            request.error_callback_(request, errno, error_str);
        }
    } else {
        request.error_callback_(request, -1, "");
    }

    return handle;
}

SkyboxHandle FilamentResourceManager::CreateSkybox(
        const ResourceLoadRequest& request) {
    SkyboxHandle handle;

    if (!request.path_.empty()) {
        std::vector<char> sky_data;
        std::string error_str;

        if (utility::filesystem::FReadToBuffer(request.path_, sky_data,
                                               &error_str)) {
            using namespace filament;
            // will be destroyed later by image::ktx::createTexture
            auto* sky_ktx = new image::KtxBundle(
                    reinterpret_cast<std::uint8_t*>(sky_data.data()),
                    sky_data.size());
            auto* sky_texture =
                    image::ktx::createTexture(&engine_, sky_ktx, false);

            auto skybox = Skybox::Builder()
                                  .environment(sky_texture)
                                  .showSun(true)
                                  .build(engine_);

            if (skybox) {
                handle = RegisterResource<SkyboxHandle>(engine_, skybox,
                                                        skyboxes_);

                auto htex = RegisterResource<TextureHandle>(
                        engine_, sky_texture, textures_);
                dependencies_[handle].insert(htex);
            } else {
                request.error_callback_(
                        request, 3, "Failed to create indirect light from ktx");
                engine_.destroy(sky_texture);
            }
        } else {
            request.error_callback_(request, errno, error_str);
        }
    } else {
        request.error_callback_(request, -1, "");
    }

    return handle;
}

VertexBufferHandle FilamentResourceManager::AddVertexBuffer(
        filament::VertexBuffer* vertex_buffer) {
    return RegisterResource<VertexBufferHandle>(engine_, vertex_buffer,
                                                vertex_buffers_);
}

IndexBufferHandle FilamentResourceManager::CreateIndexBuffer(
        size_t indices_count, size_t index_stride) {
    using namespace filament;

    IndexBuffer* ibuf =
            IndexBuffer::Builder()
                    .bufferType(index_stride == 2
                                        ? IndexBuffer::IndexType::USHORT
                                        : IndexBuffer::IndexType::UINT)
                    .indexCount(indices_count)
                    .build(engine_);

    IndexBufferHandle handle;
    if (ibuf) {
        handle = RegisterResource<IndexBufferHandle>(engine_, ibuf,
                                                     index_buffers_);
    }

    return handle;
}

std::weak_ptr<filament::Material> FilamentResourceManager::GetMaterial(
        const MaterialHandle& id) {
    return FindResource(id, materials_);
}

std::weak_ptr<filament::MaterialInstance>
FilamentResourceManager::GetMaterialInstance(const MaterialInstanceHandle& id) {
    return FindResource(id, material_instances_);
}

std::weak_ptr<filament::Texture> FilamentResourceManager::GetTexture(
        const TextureHandle& id) {
    return FindResource(id, textures_);
}

std::weak_ptr<filament::IndirectLight>
FilamentResourceManager::GetIndirectLight(const IndirectLightHandle& id) {
    return FindResource(id, ibls_);
}

std::weak_ptr<filament::Skybox> FilamentResourceManager::GetSkybox(
        const SkyboxHandle& id) {
    return FindResource(id, skyboxes_);
}

std::weak_ptr<filament::VertexBuffer> FilamentResourceManager::GetVertexBuffer(
        const VertexBufferHandle& id) {
    return FindResource(id, vertex_buffers_);
}

std::weak_ptr<filament::IndexBuffer> FilamentResourceManager::GetIndexBuffer(
        const IndexBufferHandle& id) {
    return FindResource(id, index_buffers_);
}

void FilamentResourceManager::DestroyAll() {
    material_instances_.clear();
    materials_.clear();
    textures_.clear();
    vertex_buffers_.clear();
    index_buffers_.clear();
    ibls_.clear();
    skyboxes_.clear();
}

void FilamentResourceManager::Destroy(const REHandle_abstract& id) {
    if (kDefaultResources.count(id) > 0) {
        utility::LogDebug(
                "Trying to destroy default resource {}. Nothing will happen.",
                id);
        return;
    }

    switch (id.type) {
        case EntityType::Material:
            DestroyResource(id, materials_);
            break;
        case EntityType::MaterialInstance:
            DestroyResource(id, material_instances_);
            break;
        case EntityType::Texture:
            DestroyResource(id, textures_);
            break;
        case EntityType::VertexBuffer:
            DestroyResource(id, vertex_buffers_);
            break;
        case EntityType::IndexBuffer:
            DestroyResource(id, index_buffers_);
            break;
        case EntityType::Skybox:
            DestroyResource(id, skyboxes_);
            break;
        case EntityType::IndirectLight:
            DestroyResource(id, ibls_);
            break;
        default:
            utility::LogWarning(
                    "Resource {} is not suited for destruction by "
                    "ResourceManager",
                    REHandle_abstract::TypeToString(id.type));
            return;
    }

    auto found = dependencies_.find(id);
    if (found != dependencies_.end()) {
        for (const auto& dependent : found->second) {
            Destroy(dependent);
        }

        dependencies_.erase(found);
    }
}

filament::Texture* FilamentResourceManager::LoadTextureFromImage(
        const std::shared_ptr<geometry::Image>& image) {
    using namespace filament;

    auto retained_img_id = RetainImageForLoading(image);
    auto texture_settings = GetSettingsFromImage(*image);

    Texture::PixelBufferDescriptor pb(
            image->data_.data(), image->data_.size(),
            texture_settings.image_format, texture_settings.image_type,
            FreeRetainedImage, (void*)retained_img_id);
    auto texture = Texture::Builder()
                           .width(texture_settings.texel_width)
                           .height(texture_settings.texel_height)
                           .levels((uint8_t)1)
                           .format(texture_settings.format)
                           .sampler(Texture::Sampler::SAMPLER_2D)
                           .build(engine_);

    texture->setImage(engine_, 0, std::move(pb));

    return texture;
}

filament::Texture* FilamentResourceManager::LoadFilledTexture(
        const Eigen::Vector3f& color, size_t dimension) {
    auto image = std::make_shared<geometry::Image>();
    image->Prepare(dimension, dimension, 3, 1);

    struct RGB {
        std::uint8_t r, g, b;
    };

    RGB c = {static_cast<uint8_t>(color(0) * 255.f),
             static_cast<uint8_t>(color(1) * 255.f),
             static_cast<uint8_t>(color(2) * 255.f)};

    auto data = reinterpret_cast<RGB*>(image->data_.data());
    for (size_t i = 0; i < dimension * dimension; ++i) {
        data[i] = c;
    }

    auto texture = LoadTextureFromImage(image);
    return texture;
}

void FilamentResourceManager::LoadDefaults() {
    // FIXME: Move to precompiled resource blobs
    const std::string resource_root =
            gui::Application::GetInstance().GetResourcePath();

    const auto texture_path = resource_root + "/defaultTexture.png";
    auto texture_img = io::CreateImageFromFile(texture_path);
    auto texture = LoadTextureFromImage(texture_img);
    textures_[kDefaultTexture] = MakeShared(texture, engine_);

    const auto colormap_path = resource_root + "/defaultGradient.png";
    auto colormap_img = io::CreateImageFromFile(colormap_path);
    auto color_map = LoadTextureFromImage(colormap_img);
    textures_[kDefaultColorMap] = MakeShared(color_map, engine_);

    auto normal_map = LoadFilledTexture(Eigen::Vector3f(0.5, 0.5, 1.f), 1);
    textures_[kDefaultNormalMap] = MakeShared(normal_map, engine_);

    const auto default_sampler =
            FilamentMaterialModifier::SamplerFromSamplerParameters(
                    TextureSamplerParameters::Pretty());
    const auto default_color = filament::math::float3{1.0f, 1.0f, 1.0f};

    const auto lit_path = resource_root + "/defaultLit.filamat";
    auto lit_mat = LoadMaterialFromFile(lit_path, engine_);
    lit_mat->setDefaultParameter("baseColor", filament::RgbType::sRGB,
                                 default_color);
    lit_mat->setDefaultParameter("baseRoughness", 0.7f);
    lit_mat->setDefaultParameter("reflectance", 0.5f);
    lit_mat->setDefaultParameter("baseMetallic", 0.f);
    lit_mat->setDefaultParameter("clearCoat", 0.f);
    lit_mat->setDefaultParameter("clearCoatRoughness", 0.f);
    lit_mat->setDefaultParameter("anisotropy", 0.f);
    lit_mat->setDefaultParameter("pointSize", 3.f);
    lit_mat->setDefaultParameter("albedo", texture, default_sampler);
    lit_mat->setDefaultParameter("metallicMap", texture, default_sampler);
    lit_mat->setDefaultParameter("roughnessMap", texture, default_sampler);
    lit_mat->setDefaultParameter("normalMap", normal_map, default_sampler);
    lit_mat->setDefaultParameter("ambientOcclusionMap", texture,
                                 default_sampler);
    lit_mat->setDefaultParameter("reflectanceMap", texture, default_sampler);
    lit_mat->setDefaultParameter("clearCoatMap", texture, default_sampler);
    lit_mat->setDefaultParameter("clearCoatRoughnessMap", texture,
                                 default_sampler);
    lit_mat->setDefaultParameter("anisotropyMap", texture, default_sampler);
    materials_[kDefaultLit] = MakeShared(lit_mat, engine_);

    const auto unlit_path = resource_root + "/defaultUnlit.filamat";
    auto unlit_mat = LoadMaterialFromFile(unlit_path, engine_);
    unlit_mat->setDefaultParameter("baseColor", filament::RgbType::sRGB,
                                   default_color);
    unlit_mat->setDefaultParameter("pointSize", 3.f);
    unlit_mat->setDefaultParameter("albedo", texture, default_sampler);
    materials_[kDefaultUnlit] = MakeShared(unlit_mat, engine_);

    const auto depth_path = resource_root + "/depth.filamat";
    auto depth_mat = LoadMaterialFromFile(depth_path, engine_);
    depth_mat->setDefaultParameter("pointSize", 3.f);
    materials_[kDefaultDepthShader] = MakeShared(depth_mat, engine_);

    // NOTE: Legacy. Can be removed soon.
    const auto hdepth = CreateMaterial(ResourceLoadRequest(depth_path.data()));
    auto depth_mat_inst = materials_[hdepth];
    depth_mat_inst->setDefaultParameter("pointSize", 3.f);
    material_instances_[kDepthMaterial] =
            MakeShared(depth_mat_inst->createInstance(), engine_);

    const auto normals_path = resource_root + "/normals.filamat";
    auto normals_mat = LoadMaterialFromFile(normals_path, engine_);
    normals_mat->setDefaultParameter("pointSize", 3.f);
    materials_[kDefaultNormalShader] = MakeShared(normals_mat, engine_);

    // NOTE: Leacy. Can be removed soon.
    const auto hnormals =
            CreateMaterial(ResourceLoadRequest(normals_path.data()));
    auto normals_mat_inst = materials_[hnormals];
    normals_mat_inst->setDefaultParameter("pointSize", 3.f);
    material_instances_[kNormalsMaterial] =
            MakeShared(normals_mat_inst->createInstance(), engine_);

    const auto colormap_map_path = resource_root + "/colorMap.filamat";
    const auto hcolormap_mat =
            CreateMaterial(ResourceLoadRequest(colormap_map_path.data()));
    auto colormap_mat = materials_[hcolormap_mat];
    auto colormap_mat_inst = colormap_mat->createInstance();
    colormap_mat_inst->setParameter("colorMap", color_map, default_sampler);
    material_instances_[kColorMapMaterial] =
            MakeShared(colormap_mat_inst, engine_);
}

}  // namespace rendering
}  // namespace visualization
}  // namespace open3d
