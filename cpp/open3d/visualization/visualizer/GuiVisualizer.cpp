// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
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

#include "open3d/visualization/visualizer/GuiVisualizer.h"

#include "open3d/Open3DConfig.h"
#include "open3d/geometry/BoundingVolume.h"
#include "open3d/geometry/Image.h"
#include "open3d/geometry/PointCloud.h"
#include "open3d/geometry/TriangleMesh.h"
#include "open3d/io/FileFormatIO.h"
#include "open3d/io/ImageIO.h"
#include "open3d/io/PointCloudIO.h"
#include "open3d/io/TriangleMeshIO.h"
#include "open3d/utility/Console.h"
#include "open3d/utility/FileSystem.h"
#include "open3d/visualization/gui/Application.h"
#include "open3d/visualization/gui/Button.h"
#include "open3d/visualization/gui/Checkbox.h"
#include "open3d/visualization/gui/Color.h"
#include "open3d/visualization/gui/ColorEdit.h"
#include "open3d/visualization/gui/Combobox.h"
#include "open3d/visualization/gui/Dialog.h"
#include "open3d/visualization/gui/FileDialog.h"
#include "open3d/visualization/gui/Label.h"
#include "open3d/visualization/gui/Layout.h"
#include "open3d/visualization/gui/ProgressBar.h"
#include "open3d/visualization/gui/SceneWidget.h"
#include "open3d/visualization/gui/Slider.h"
#include "open3d/visualization/gui/Theme.h"
#include "open3d/visualization/gui/VectorEdit.h"
#include "open3d/visualization/rendering/Camera.h"
#include "open3d/visualization/rendering/Material.h"
#include "open3d/visualization/rendering/Open3DScene.h"
#include "open3d/visualization/rendering/RenderToBuffer.h"
#include "open3d/visualization/rendering/RendererHandle.h"
#include "open3d/visualization/rendering/RendererStructs.h"
#include "open3d/visualization/rendering/Scene.h"
#include "open3d/visualization/rendering/filament/FilamentResourceManager.h"
#include "open3d/visualization/visualizer/GuiSettingsModel.h"
#include "open3d/visualization/visualizer/GuiSettingsView.h"
#include "open3d/visualization/visualizer/GuiWidgets.h"

#define LOAD_IN_NEW_WINDOW 0

namespace open3d {
namespace visualization {

namespace {

std::shared_ptr<gui::Dialog> CreateAboutDialog(gui::Window *window) {
    auto &theme = window->GetTheme();
    auto dlg = std::make_shared<gui::Dialog>("About");

    auto title = std::make_shared<gui::Label>(
            (std::string("Open3D ") + OPEN3D_VERSION).c_str());
    auto text = std::make_shared<gui::Label>(
            "The MIT License (MIT)\n"
            "Copyright (c) 2018 - 2020 www.open3d.org\n\n"

            "Permission is hereby granted, free of charge, to any person "
            "obtaining "
            "a copy of this software and associated documentation files (the "
            "\"Software\"), to deal in the Software without restriction, "
            "including "
            "without limitation the rights to use, copy, modify, merge, "
            "publish, "
            "distribute, sublicense, and/or sell copies of the Software, and "
            "to "
            "permit persons to whom the Software is furnished to do so, "
            "subject to "
            "the following conditions:\n\n"

            "The above copyright notice and this permission notice shall be "
            "included in all copies or substantial portions of the "
            "Software.\n\n"

            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
            "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES "
            "OF "
            "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
            "NONINFRINGEMENT. "
            "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR "
            "ANY "
            "CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF "
            "CONTRACT, "
            "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
            "SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.");
    auto ok = std::make_shared<gui::Button>("OK");
    ok->SetOnClicked([window]() { window->CloseDialog(); });

    gui::Margins margins(theme.font_size);
    auto layout = std::make_shared<gui::Vert>(0, margins);
    layout->AddChild(gui::Horiz::MakeCentered(title));
    layout->AddFixed(theme.font_size);
    layout->AddChild(text);
    layout->AddFixed(theme.font_size);
    layout->AddChild(gui::Horiz::MakeCentered(ok));
    dlg->AddChild(layout);

    return dlg;
}

std::shared_ptr<gui::VGrid> CreateHelpDisplay(gui::Window *window) {
    auto &theme = window->GetTheme();

    gui::Margins margins(theme.font_size);
    auto layout = std::make_shared<gui::VGrid>(2, 0, margins);
    layout->SetBackgroundColor(gui::Color(0, 0, 0, 0.5));

    auto AddLabel = [layout](const char *text) {
        auto label = std::make_shared<gui::Label>(text);
        label->SetTextColor(gui::Color(1, 1, 1));
        layout->AddChild(label);
    };
    auto AddRow = [layout, &AddLabel](const char *left, const char *right) {
        AddLabel(left);
        AddLabel(right);
    };

    AddRow("Arcball mode", " ");
    AddRow("Left-drag", "Rotate camera");
    AddRow("Shift + left-drag", "Forward/backward");

#if defined(__APPLE__)
    AddLabel("Cmd + left-drag");
#else
    AddLabel("Ctrl + left-drag");
#endif  // __APPLE__
    AddLabel("Pan camera");

#if defined(__APPLE__)
    AddLabel("Opt + left-drag (up/down)  ");
#else
    AddLabel("Win + left-drag (up/down)  ");
#endif  // __APPLE__
    AddLabel("Rotate around forward axis");

    // GNOME3 uses Win/Meta as a shortcut to move windows around, so we
    // need another way to rotate around the forward axis.
    AddLabel("Ctrl + Shift + left-drag");
    AddLabel("Rotate around forward axis");

#if defined(__APPLE__)
    AddLabel("Ctrl + left-drag");
#else
    AddLabel("Alt + left-drag");
#endif  // __APPLE__
    AddLabel("Rotate directional light");

    AddRow("Right-drag", "Pan camera");
    AddRow("Middle-drag", "Rotate directional light");
    AddRow("Wheel", "Forward/backward");
    AddRow("Shift + Wheel", "Change field of view");
    AddRow("", "");

    AddRow("Fly mode", " ");
    AddRow("Left-drag", "Rotate camera");
#if defined(__APPLE__)
    AddLabel("Opt + left-drag");
#else
    AddLabel("Win + left-drag");
#endif  // __APPLE__
    AddLabel("Rotate around forward axis");
    AddRow("W", "Forward");
    AddRow("S", "Backward");
    AddRow("A", "Step left");
    AddRow("D", "Step right");
    AddRow("Q", "Step up");
    AddRow("Z", "Step down");
    AddRow("E", "Roll left");
    AddRow("R", "Roll right");
    AddRow("Up", "Look up");
    AddRow("Down", "Look down");
    AddRow("Left", "Look left");
    AddRow("Right", "Look right");

    return layout;
}

std::shared_ptr<gui::VGrid> CreateCameraDisplay(gui::Window *window) {
    auto &theme = window->GetTheme();

    gui::Margins margins(theme.font_size);
    auto layout = std::make_shared<gui::VGrid>(2, 0, margins);
    layout->SetBackgroundColor(gui::Color(0, 0, 0, 0.5));

    auto AddLabel = [layout](const char *text) {
        auto label = std::make_shared<gui::Label>(text);
        label->SetTextColor(gui::Color(1, 1, 1));
        layout->AddChild(label);
    };
    auto AddRow = [layout, &AddLabel](const char *left, const char *right) {
        AddLabel(left);
        AddLabel(right);
    };

    AddRow("Position:", "[0 0 0]");
    AddRow("Forward:", "[0 0 0]");
    AddRow("Left:", "[0 0 0]");
    AddRow("Up:", "[0 0 0]");

    return layout;
}

std::shared_ptr<gui::Dialog> CreateContactDialog(gui::Window *window) {
    auto &theme = window->GetTheme();
    auto em = theme.font_size;
    auto dlg = std::make_shared<gui::Dialog>("Contact Us");

    auto title = std::make_shared<gui::Label>("Contact Us");
    auto left_col = std::make_shared<gui::Label>(
            "Web site:\n"
            "Code:\n"
            "Mailing list:\n"
            "Discord channel:");
    auto right_col = std::make_shared<gui::Label>(
            "http://www.open3d.org\n"
            "http://github.org/intel-isl/Open3D\n"
            "http://www.open3d.org/index.php/subscribe/\n"
            "https://discord.gg/D35BGvn");
    auto ok = std::make_shared<gui::Button>("OK");
    ok->SetOnClicked([window]() { window->CloseDialog(); });

    gui::Margins margins(em);
    auto layout = std::make_shared<gui::Vert>(0, margins);
    layout->AddChild(gui::Horiz::MakeCentered(title));
    layout->AddFixed(em);

    auto columns = std::make_shared<gui::Horiz>(em, gui::Margins());
    columns->AddChild(left_col);
    columns->AddChild(right_col);
    layout->AddChild(columns);

    layout->AddFixed(em);
    layout->AddChild(gui::Horiz::MakeCentered(ok));
    dlg->AddChild(layout);

    return dlg;
}

bool ColorArrayIsUniform(const std::vector<Eigen::Vector3d> &colors) {
    static const double e = 1.0 / 255.0;
    static const double SQ_EPSILON = Eigen::Vector3d(e, e, e).squaredNorm();
    const auto &color = colors[0];

    for (const auto &c : colors) {
        if ((color - c).squaredNorm() > SQ_EPSILON) {
            return false;
        }
    }

    return true;
}

bool PointCloudHasUniformColor(const geometry::PointCloud &pcd) {
    if (!pcd.HasColors()) {
        return true;
    }
    return ColorArrayIsUniform(pcd.colors_);
};

bool MeshHasUniformColor(const geometry::MeshBase &mesh) {
    if (!mesh.HasVertexColors()) {
        return true;
    }
    return ColorArrayIsUniform(mesh.vertex_colors_);
};

//----
class DrawTimeLabel : public gui::Label {
    using Super = Label;

public:
    DrawTimeLabel(gui::Window *w) : Label("0.0 ms") { window_ = w; }

    gui::Size CalcPreferredSize(const gui::Theme &theme) const override {
        auto h = Super::CalcPreferredSize(theme).height;
        return gui::Size(theme.font_size * 5, h);
    }

    DrawResult Draw(const gui::DrawContext &context) override {
        char text[64];
        // double ms = window_->GetLastFrameTimeSeconds() * 1000.0;
        double ms = 0.0;
        snprintf(text, sizeof(text) - 1, "%.1f ms", ms);
        SetText(text);

        return Super::Draw(context);
    }

private:
    gui::Window *window_;
};

}  // namespace

enum MenuId {
    FILE_OPEN,
    FILE_EXPORT_RGB,
    FILE_QUIT,
    SETTINGS_LIGHT_AND_MATERIALS,
    HELP_KEYS,
    HELP_CAMERA,
    HELP_ABOUT,
    HELP_CONTACT
};

struct GuiVisualizer::Impl {
    std::shared_ptr<gui::SceneWidget> scene_wgt_;
    std::shared_ptr<gui::VGrid> help_keys_;
    std::shared_ptr<gui::VGrid> help_camera_;

    struct TextureMaps {
        rendering::TextureHandle albedo_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle normal_map =
                rendering::FilamentResourceManager::kDefaultNormalMap;
        rendering::TextureHandle ambient_occlusion_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle roughness_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle metallic_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle reflectance_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle clear_coat_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle clear_coat_roughness_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        rendering::TextureHandle anisotropy_map =
                rendering::FilamentResourceManager::kDefaultTexture;
    };

    struct Settings {
        rendering::MaterialHandle lit_template;
        rendering::MaterialHandle unlit_template;

        rendering::MaterialInstanceHandle lit;
        rendering::MaterialInstanceHandle unlit;
        TextureMaps maps;

        // geometry -> material  (entry exists if mesh HasMaterials())
        bool have_loaded_material_;
        rendering::Material loaded_material_;
        rendering::Material lit_material_;
        rendering::Material unlit_material_;
        rendering::Material normal_depth_material_;

        GuiSettingsModel model_;
        std::shared_ptr<gui::Vert> wgt_base;
        std::shared_ptr<gui::Button> wgt_mouse_arcball;
        std::shared_ptr<gui::Button> wgt_mouse_fly;
        std::shared_ptr<gui::Button> wgt_mouse_model;
        std::shared_ptr<gui::Button> wgt_mouse_sun;
        std::shared_ptr<gui::Button> wgt_mouse_ibl;
        std::shared_ptr<GuiSettingsView> view_;
    } settings_;

    int app_menu_custom_items_index_ = -1;
    std::shared_ptr<gui::Menu> app_menu_;

    void InitializeMaterials(rendering::Renderer &renderer,
                             const std::string &resource_path) {
        settings_.lit_material_.shader = "defaultLit";
        settings_.unlit_material_.shader = "defaultUnlit";

        auto &defaults = settings_.model_.GetCurrentMaterials();

        settings_.maps.albedo_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.normal_map =
                rendering::FilamentResourceManager::kDefaultNormalMap;
        settings_.maps.ambient_occlusion_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.roughness_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.metallic_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.reflectance_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.clear_coat_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.clear_coat_roughness_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.anisotropy_map =
                rendering::FilamentResourceManager::kDefaultTexture;

        UpdateMaterials(renderer, defaults);
    }

    void SetMaterialsToDefault() {
        settings_.view_->ShowFileMaterialEntry(false);

        settings_.maps.albedo_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.normal_map =
                rendering::FilamentResourceManager::kDefaultNormalMap;
        settings_.maps.ambient_occlusion_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.roughness_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.metallic_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.reflectance_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.clear_coat_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.clear_coat_roughness_map =
                rendering::FilamentResourceManager::kDefaultTexture;
        settings_.maps.anisotropy_map =
                rendering::FilamentResourceManager::kDefaultTexture;

        settings_.model_.SetMaterialsToDefault();
        // model's OnChanged callback will get called (if set), which will
        // update everything.
    }

    void SetLoadedMaterial(rendering::Renderer &renderer,
                           GuiSettingsModel::LitMaterial material,
                           TextureMaps maps) {
        settings_.maps = maps;

        GuiSettingsModel::Materials materials;
        materials.lit = material;
        materials.unlit.base_color = material.base_color;

        settings_.model_.SetCurrentMaterials(
                materials, GuiSettingsModel::MATERIAL_FROM_FILE_NAME);
        // model's OnChanged callback will get called (if set), which will
        // update everything.
    }

    bool SetIBL(rendering::Renderer &renderer, const std::string &path) {
        auto *render_scene = scene_wgt_->GetScene()->GetScene();
        std::string ibl_name(path);
        if (ibl_name.empty()) {
            ibl_name =
                    std::string(
                            gui::Application::GetInstance().GetResourcePath()) +
                    "/" + GuiSettingsModel::DEFAULT_IBL;
        }
        if (ibl_name.find("_ibl.ktx") != std::string::npos) {
            ibl_name = ibl_name.substr(0, ibl_name.size() - 8);
        }
        render_scene->SetIndirectLight(ibl_name);
        float intensity = render_scene->GetIndirectLightIntensity();
        render_scene->SetIndirectLightIntensity(intensity);

        return true;
    }

    void SetMouseControls(gui::Window &window,
                          gui::SceneWidget::Controls mode) {
        using Controls = gui::SceneWidget::Controls;
        scene_wgt_->SetViewControls(mode);
        window.SetFocusWidget(scene_wgt_.get());
        settings_.wgt_mouse_arcball->SetOn(mode == Controls::ROTATE_CAMERA);
        settings_.wgt_mouse_fly->SetOn(mode == Controls::FLY);
        settings_.wgt_mouse_model->SetOn(mode == Controls::ROTATE_MODEL);
        settings_.wgt_mouse_sun->SetOn(mode == Controls::ROTATE_SUN);
        settings_.wgt_mouse_ibl->SetOn(mode == Controls::ROTATE_IBL);
    }

    void UpdateFromModel(rendering::Renderer &renderer,
                         bool material_type_changed) {
        scene_wgt_->SetBackgroundColor(settings_.model_.GetBackgroundColor());

        if (settings_.model_.GetShowSkybox()) {
            scene_wgt_->GetScene()->ShowSkybox(true);
        } else {
            scene_wgt_->GetScene()->ShowSkybox(false);
        }
        scene_wgt_->ShowSkybox(settings_.model_.GetShowSkybox());

        scene_wgt_->GetScene()->ShowAxes(settings_.model_.GetShowAxes());

        UpdateLighting(renderer, settings_.model_.GetLighting());

        auto &current_materials = settings_.model_.GetCurrentMaterials();
        if (current_materials.lit_name ==
            GuiSettingsModel::MATERIAL_FROM_FILE_NAME) {
            scene_wgt_->GetScene()->UpdateMaterial(settings_.loaded_material_);
        } else {
            UpdateMaterials(renderer, current_materials);
            switch (settings_.model_.GetMaterialType()) {
                case GuiSettingsModel::MaterialType::LIT:
                    scene_wgt_->GetScene()->UpdateMaterial(
                            settings_.lit_material_);
                    break;
                case GuiSettingsModel::MaterialType::UNLIT:
                    scene_wgt_->GetScene()->UpdateMaterial(
                            settings_.unlit_material_);
                    break;
                case GuiSettingsModel::MaterialType::NORMAL_MAP: {
                    settings_.normal_depth_material_.shader = "normals";
                    scene_wgt_->GetScene()->UpdateMaterial(
                            settings_.normal_depth_material_);
                } break;
                case GuiSettingsModel::MaterialType::DEPTH: {
                    settings_.normal_depth_material_.shader = "depth";
                    scene_wgt_->GetScene()->UpdateMaterial(
                            settings_.normal_depth_material_);
                } break;

                default:
                    break;
            }
        }

        if (material_type_changed) {
            auto *view = scene_wgt_->GetRenderView();
            switch (settings_.model_.GetMaterialType()) {
                case GuiSettingsModel::MaterialType::LIT: {
                    view->SetMode(rendering::View::Mode::Color);
                    break;
                }
                case GuiSettingsModel::MaterialType::UNLIT: {
                    view->SetMode(rendering::View::Mode::Color);
                    break;
                }
                case GuiSettingsModel::MaterialType::NORMAL_MAP:
                    view->SetMode(rendering::View::Mode::Normals);
                    break;
                case GuiSettingsModel::MaterialType::DEPTH:
                    view->SetMode(rendering::View::Mode::Depth);
                    break;
            }
        }
    }

private:
    void UpdateLighting(rendering::Renderer &renderer,
                        const GuiSettingsModel::LightingProfile &lighting) {
        auto scene = scene_wgt_->GetScene();
        auto *render_scene = scene->GetScene();
        if (lighting.use_default_ibl) {
            this->SetIBL(renderer, "");
        }

        render_scene->EnableIndirectLight(lighting.ibl_enabled);
        render_scene->SetIndirectLightIntensity(lighting.ibl_intensity);
        render_scene->SetIndirectLightRotation(lighting.ibl_rotation);
        render_scene->SetDirectionalLight(lighting.sun_dir, lighting.sun_color,
                                          lighting.sun_intensity);
        render_scene->EnableDirectionalLight(lighting.sun_enabled);
    }

    void UpdateMaterials(rendering::Renderer &renderer,
                         const GuiSettingsModel::Materials &materials) {
        auto &lit = settings_.lit_material_;
        auto &unlit = settings_.unlit_material_;
        auto &normal_depth = settings_.normal_depth_material_;

        // Update lit from GUI
        lit.base_color.x() = materials.lit.base_color.x();
        lit.base_color.y() = materials.lit.base_color.y();
        lit.base_color.z() = materials.lit.base_color.z();
        lit.point_size = materials.point_size;
        lit.base_metallic = materials.lit.metallic;
        lit.base_roughness = materials.lit.roughness;
        lit.base_reflectance = materials.lit.reflectance;
        lit.base_clearcoat = materials.lit.clear_coat;
        lit.base_clearcoat_roughness = materials.lit.clear_coat_roughness;
        lit.base_anisotropy = materials.lit.anisotropy;

        // Update unlit from GUI
        unlit.base_color.x() = materials.unlit.base_color.x();
        unlit.base_color.y() = materials.unlit.base_color.y();
        unlit.base_color.z() = materials.unlit.base_color.z();
        unlit.point_size = materials.point_size;

        // Update normal/depth from GUI
        normal_depth.point_size = materials.point_size;
    }

    void OnNewIBL(Window &window, const char *name) {
        std::string path = gui::Application::GetInstance().GetResourcePath();
        path += std::string("/") + name + "_ibl.ktx";
        if (!SetIBL(window.GetRenderer(), path)) {
            // must be the "Custom..." option
            auto dlg = std::make_shared<gui::FileDialog>(
                    gui::FileDialog::Mode::OPEN, "Open HDR Map",
                    window.GetTheme());
            dlg->AddFilter(".ktx", "Khronos Texture (.ktx)");
            dlg->SetOnCancel([&window]() { window.CloseDialog(); });
            dlg->SetOnDone([this, &window](const char *path) {
                window.CloseDialog();
                SetIBL(window.GetRenderer(), path);
                // We need to set the "custom" bit, so just call the current
                // profile a custom profile.
                settings_.model_.SetCustomLighting(
                        settings_.model_.GetLighting());
            });
            window.ShowDialog(dlg);
        }
    }
};

GuiVisualizer::GuiVisualizer(const std::string &title, int width, int height)
    : gui::Window(title, width, height), impl_(new GuiVisualizer::Impl()) {
    Init();
}

GuiVisualizer::GuiVisualizer(
        const std::vector<std::shared_ptr<const geometry::Geometry>>
                &geometries,
        const std::string &title,
        int width,
        int height,
        int left,
        int top)
    : gui::Window(title, left, top, width, height),
      impl_(new GuiVisualizer::Impl()) {
    Init();
    SetGeometry(geometries);  // also updates the camera
}

void GuiVisualizer::Init() {
    auto &app = gui::Application::GetInstance();
    auto &theme = GetTheme();

    // Create menu
    if (!gui::Application::GetInstance().GetMenubar()) {
        auto menu = std::make_shared<gui::Menu>();
#if defined(__APPLE__)
        // The first menu item to be added on macOS becomes the application
        // menu (no matter its name)
        auto app_menu = std::make_shared<gui::Menu>();
        app_menu->AddItem("About", HELP_ABOUT);
        app_menu->AddSeparator();
        impl_->app_menu_custom_items_index_ = app_menu->GetNumberOfItems();
        app_menu->AddItem("Quit", FILE_QUIT, gui::KEY_Q);
        menu->AddMenu("Open3D", app_menu);
        impl_->app_menu_ = app_menu;
#endif  // __APPLE__
        auto file_menu = std::make_shared<gui::Menu>();
        file_menu->AddItem("Open...", FILE_OPEN, gui::KEY_O);
        file_menu->AddItem("Export Current Image...", FILE_EXPORT_RGB);
        file_menu->AddSeparator();
#if WIN32
        file_menu->AddItem("Exit", FILE_QUIT);
#elif !defined(__APPLE__)  // quit goes in app menu on macOS
        file_menu->AddItem("Quit", FILE_QUIT, gui::KEY_Q);
#endif
        menu->AddMenu("File", file_menu);

        auto settings_menu = std::make_shared<gui::Menu>();
        settings_menu->AddItem("Lighting & Materials",
                               SETTINGS_LIGHT_AND_MATERIALS);
        settings_menu->SetChecked(SETTINGS_LIGHT_AND_MATERIALS, true);
        menu->AddMenu("Settings", settings_menu);

        auto help_menu = std::make_shared<gui::Menu>();
        help_menu->AddItem("Show Controls", HELP_KEYS);
        help_menu->AddItem("Show Camera Info", HELP_CAMERA);
        help_menu->AddSeparator();
        help_menu->AddItem("About", HELP_ABOUT);
        help_menu->AddItem("Contact", HELP_CONTACT);
#if defined(__APPLE__) && GUI_USE_NATIVE_MENUS
        // macOS adds a special search item to menus named "Help",
        // so add a space to avoid that.
        menu->AddMenu("Help ", help_menu);
#else
        menu->AddMenu("Help", help_menu);
#endif

        gui::Application::GetInstance().SetMenubar(menu);
    }

    // Create scene
    impl_->scene_wgt_ = std::make_shared<gui::SceneWidget>();
    impl_->scene_wgt_->SetScene(
            std::make_shared<rendering::Open3DScene>(GetRenderer()));
    impl_->scene_wgt_->SetOnSunDirectionChanged(
            [this](const Eigen::Vector3f &new_dir) {
                auto lighting = impl_->settings_.model_.GetLighting();  // copy
                lighting.sun_dir = new_dir.normalized();
                impl_->settings_.model_.SetCustomLighting(lighting);
            });

    // Create light
    auto &settings = impl_->settings_;
    std::string resource_path = app.GetResourcePath();
    auto ibl_path = resource_path + "/default";
    auto *render_scene = impl_->scene_wgt_->GetScene()->GetScene();
    render_scene->SetIndirectLight(ibl_path);
    impl_->scene_wgt_->ShowSkybox(settings.model_.GetShowSkybox());

    // Create materials
    impl_->InitializeMaterials(GetRenderer(), resource_path);

    // Apply model settings (which should be defaults) to the rendering entities
    impl_->UpdateFromModel(GetRenderer(), false);

    // Setup UI
    const auto em = theme.font_size;
    const int lm = std::ceil(0.5 * em);
    const int grid_spacing = std::ceil(0.25 * em);

    AddChild(impl_->scene_wgt_);

    // Add settings widget
    const int separation_height = std::ceil(0.75 * em);
    // (we don't want as much left margin because the twisty arrow is the
    // only thing there, and visually it looks larger than the right.)
    const gui::Margins base_margins(0.5 * lm, lm, lm, lm);
    settings.wgt_base = std::make_shared<gui::Vert>(0, base_margins);

    gui::Margins indent(em, 0, 0, 0);
    auto view_ctrls =
            std::make_shared<gui::CollapsableVert>("Mouse controls", 0, indent);

    // ... view manipulator buttons
    settings.wgt_mouse_arcball = std::make_shared<SmallToggleButton>("Arcball");
    impl_->settings_.wgt_mouse_arcball->SetOn(true);
    settings.wgt_mouse_arcball->SetOnClicked([this]() {
        impl_->SetMouseControls(*this,
                                gui::SceneWidget::Controls::ROTATE_CAMERA);
    });
    settings.wgt_mouse_fly = std::make_shared<SmallToggleButton>("Fly");
    settings.wgt_mouse_fly->SetOnClicked([this]() {
        impl_->SetMouseControls(*this, gui::SceneWidget::Controls::FLY);
    });
    settings.wgt_mouse_model = std::make_shared<SmallToggleButton>("Model");
    settings.wgt_mouse_model->SetOnClicked([this]() {
        impl_->SetMouseControls(*this,
                                gui::SceneWidget::Controls::ROTATE_MODEL);
    });
    settings.wgt_mouse_sun = std::make_shared<SmallToggleButton>("Sun");
    settings.wgt_mouse_sun->SetOnClicked([this]() {
        impl_->SetMouseControls(*this, gui::SceneWidget::Controls::ROTATE_SUN);
    });
    settings.wgt_mouse_ibl = std::make_shared<SmallToggleButton>("Environment");
    settings.wgt_mouse_ibl->SetOnClicked([this]() {
        impl_->SetMouseControls(*this, gui::SceneWidget::Controls::ROTATE_IBL);
    });

    auto reset_camera = std::make_shared<SmallButton>("Reset camera");
    reset_camera->SetOnClicked([this]() {
        impl_->scene_wgt_->GoToCameraPreset(
                gui::SceneWidget::CameraPreset::PLUS_Z);
    });

    auto camera_controls1 = std::make_shared<gui::Horiz>(grid_spacing);
    camera_controls1->AddStretch();
    camera_controls1->AddChild(settings.wgt_mouse_arcball);
    camera_controls1->AddChild(settings.wgt_mouse_fly);
    camera_controls1->AddChild(settings.wgt_mouse_model);
    camera_controls1->AddStretch();
    auto camera_controls2 = std::make_shared<gui::Horiz>(grid_spacing);
    camera_controls2->AddStretch();
    camera_controls2->AddChild(settings.wgt_mouse_sun);
    camera_controls2->AddChild(settings.wgt_mouse_ibl);
    camera_controls2->AddStretch();
    view_ctrls->AddChild(camera_controls1);
    view_ctrls->AddFixed(0.25 * em);
    view_ctrls->AddChild(camera_controls2);
    view_ctrls->AddFixed(separation_height);
    view_ctrls->AddChild(gui::Horiz::MakeCentered(reset_camera));
    settings.wgt_base->AddChild(view_ctrls);

    // ... lighting and materials
    settings.view_ = std::make_shared<GuiSettingsView>(
            settings.model_, theme, resource_path, [this](const char *name) {
                std::string resource_path =
                        gui::Application::GetInstance().GetResourcePath();
                impl_->SetIBL(GetRenderer(),
                              resource_path + "/" + name + "_ibl.ktx");
            });
    settings.model_.SetOnChanged([this](bool material_type_changed) {
        impl_->settings_.view_->Update();
        impl_->UpdateFromModel(GetRenderer(), material_type_changed);
    });
    settings.wgt_base->AddChild(settings.view_);

    AddChild(settings.wgt_base);

    // Other items
    impl_->help_keys_ = CreateHelpDisplay(this);
    impl_->help_keys_->SetVisible(false);
    AddChild(impl_->help_keys_);
    impl_->help_camera_ = CreateCameraDisplay(this);
    impl_->help_camera_->SetVisible(false);
    AddChild(impl_->help_camera_);
}

GuiVisualizer::~GuiVisualizer() {}

void GuiVisualizer::SetTitle(const std::string &title) {
    Super::SetTitle(title.c_str());
}

void GuiVisualizer::AddItemsToAppMenu(
        const std::vector<std::pair<std::string, gui::Menu::ItemId>> &items) {
#if !defined(__APPLE__)
    return;  // application menu only exists on macOS
#endif

    if (impl_->app_menu_ && impl_->app_menu_custom_items_index_ >= 0) {
        for (auto &it : items) {
            impl_->app_menu_->InsertItem(impl_->app_menu_custom_items_index_++,
                                         it.first.c_str(), it.second);
        }
        impl_->app_menu_->InsertSeparator(
                impl_->app_menu_custom_items_index_++);
    }
}

void GuiVisualizer::SetGeometry(
        const std::vector<std::shared_ptr<const geometry::Geometry>>
                &geometries) {
    auto scene3d = impl_->scene_wgt_->GetScene();
    scene3d->ClearGeometry();

    impl_->SetMaterialsToDefault();

    std::size_t num_point_clouds = 0;
    std::size_t num_point_cloud_points = 0;
    for (auto &g : geometries) {
        if (g->GetGeometryType() ==
            geometry::Geometry::GeometryType::PointCloud) {
            num_point_clouds++;
            auto cloud =
                    std::static_pointer_cast<const geometry::PointCloud>(g);
            num_point_cloud_points += cloud->points_.size();
        }
    }

    geometry::AxisAlignedBoundingBox bounds;
    std::size_t num_unlit = 0;
    for (size_t i = 0; i < geometries.size(); ++i) {
        std::shared_ptr<const geometry::Geometry> g = geometries[i];
        rendering::Material loaded_material;
        bool material_is_loaded = false;
        // GuiSettingsModel::LitMaterial loaded_material;

        // If a point cloud or mesh has no vertex colors or a single uniform
        // color (usually white), then we want to display it normally, that is,
        // lit. But if the cloud/mesh has differing vertex colors, then we
        // assume that the vertex colors have the lighting value baked in
        // (for example, fountain.ply at http://qianyi.info/scenedata.html)
        switch (g->GetGeometryType()) {
            case geometry::Geometry::GeometryType::PointCloud: {
                auto pcd =
                        std::static_pointer_cast<const geometry::PointCloud>(g);

                if (pcd->HasColors() && !PointCloudHasUniformColor(*pcd)) {
                    loaded_material.shader = "defaultUnlit";
                    num_unlit += 1;
                } else {
                    loaded_material.shader = "defaultLit";
                }
            } break;
            case geometry::Geometry::GeometryType::LineSet: {
                loaded_material.shader = "defaultUnlit";
                num_unlit += 1;
            } break;
            case geometry::Geometry::GeometryType::TriangleMesh: {
                auto mesh =
                        std::static_pointer_cast<const geometry::TriangleMesh>(
                                g);

                bool albedo_only = true;
                auto is_map_valid =
                        [](std::shared_ptr<geometry::Image> map) -> bool {
                    return map && map->HasData();
                };

                if (mesh->HasMaterials()) {
                    auto mesh_material = mesh->materials_.begin()->second;
                    loaded_material.base_color.x() =
                            mesh_material.baseColor.r();
                    loaded_material.base_color.y() =
                            mesh_material.baseColor.g();
                    loaded_material.base_color.z() =
                            mesh_material.baseColor.b();
                    loaded_material.base_roughness =
                            mesh_material.baseRoughness;
                    loaded_material.base_reflectance =
                            mesh_material.baseReflectance;
                    loaded_material.base_clearcoat =
                            mesh_material.baseClearCoat;
                    loaded_material.base_clearcoat_roughness =
                            mesh_material.baseClearCoatRoughness;
                    loaded_material.base_anisotropy =
                            mesh_material.baseAnisotropy;
                    loaded_material.albedo_img = mesh_material.albedo;
                    loaded_material.normal_img = mesh_material.normalMap;
                    loaded_material.ao_img = mesh_material.ambientOcclusion;
                    loaded_material.metallic_img = mesh_material.metallic;
                    loaded_material.roughness_img = mesh_material.roughness;
                    loaded_material.reflectance_img = mesh_material.reflectance;
                    loaded_material.clearcoat_img = mesh_material.clearCoat;
                    loaded_material.clearcoat_roughness_img =
                            mesh_material.clearCoatRoughness;
                    loaded_material.anisotropy_img = mesh_material.anisotropy;
                    loaded_material.shader = "defaultLit";
                    if (is_map_valid(mesh_material.metallic)) {
                        loaded_material.base_metallic = 1.f;
                        albedo_only = false;
                    }
                    if (albedo_only) {
                        albedo_only =
                                !is_map_valid(mesh_material.normalMap) &&
                                !is_map_valid(mesh_material.ambientOcclusion) &&
                                !is_map_valid(mesh_material.roughness) &&
                                !is_map_valid(mesh_material.reflectance) &&
                                !is_map_valid(mesh_material.clearCoat) &&
                                !is_map_valid(
                                        mesh_material.clearCoatRoughness) &&
                                !is_map_valid(mesh_material.anisotropy);
                    }
                    material_is_loaded = true;
                }

                if ((mesh->HasVertexColors() && !MeshHasUniformColor(*mesh)) ||
                    (mesh->HasMaterials() && albedo_only)) {
                    loaded_material.shader = "defaultUnlit";
                    num_unlit += 1;
                } else {
                    loaded_material.shader = "defaultLit";
                }
            } break;
            default:
                utility::LogWarning("Geometry type {} not supported!",
                                    (int)g->GetGeometryType());
                break;
        }

        auto g3 = std::static_pointer_cast<const geometry::Geometry3D>(g);
        scene3d->AddGeometry(g3, loaded_material);
        bounds += scene3d->GetScene()->GetGeometryBoundingBox("__model__");
        if (material_is_loaded) {
            impl_->settings_.have_loaded_material_ = true;
            impl_->settings_.loaded_material_ = loaded_material;
            impl_->settings_.lit_material_ = loaded_material;
            impl_->settings_.lit_material_.shader = "defaultLit";
            impl_->settings_.unlit_material_ = loaded_material;
            impl_->settings_.unlit_material_.shader = "defaultUnlit";
        }
    }

    if (!geometries.empty()) {
        if (num_point_clouds == geometries.size()) {
            impl_->settings_.model_.SetDisplayingPointClouds(true);
            if (!impl_->settings_.model_.GetUserHasChangedLightingProfile()) {
                auto &profile =
                        GuiSettingsModel::GetDefaultPointCloudLightingProfile();
                impl_->settings_.model_.SetLightingProfile(profile);
            }
        } else {
            impl_->settings_.model_.SetDisplayingPointClouds(false);
        }

        auto type = impl_->settings_.model_.GetMaterialType();
        if (type == GuiSettingsModel::MaterialType::LIT ||
            type == GuiSettingsModel::MaterialType::UNLIT) {
            if (num_unlit == geometries.size()) {
                impl_->settings_.model_.SetMaterialType(
                        GuiSettingsModel::MaterialType::UNLIT);
            } else {
                impl_->settings_.model_.SetMaterialType(
                        GuiSettingsModel::MaterialType::LIT);
            }
        }
    }

    impl_->settings_.model_.UnsetCustomDefaultColor();
    if (impl_->settings_.have_loaded_material_) {
        Eigen::Vector3f color(
                impl_->settings_.loaded_material_.base_color.data());
        impl_->settings_.model_.SetCustomDefaultColor(color);
        impl_->settings_.model_.SetCurrentMaterials(
                GuiSettingsModel::MATERIAL_FROM_FILE_NAME);
        impl_->settings_.view_->ShowFileMaterialEntry(true);
    } else {
        impl_->settings_.view_->ShowFileMaterialEntry(false);
    }
    impl_->settings_.view_->Update();  // make sure prefab material is correct

    impl_->scene_wgt_->SetupCamera(60.0, bounds,
                                   bounds.GetCenter().cast<float>());
}

void GuiVisualizer::Layout(const gui::Theme &theme) {
    auto r = GetContentRect();
    const auto em = theme.font_size;
    impl_->scene_wgt_->SetFrame(r);

    // Draw help keys HUD in upper left
    const auto pref = impl_->help_keys_->CalcPreferredSize(theme);
    impl_->help_keys_->SetFrame(gui::Rect(0, r.y, pref.width, pref.height));
    impl_->help_keys_->Layout(theme);

    // Draw camera HUD in lower left
    const auto prefcam = impl_->help_camera_->CalcPreferredSize(theme);
    impl_->help_camera_->SetFrame(gui::Rect(0, r.height + r.y - prefcam.height,
                                            prefcam.width, prefcam.height));
    impl_->help_camera_->Layout(theme);

    // Settings in upper right
    const auto LIGHT_SETTINGS_WIDTH = 18 * em;
    auto light_settings_size =
            impl_->settings_.wgt_base->CalcPreferredSize(theme);
    gui::Rect lightSettingsRect(r.width - LIGHT_SETTINGS_WIDTH, r.y,
                                LIGHT_SETTINGS_WIDTH,
                                std::min(r.height, light_settings_size.height));
    impl_->settings_.wgt_base->SetFrame(lightSettingsRect);

    Super::Layout(theme);
}

bool GuiVisualizer::SetIBL(const char *path) {
    auto result = impl_->SetIBL(GetRenderer(), path);
    PostRedraw();
    return result;
}

void GuiVisualizer::LoadGeometry(const std::string &path) {
    auto progressbar = std::make_shared<gui::ProgressBar>();
    gui::Application::GetInstance().PostToMainThread(this, [this, path,
                                                            progressbar]() {
        auto &theme = GetTheme();
        auto loading_dlg = std::make_shared<gui::Dialog>("Loading");
        auto vert =
                std::make_shared<gui::Vert>(0, gui::Margins(theme.font_size));
        auto loading_text = std::string("Loading ") + path;
        vert->AddChild(std::make_shared<gui::Label>(loading_text.c_str()));
        vert->AddFixed(theme.font_size);
        vert->AddChild(progressbar);
        loading_dlg->AddChild(vert);
        ShowDialog(loading_dlg);
    });

    gui::Application::GetInstance().RunInThread([this, path, progressbar]() {
        auto UpdateProgress = [this, progressbar](float value) {
            gui::Application::GetInstance().PostToMainThread(
                    this,
                    [progressbar, value]() { progressbar->SetValue(value); });
        };

        auto geometry = std::shared_ptr<geometry::Geometry3D>();

        auto geometry_type = io::ReadFileGeometryType(path);

        auto mesh = std::make_shared<geometry::TriangleMesh>();
        bool mesh_success = false;
        if (geometry_type & io::CONTAINS_TRIANGLES) {
            try {
                mesh_success = io::ReadTriangleMesh(path, *mesh);
            } catch (...) {
                mesh_success = false;
            }
        }
        if (mesh_success) {
            if (mesh->triangles_.size() == 0) {
                utility::LogWarning(
                        "Contains 0 triangles, will read as point cloud");
                mesh.reset();
            } else {
                UpdateProgress(0.5);
                mesh->ComputeVertexNormals();
                if (mesh->vertex_colors_.empty()) {
                    mesh->PaintUniformColor({1, 1, 1});
                }
                UpdateProgress(0.666);
                geometry = mesh;
            }
            // Make sure the mesh has texture coordinates
            if (!mesh->HasTriangleUvs()) {
                mesh->triangle_uvs_.resize(mesh->triangles_.size() * 3,
                                           {0.0, 0.0});
            }
        } else {
            // LogError throws an exception, which we don't want, because this
            // might be a point cloud.
            utility::LogInfo("{} appears to be a point cloud", path.c_str());
            mesh.reset();
        }

        if (!geometry) {
            auto cloud = std::make_shared<geometry::PointCloud>();
            bool success = false;
            const float ioProgressAmount = 0.5f;
            try {
                io::ReadPointCloudOption opt;
                opt.update_progress = [ioProgressAmount,
                                       UpdateProgress](double percent) -> bool {
                    UpdateProgress(ioProgressAmount * percent / 100.0);
                    return true;
                };
                success = io::ReadPointCloud(path, *cloud, opt);
            } catch (...) {
                success = false;
            }
            if (success) {
                utility::LogInfo("Successfully read {}", path.c_str());
                UpdateProgress(ioProgressAmount);
                if (!cloud->HasNormals()) {
                    cloud->EstimateNormals();
                }
                UpdateProgress(0.666);
                cloud->NormalizeNormals();
                UpdateProgress(0.75);
                geometry = cloud;
            } else {
                utility::LogWarning("Failed to read points {}", path.c_str());
                cloud.reset();
            }
        }

        if (geometry) {
            gui::Application::GetInstance().PostToMainThread(
                    this, [this, geometry]() {
                        SetGeometry({geometry});
                        CloseDialog();
                    });
        } else {
            gui::Application::GetInstance().PostToMainThread(this, [this,
                                                                    path]() {
                CloseDialog();
                auto msg = std::string("Could not load '") + path + "'.";
                ShowMessageBox("Error", msg.c_str());
            });
        }
    });
}

void GuiVisualizer::ExportCurrentImage(int width,
                                       int height,
                                       const std::string &path) {
    impl_->scene_wgt_->GetScene()->GetScene()->RenderToImage(
            width, height,
            [this, path](std::shared_ptr<geometry::Image> image) mutable {
                if (!io::WriteImage(path, *image)) {
                    this->ShowMessageBox(
                            "Error", (std::string("Could not write image to ") +
                                      path + ".")
                                             .c_str());
                }
            });
}

void GuiVisualizer::OnMenuItemSelected(gui::Menu::ItemId item_id) {
    auto menu_id = MenuId(item_id);
    switch (menu_id) {
        case FILE_OPEN: {
            auto dlg = std::make_shared<gui::FileDialog>(
                    gui::FileDialog::Mode::OPEN, "Open Geometry", GetTheme());
            dlg->AddFilter(".ply .stl .obj .off .gltf .glb",
                           "Triangle mesh files (.ply, .stl, .obj, .off, "
                           ".gltf, .glb)");
            dlg->AddFilter(".xyz .xyzn .xyzrgb .ply .pcd .pts",
                           "Point cloud files (.xyz, .xyzn, .xyzrgb, .ply, "
                           ".pcd, .pts)");
            dlg->AddFilter(".ply", "Polygon files (.ply)");
            dlg->AddFilter(".stl", "Stereolithography files (.stl)");
            dlg->AddFilter(".obj", "Wavefront OBJ files (.obj)");
            dlg->AddFilter(".off", "Object file format (.off)");
            dlg->AddFilter(".gltf", "OpenGL transfer files (.gltf)");
            dlg->AddFilter(".glb", "OpenGL binary transfer files (.glb)");
            dlg->AddFilter(".xyz", "ASCII point cloud files (.xyz)");
            dlg->AddFilter(".xyzn", "ASCII point cloud with normals (.xyzn)");
            dlg->AddFilter(".xyzrgb",
                           "ASCII point cloud files with colors (.xyzrgb)");
            dlg->AddFilter(".pcd", "Point Cloud Data files (.pcd)");
            dlg->AddFilter(".pts", "3D Points files (.pts)");
            dlg->AddFilter("", "All files");
            dlg->SetOnCancel([this]() { this->CloseDialog(); });
            dlg->SetOnDone([this](const char *path) {
                this->CloseDialog();
                OnDragDropped(path);
            });
            ShowDialog(dlg);
            break;
        }
        case FILE_EXPORT_RGB: {
            auto dlg = std::make_shared<gui::FileDialog>(
                    gui::FileDialog::Mode::SAVE, "Save File", GetTheme());
            dlg->AddFilter(".png", "PNG images (.png)");
            dlg->AddFilter("", "All files");
            dlg->SetOnCancel([this]() { this->CloseDialog(); });
            dlg->SetOnDone([this](const char *path) {
                this->CloseDialog();
                auto r = GetContentRect();
                this->ExportCurrentImage(r.width, r.height, path);
            });
            ShowDialog(dlg);
            break;
        }
        case FILE_QUIT:
            gui::Application::GetInstance().Quit();
            break;
        case SETTINGS_LIGHT_AND_MATERIALS: {
            auto visibility = !impl_->settings_.wgt_base->IsVisible();
            impl_->settings_.wgt_base->SetVisible(visibility);
            auto menubar = gui::Application::GetInstance().GetMenubar();
            menubar->SetChecked(SETTINGS_LIGHT_AND_MATERIALS, visibility);

            // We need relayout because materials settings pos depends on light
            // settings visibility
            Layout(GetTheme());

            break;
        }
        case HELP_KEYS: {
            bool is_visible = !impl_->help_keys_->IsVisible();
            impl_->help_keys_->SetVisible(is_visible);
            auto menubar = gui::Application::GetInstance().GetMenubar();
            menubar->SetChecked(HELP_KEYS, is_visible);
            break;
        }
        case HELP_CAMERA: {
            bool is_visible = !impl_->help_camera_->IsVisible();
            impl_->help_camera_->SetVisible(is_visible);
            auto menubar = gui::Application::GetInstance().GetMenubar();
            menubar->SetChecked(HELP_CAMERA, is_visible);
            if (is_visible) {
                impl_->scene_wgt_->SetOnCameraChanged([this](rendering::Camera
                                                                     *cam) {
                    auto children = this->impl_->help_camera_->GetChildren();
                    auto set_text = [](const Eigen::Vector3f &v,
                                       std::shared_ptr<gui::Widget> label) {
                        auto l = std::dynamic_pointer_cast<gui::Label>(label);
                        l->SetText(fmt::format("[{:.2f} {:.2f} "
                                               "{:.2f}]",
                                               v.x(), v.y(), v.z())
                                           .c_str());
                    };
                    set_text(cam->GetPosition(), children[1]);
                    set_text(cam->GetForwardVector(), children[3]);
                    set_text(cam->GetLeftVector(), children[5]);
                    set_text(cam->GetUpVector(), children[7]);
                    this->SetNeedsLayout();
                });
            } else {
                impl_->scene_wgt_->SetOnCameraChanged(
                        std::function<void(rendering::Camera *)>());
            }
            break;
        }
        case HELP_ABOUT: {
            auto dlg = CreateAboutDialog(this);
            ShowDialog(dlg);
            break;
        }
        case HELP_CONTACT: {
            auto dlg = CreateContactDialog(this);
            ShowDialog(dlg);
            break;
        }
    }
}

void GuiVisualizer::OnDragDropped(const char *path) {
    auto title = std::string("Open3D - ") + path;
#if LOAD_IN_NEW_WINDOW
    auto frame = this->GetFrame();
    std::vector<std::shared_ptr<const geometry::Geometry>> nothing;
    auto vis = std::make_shared<GuiVisualizer>(nothing, title.c_str(),
                                               frame.width, frame.height,
                                               frame.x + 20, frame.y + 20);
    gui::Application::GetInstance().AddWindow(vis);
#else
    this->SetTitle(title);
    auto vis = this;
#endif  // LOAD_IN_NEW_WINDOW
    vis->LoadGeometry(path);
}

}  // namespace visualization
}  // namespace open3d
