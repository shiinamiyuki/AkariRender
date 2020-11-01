// MIT License
//
// Copyright (c) 2020 椎名深雪
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <csignal>
#include <akari/core/profiler.h>
#include <akari/render/scene.h>
#include <akari/render/camera.h>
#include <akari/render/integrator.h>
#include <akari/render/material.h>
#include <akari/render/mesh.h>
#include <akari/render/light.h>
#include <akari/render/denoiser.h>
namespace akari::render {
    void SceneNode::commit() {
        for (auto &shape : shapes) {
            AKR_ASSERT_THROW(shape);
            shape->commit();
        }
        AKR_ASSERT_THROW(camera);
        camera->commit();
    }
    void SceneNode::init_scene(Allocator<> allocator) {
        scene = std::make_shared<Scene>();
        auto &light_pdf_map = scene->light_pdf_map;
        lights.clear();
        scene->camera = camera->create_camera(allocator);
        auto &instances = scene->meshes;
        for (auto &shape : shapes) {
            instances.emplace_back(shape->create_instance(allocator));
        }
        scene->accel = accel->create_accel(*scene);
        std::vector<const Light *> area_lights;
        std::vector<Float> power;
        for (uint32_t mesh_id = 0; mesh_id < scene->meshes.size(); mesh_id++) {
            MeshInstance &mesh = scene->meshes[mesh_id];
            for (uint32_t prim_id = 0; prim_id < mesh.indices.size() / 3; prim_id++) {
                auto triangle = scene->get_triangle(mesh_id, prim_id);
                auto material = triangle.material;
                if (!material)
                    continue;
                if (auto e = material->as_emissive()) {
                    auto light_node = e->light;
                    auto light = light_node->create(allocator, scene.get(), triangle);
                    power.emplace_back(light->power());
                    lights.emplace_back(light);
                }
            }
        }

        if (envmap) {
            scene->envmap = envmap->create(allocator, scene.get(), std::nullopt);
            power.emplace_back(scene->envmap->power());
            lights.emplace_back(scene->envmap);
        }
        if (envmap) {
            lights.emplace_back(scene->envmap);
        }
        auto light_distribution = std::make_unique<Distribution1D>(power.data(), power.size(), Allocator<>());
        AKR_ASSERT(lights.size() == power.size());
        for (size_t i = 0; i < lights.size(); i++) {
            light_pdf_map.emplace(lights[i].get(), light_distribution->pdf_discrete(i));
        }
        scene->light_distribution = std::move(light_distribution);
        scene->lights = std::move(lights);
        scene->sampler = sampler->create_sampler(allocator);
    }
    void SceneNode::render() {
        // Thanks to python hijacking SIGINT handler;
        /* We want to restore the SIGINT handler so that the user can interrupt the renderer */
        auto _prev_SIGINT_handler = signal(SIGINT, SIG_DFL);
        auto _restore_handler = AtScopeExit([=]() { signal(SIGINT, _prev_SIGINT_handler); });
        commit();
        if (spp_override > 0) {
            if (!integrator->set_spp(spp_override)) {
                warning("cannot override spp");
            }
        }
        info("preparing scene");

        Allocator<> allocator(&memory_arena);
        init_scene(allocator);

        auto real_integrator = integrator->create_integrator(allocator);
        ivec2 res = scene->camera->resolution();
        // if (super_sampling_k > 1) {
        //     auto s = std::sqrt(super_sampling_k);
        //     if (s * s != super_sampling_k) {
        //         error("super sampling factor must be square number (got {})", super_sampling_k);
        //         std::exit(1);
        //     }
        //     res *= s;
        // }
        auto film = Film(res);

        Timer timer;
        real_integrator->render(scene.get(), &film);
        info("render done ({}s)", timer.elapsed_seconds());
        if (!run_denoiser_) {
            film.write_image(fs::path(output));
        } else {
            film.write_image(fs::path(output + std::string(".unfiltered.png")));
            AOV aov;
            auto render_aov = [&](const char *name) {
                auto aov_integrator_node = make_aov_integrator(std::min(64, integrator->get_spp()), name);
                auto integrator = aov_integrator_node->create_integrator(allocator);
                Film aov_film(res);
                integrator->render(scene.get(), &aov_film);
                aov.aovs[name] = aov_film.to_rgba_image();
            };
            aov.aovs["color"] = film.to_rgba_image();
            render_aov("normal");
            render_aov("albedo");
            PluginManager<Denoiser> denoisers;
            info("denoising...");
            auto pi = denoisers.load_plugin("OIDNDenoiser");
            auto denoiser = pi->make_shared();
            auto output_image = denoiser->denoise(scene.get(), aov);
            if (output_image) {
                default_image_writer()->write(*output_image, fs::path(output), GammaCorrection());
            }
            denoiser = nullptr;
        }

        finalize();
    }
    void SceneNode::finalize() {
        scene.reset();
        lights.clear();
        accel->finalize();
        integrator->finalize();
        camera->finalize();
        sampler->finalize();
        envmap->finalize();
        for (auto &shape : shapes) {
            shape->finalize();
        }
    }
    void SceneNode::object_field(sdl::Parser &parser, sdl::ParserContext &ctx, const std::string &field,
                                 const sdl::Value &value) {
        if (field == "camera") {
            camera = dyn_cast<CameraNode>(value.object());
            AKR_ASSERT_THROW(camera);
        } else if (field == "output") {
            output = value.get<std::string>().value();
        } else if (field == "integrator") {
            integrator = dyn_cast<IntegratorNode>(value.object());
            AKR_ASSERT_THROW(integrator);
        } else if (field == "shapes") {
            AKR_ASSERT_THROW(value.is_array());
            for (auto shape : value) {
                shapes.emplace_back(dyn_cast<MeshNode>(shape.object()));
            }
        } else if (field == "sampler") {
            sampler = dyn_cast<SamplerNode>(value.object());
            AKR_ASSERT_THROW(sampler);
        } else if (field == "accelerator") {
            accel = dyn_cast<AcceleratorNode>(value.object());
            AKR_ASSERT_THROW(accel);
        } else if (field == "envmap") {
            envmap = dyn_cast<LightNode>(value.object());
            AKR_ASSERT_THROW(envmap);
        }
    }
} // namespace akari::render