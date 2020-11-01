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
#include <akari/core/math.h>
#include <akari/core/distribution.h>
#include <akari/core/film.h>
#include <akari/render/scenegraph.h>
namespace akari::render {
    class Scene;
    /*
    AOVS:
      color
      normal
      albedo
      first-diffuse-normal
      first-diffuse-albedo
    */
    struct RenderOutput {
        std::unordered_map<std::string, std::shared_ptr<Film>> aovs;
    };
    class Integrator {
      public:
        virtual void render(const Scene *scene, Film *out) = 0;
    };
    class IntegratorNode : public SceneGraphNode {
      public:
        virtual std::shared_ptr<Integrator>create_integrator(Allocator<>) = 0;
        virtual bool set_spp(int spp) = 0;
        virtual int get_spp() const = 0;
    };

    AKR_EXPORT std::shared_ptr<IntegratorNode> make_aov_integrator();
    AKR_EXPORT std::shared_ptr<IntegratorNode> make_aov_integrator(int spp, const char *aov);
} // namespace akari::render