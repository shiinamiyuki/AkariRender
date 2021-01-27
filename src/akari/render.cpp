// Copyright 2020 shiinamiyuki
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <akari/util.h>
#include <akari/api.h>
#include <akari/render.h>
#include <akari/render_ppg.h>
#include <spdlog/spdlog.h>
namespace akari::render {
    Spectrum FresnelNoOp::evaluate(Float cosThetaI) const { return Spectrum(1.0f); }
    Spectrum FresnelConductor::evaluate(Float cosThetaI) const { return fr_conductor(cosThetaI, etaI, etaT, k); }
    Spectrum FresnelDielectric::evaluate(Float cosThetaI) const {
        return Spectrum(fr_dielectric(cosThetaI, etaI, etaT));
    }
    [[nodiscard]] std::optional<BSDFSample> FresnelSpecular::sample(const vec2 &u, const Vec3 &wo) const {
        Float F = fr_dielectric(cos_theta(wo), etaA, etaB);
        AKR_ASSERT(F >= 0.0);
        BSDFSample sample;
        if (u[0] < F) {
            sample.wi = reflect(-wo, vec3(0, 1, 0));
            sample.pdf = F;
            sample.type = BSDFType::SpecularReflection;
            sample.f = BSDFValue::with_specular(F * R / abs_cos_theta(sample.wi));
        } else {
            bool entering = cos_theta(wo) > 0;
            Float etaI = entering ? etaA : etaB;
            Float etaT = entering ? etaB : etaA;
            auto wt = refract(wo, faceforward(wo, vec3(0, 1, 0)), etaI / etaT);
            if (!wt) {
                AKR_ASSERT(etaI > etaT);
                return std::nullopt;
            }
            Spectrum ft = T * (1 - F);
            sample.type = BSDFType::SpecularTransmission;

            ft *= (etaI * etaI) / (etaT * etaT);
            sample.pdf = 1 - F;
            sample.wi = *wt;
            sample.f = BSDFValue::with_specular(ft / abs_cos_theta(sample.wi));
        }
        return sample;
    }

    [[nodiscard]] Float MixBSDF::evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const {
        return (1.0 - fraction) * bsdf_A->evaluate_pdf(wo, wi) + fraction * bsdf_B->evaluate_pdf(wo, wi);
    }
    [[nodiscard]] BSDFValue MixBSDF::evaluate(const Vec3 &wo, const Vec3 &wi) const {
        return BSDFValue::mix(fraction, bsdf_A->evaluate(wo, wi), bsdf_B->evaluate(wo, wi));
    }
    [[nodiscard]] BSDFType MixBSDF::type() const { return BSDFType(bsdf_A->type() | bsdf_B->type()); }
    BSDFValue MixBSDF::albedo() const { return BSDFValue::mix(fraction, bsdf_A->albedo(), bsdf_B->albedo()); }
    std::optional<BSDFSample> MixBSDF::sample(const vec2 &u, const Vec3 &wo) const {
        BSDFSample sample;
        std::optional<BSDFSample> inner_sample;
        bool selA = true;
        if (u[0] < fraction) {
            vec2 u_(u[0] / fraction, u[1]);
            inner_sample = bsdf_B->sample(u_, wo);
            selA = false;
        } else {
            vec2 u_((u[0] - fraction) / (1.0 - fraction), u[1]);
            inner_sample = bsdf_A->sample(u_, wo);
        }
        if (!inner_sample) {
            return std::nullopt;
        }
        if ((inner_sample->type & BSDFType::Specular) != BSDFType::Unset) {
            sample = *inner_sample;
            sample.pdf *= selA ? fraction : (1.0f - fraction);
            // AKR_ASSERT(sample.pdf >= 0.0);
            // AKR_ASSERT(pdf_select >= 0.0);
            // AKR_ASSERT(sample.pdf >= 0.0);
            return sample;
        } else {
            sample = *inner_sample;
            if (selA) {
                sample.f = BSDFValue::mix(fraction, sample.f, bsdf_B->evaluate(wo, sample.wi));
                sample.pdf = (1.0 - fraction) * sample.pdf + fraction * bsdf_B->evaluate_pdf(wo, sample.wi);
            } else {
                sample.f = BSDFValue::mix(fraction, bsdf_A->evaluate(wo, sample.wi), sample.f);
                sample.pdf = fraction * sample.pdf + (1.0 - fraction) * bsdf_A->evaluate_pdf(wo, sample.wi);
            }
            return sample;
        }
    }
    BSDF Material::evaluate(Sampler &sampler, Allocator<> alloc, const SurfaceInteraction &si) const {
        auto sp = si.sp();
        BSDF bsdf(Frame(si.ns, si.dpdu));
        auto m = metallic.evaluate_f(sp);
        auto r = roughness.evaluate_f(sp);
        r *= r;
        auto tr = transmission.evaluate_f(sp);
        if (tr > 1 - 1e-5f) {
            bsdf.set_closure(FresnelSpecular(color.evaluate_s(sp), color.evaluate_s(sp), 1.0, 1.333));
        } else {
            auto base_color = color.evaluate_s(sp);
            // AKR_ASSERT(false);
            // MicrofacetReflection glossy(base_color, r);
            BSDFClosure glossy = [&]() -> BSDFClosure {
                if (r < 0.001) {
                    return SpecularReflection(base_color);
                } else {
                    return MicrofacetReflection(base_color, r);
                }
            }();
            auto diffuse = DiffuseBSDF(base_color);
            AKR_ASSERT(m >= 0 && m <= 1);
            if (m < 1e-5f) {
                bsdf.set_closure(diffuse);
            } else if (m > 1 - 1e-5f) {
                bsdf.set_closure(glossy);
            } else {
                MixBSDF mix(m, alloc.new_object<BSDFClosure>(diffuse), alloc.new_object<BSDFClosure>(glossy));
                bsdf.set_closure(mix);
            }
        }
        return bsdf;
    }
    bool Scene::occlude(const Ray &ray) const { return accel->occlude1(ray); }
    std::optional<SurfaceInteraction> Scene::intersect(const Ray &ray) const {
        std::optional<Intersection> isct = accel->intersect1(ray);
        if (!isct) {
            return std::nullopt;
        }
        Triangle triangle = instances[isct->geom_id].get_triangle(isct->prim_id);
        SurfaceInteraction si(isct->uv, triangle);
        si.shape = &instances[isct->geom_id];
        ray.tmax = isct->t;
        return si;
    }
    Scene::~Scene() {
        camera.reset();
        light_sampler.reset();
        materials.clear();
        lights.clear();
        delete rsrc;
    }
    std::shared_ptr<const Scene> create_scene(Allocator<> alloc,
                                              const std::shared_ptr<scene::SceneGraph> &scene_graph) {
        scene_graph->commit();
        auto scene = make_pmr_shared<Scene>(alloc);
        {
            auto rsrc = alloc.resource();
            scene->rsrc = new astd::pmr::monotonic_buffer_resource(rsrc);
            scene->allocator = Allocator<>(scene->rsrc);
        }
        scene->camera = [&] {
            std::optional<Camera> camera;
            if (auto perspective = scene_graph->camera->as<scene::PerspectiveCamera>()) {
                TRSTransform TRS{perspective->transform.translation, perspective->transform.rotation, Vec3(1.0)};
                auto c2w = TRS();
                camera.emplace(PerspectiveCamera(perspective->resolution, c2w, perspective->fov));
            }
            return camera;
        }();
        std::unordered_map<const scene::Material *, const Material *> mat_map;
        auto create_tex = [&](const scene::P<scene::Texture> &tex_node) -> Texture {
            if (!tex_node) {
                return Texture(ConstantTexture(0.0));
            }
            std::optional<Texture> tex;
            if (auto ftex = tex_node->as<scene::FloatTexture>()) {
                tex.emplace(ConstantTexture(ftex->value));
            } else if (auto rgb_tex = tex_node->as<scene::RGBTexture>()) {
                tex.emplace(ConstantTexture(rgb_tex->value));
            } else if (auto img_tex = tex_node->as<scene::ImageTexture>()) {
                std::shared_ptr<Image> img;
                img.reset(new Image(read_generic_image(img_tex->path)));
                tex.emplace(ImageTexture(std::move(img)));
            }
            return tex.value();
        };
        auto create_volume = [&](const scene::P<scene::Volume> &vol_node) -> const Medium * {
            if (!vol_node)
                return nullptr;
            if (auto homo = vol_node->as<scene::HomogeneousVolume>()) {
                auto vol =
                    HomogeneousMedium(homo->density * homo->absorption, homo->density * homo->color, homo->anisotropy);
                return scene->allocator.new_object<Medium>(vol);
            }
            return nullptr;
        };
        auto create_mat = [&](const scene::P<scene::Material> &mat_node) -> const Material * {
            if (!mat_node)
                return nullptr;
            auto it = mat_map.find(mat_node.get());
            if (it != mat_map.end())
                return it->second;
            auto mat = scene->allocator.new_object<Material>();
            mat->color = create_tex(mat_node->color);
            mat->metallic = create_tex(mat_node->metallic);
            mat->emission = create_tex(mat_node->emission);
            mat->roughness = create_tex(mat_node->roughness);
            mat->transmission = create_tex(mat_node->transmission);
            mat_map.emplace(mat_node.get(), mat);
            scene->materials.emplace_back(mat);
            return mat;
        };
        auto create_instance = [&](Transform parent_transform, scene::P<scene::Node> node, auto &&self) -> void {
            Transform node_T = parent_transform * node->transform();
            for (auto &instance : node->instances) {
                if (!instance)
                    continue;
                Transform T = node_T * instance->transform();
                MeshInstance inst;
                inst.transform = T;
                inst.material = create_mat(instance->material);
                inst.medium = create_volume(instance->volume);
                inst.indices = BufferView<const uvec3>(instance->mesh->indices.data(), instance->mesh->indices.size());
                inst.normals = BufferView<const vec3>(instance->mesh->normals.data(), instance->mesh->normals.size());
                inst.texcoords =
                    BufferView<const vec2>(instance->mesh->texcoords.data(), instance->mesh->texcoords.size());
                inst.vertices =
                    BufferView<const vec3>(instance->mesh->vertices.data(), instance->mesh->vertices.size());
                inst.mesh = instance->mesh.get();
                if (inst.material) {
                    if (inst.material->emission.isa<ConstantTexture>() &&
                        luminance(inst.material->emission.get<ConstantTexture>()->evaluate_s(ShadingPoint())) <= 0.0) {
                        // not emissive
                    } else {
                        // emissived
                        std::vector<const Light *> lights;
                        for (int i = 0; i < (int)inst.indices.size(); i++) {
                            AreaLight area_light(inst.get_triangle(i), inst.material->emission, false);
                            auto light = alloc.new_object<Light>(area_light);
                            scene->lights.emplace_back(light);
                            lights.emplace_back(light);
                        }
                        inst.lights = std::move(lights);
                    }
                }
                scene->instances.emplace_back(std::move(inst));
            }
            for (auto &child : node->children) {
                self(node_T, child, self);
            }
        };
        create_instance(Transform(), scene_graph->root, create_instance);
        {
            BufferView<const Light *> lights(scene->lights.data(), scene->lights.size());
            std::vector<Float> power;
            for (auto light : lights) {
                (void)light;
                power.emplace_back(1.0);
            }
            scene->light_sampler = std::make_shared<PowerLightSampler>(alloc, lights, power);
        }
        scene->accel = create_embree_accel();
        scene->accel->build(*scene, scene_graph);
        return scene;
    }
} // namespace akari::render

namespace akari {
    void render_scenegraph(scene::P<scene::SceneGraph> graph) {
        if (!graph->integrator) {
            std::cerr << "no integrator!" << std::endl;
            exit(1);
        }
        Allocator<> alloc;
        auto scene = render::create_scene(alloc, graph);
        if (auto pt = graph->integrator->as<scene::PathTracer>()) {
            render::PTConfig config;
            config.min_depth = pt->min_depth;
            config.max_depth = pt->max_depth;
            config.spp = pt->spp;
            config.sampler = render::PCGSampler();
            auto film = render::render_pt(config, *scene);
            auto image = film.to_rgb_image();
            write_generic_image(image, graph->output_path);
        } else if (auto upt = graph->integrator->as<scene::UnifiedPathTracer>()) {
            render::UPTConfig config;
            config.min_depth = upt->min_depth;
            config.max_depth = upt->max_depth;
            config.spp = upt->spp;
            config.sampler = render::PCGSampler();
            auto image = render::render_unified(config, *scene);
            write_generic_image(image, graph->output_path);
        } else if (auto bdpt = graph->integrator->as<scene::BDPT>()) {
            render::PTConfig config;
            config.min_depth = bdpt->min_depth;
            config.max_depth = bdpt->max_depth;
            config.spp = bdpt->spp;
            config.sampler = render::PCGSampler();
            auto image = render::render_bdpt(config, *scene);
            write_generic_image(image, graph->output_path);
        } else if (auto gpt = graph->integrator->as<scene::GuidedPathTracer>()) {
            render::PPGConfig config;
            config.min_depth = gpt->min_depth;
            config.max_depth = gpt->max_depth;
            config.spp = gpt->spp;
            config.sampler = render::PCGSampler();
            if (gpt->metropolized) {
                (void)render::render_metropolized_ppg(config, *scene);
            } else {
                auto image = render::render_ppg(config, *scene);
                write_generic_image(image, graph->output_path);
            }
        } else if (auto vpl = graph->integrator->as<scene::VPL>()) {
            render::IRConfig config;
            config.min_depth = vpl->min_depth;
            config.max_depth = vpl->max_depth;
            config.spp = vpl->spp;
            config.sampler = render::PCGSampler();
            auto image = render::render_ir(config, *scene);
            write_generic_image(image, graph->output_path);
        } else if (auto smcmc = graph->integrator->as<scene::SMCMC>()) {
            render::MLTConfig config;
            config.min_depth = smcmc->min_depth;
            config.max_depth = smcmc->max_depth;
            config.spp = smcmc->spp;
            auto image = render::render_smcmc(config, *scene);
            write_generic_image(image, graph->output_path);
        } else if (auto mcmc = graph->integrator->as<scene::MCMC>()) {
            render::MLTConfig config;
            config.min_depth = mcmc->min_depth;
            config.max_depth = mcmc->max_depth;
            config.spp = mcmc->spp;
            auto image = render::render_mlt(config, *scene);
            write_generic_image(image, graph->output_path);
        }
    }
} // namespace akari