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

#pragma once
#include <unordered_set>
#include <akari/core/math.h>
#include <akari/core/distribution.h>
#include <akari/core/film.h>
#include <akari/render/scenegraph.h>
namespace akari::render {
    class Scene;
    namespace AOVKind {
        static constexpr std::string_view ALBEDO = "albedo";
        static constexpr std::string_view NORMAL = "normal";
        static constexpr std::string_view VARIANCE = "variance";
        static constexpr std::string_view SHADOW = "shadow";
    }; // namespace AOVKind
    struct AOVRecord {
        std::optional<Film> value;
        std::optional<Film> variance;
    };
    struct AOVRequest {
        bool required_variance = false;
    };
    struct RenderOutput {
        std::unordered_map<std::string, AOVRecord> aovs;
    };
    struct RenderInput {
        const Scene *scene;
        std::unordered_map<std::string, AOVRequest> requested_aovs;
    };
    class Integrator {
      public:
        virtual RenderOutput render(const RenderInput &) = 0;
        virtual ~Integrator() = default;
    };
    // only output "color" channel
    class AKR_EXPORT UniAOVIntegrator : public Integrator {
      public:
        virtual void do_render(const Scene *scene, Film *film) = 0;
        RenderOutput render(const RenderInput &);
    };
    class IntegratorNode : public SceneGraphNode {
      public:
        virtual std::shared_ptr<Integrator> create_integrator(Allocator<>) = 0;
        virtual bool set_spp(int spp) = 0;
        virtual int get_spp() const = 0;
    };
    AKR_EXPORT std::shared_ptr<IntegratorNode> make_aov_integrator();
    AKR_EXPORT std::shared_ptr<IntegratorNode> make_aov_integrator(int spp, const char *aov);
} // namespace akari::render