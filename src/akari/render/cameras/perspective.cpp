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
#include <akari/core/logger.h>
#include <akari/render/scenegraph.h>
#include <akari/render/camera.h>
#include <akari/render/common.h>
namespace akari::render {
    class PerspectiveCamera : public Camera {
      public:
        Transform c2w, w2c, r2c, c2r;
        ivec2 _resolution;
        Float fov;
        Float lens_radius = 0.0f;
        Float focal_distance = 0.0f;
        void preprocess() {
            Transform m;
            m = Transform::scale(Vec3(1.0f / _resolution.x, 1.0f / _resolution.y, 1)) * m;
            m = Transform::scale(Vec3(2, 2, 1)) * m;
            m = Transform::translate(Vec3(-1, -1, 0)) * m;
            m = Transform::scale(Vec3(1, -1, 1)) * m;
            auto s = atan(fov / 2);
            if (_resolution.x > _resolution.y) {
                m = Transform::scale(Vec3(s, s * Float(_resolution.y) / _resolution.x, 1)) * m;
            } else {
                m = Transform::scale(Vec3(s * Float(_resolution.x) / _resolution.y, s, 1)) * m;
            }
            r2c = m;
            c2r = r2c.inverse();
        }

      public:
        PerspectiveCamera(const ivec2 &_resolution, const Transform &c2w, Float fov)
            : c2w(c2w), w2c(c2w.inverse()), _resolution(_resolution), fov(fov) {
            preprocess();
        }
        ivec2 resolution() const { return _resolution; }
        CameraSample generate_ray(const vec2 &u1, const vec2 &u2, const ivec2 &raster) const override {
            CameraSample sample;
            sample.p_lens = concentric_disk_sampling(u1) * lens_radius;
            sample.p_film = vec2(raster) + u2;
            sample.weight = 1;

            vec2 p = shuffle<0, 1>(r2c.apply_point(Vec3(sample.p_film.x, sample.p_film.y, 0.0f)));
            Ray ray(Vec3(0), Vec3(normalize(Vec3(p.x, p.y, 0) - Vec3(0, 0, 1))));
            if (lens_radius > 0 && focal_distance > 0) {
                Float ft = focal_distance / std::abs(ray.d.z);
                Vec3 pFocus = ray(ft);
                ray.o = Vec3(sample.p_lens.x, sample.p_lens.y, 0);
                ray.d = Vec3(normalize(pFocus - ray.o));
            }
            ray.o = c2w.apply_point(ray.o);
            ray.d = c2w.apply_vector(ray.d);
            sample.normal = c2w.apply_normal(Vec3(0, 0, -1.0f));
            sample.ray = ray;

            return sample;
        }
    };
    class PerspectiveCameraNode final : public CameraNode {
      public:
        vec3 position;
        vec3 rotation;
        ivec2 resolution_ = ivec2(512, 512);
        double fov = glm::radians(80.0f);
        void object_field(sdl::Parser &parser, sdl::ParserContext &ctx, const std::string &field,
                          const sdl::Value &value) override {
            if (field == "fov") {
                fov = glm::radians(value.get<double>().value());
            } else if (field == "rotation") {
                rotation = radians(load<vec3>(value));
            } else if (field == "position") {
                position = load<vec3>(value);
            } else if (field == "resolution") {
                resolution_ = load<ivec2>(value);
            }
        }
        std::shared_ptr<const Camera> create_camera(Allocator<> allocator) override {
            TRSTransform TRS{position, rotation, Vec3(1.0)};
            auto c2w = TRS();
            return make_pmr_shared<PerspectiveCamera>(allocator, resolution_, c2w, fov);
        }
        ivec2 resolution() const override { return resolution_; }
        void set_resolution(const ivec2 &res) override { resolution_ = res; }
    };
    AKR_EXPORT_NODE(PerspectiveCamera, PerspectiveCameraNode)
} // namespace akari::render