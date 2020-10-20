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

#include <akari/render/scenegraph.h>
#include <akari/render/texture.h>
#include <akari/render/material.h>
#include <akari/core/color.h>
#include <akari/shaders/common.h>
namespace akari::render {
    using namespace shader;
    class DiffuseMaterial : public Material {
      public:
        DiffuseMaterial(const Texture *color) : color(color) {}
        const Texture *color;
        BSDF get_bsdf(MaterialEvalContext &ctx) const override {
            ShadingPoint sp;
            sp.texcoords = ctx.texcoords;
            auto R = color->evaluate(sp);
            BSDF bsdf(ctx.ng, ctx.ns);
            bsdf.set_closure(ctx.allocator.new_object<DiffuseBSDF>(R));
            return bsdf;
        }
    };


    class DiffuseMaterialNode final : public MaterialNode {
        std::shared_ptr<TextureNode> color;

      public:
        void object_field(sdl::Parser &parser, sdl::ParserContext &ctx, const std::string &field,
                          const sdl::Value &value) override {
            if (field == "color") {
                color = resolve_texture(value);
            }
        }
        Material *create_material(Allocator<> *allocator) override {
            return allocator->new_object<DiffuseMaterial>(color->create_texture(allocator));
        }
    };
    AKR_EXPORT_NODE(DiffuseMaterial, DiffuseMaterialNode)
} // namespace akari::render