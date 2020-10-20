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

#include <akari/core/color.h>
#include <akari/core/image.hpp>
#include <akari/render/scenegraph.h>

namespace akari::render {
    struct ShadingPoint {
        ShadingPoint() = default;
        ShadingPoint(const vec2 &tc) : texcoords(tc) {}
        vec2 texcoords;
    };
    class Texture {
      public:
        virtual Spectrum evaluate(const ShadingPoint &sp) const = 0;
        virtual Float integral() const = 0;
    };
    class TextureNode : public SceneGraphNode {
      public:
        virtual Texture *create_texture(Allocator<> *) = 0;
    };
    AKR_EXPORT std::shared_ptr<TextureNode> create_constant_texture();
    AKR_EXPORT std::shared_ptr<TextureNode> create_image_texture();
    AKR_EXPORT std::shared_ptr<TextureNode> create_constant_texture_rgb(const RGB &value);
    AKR_EXPORT std::shared_ptr<TextureNode> create_image_texture(const std::string &path);
} // namespace akari::render