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

#pragma once
#include <akari/util.h>
#include <akari/pmj02tables.h>
#include <akari/bluenoise.h>
#include <akari/image.h>
#include <akari/scenegraph.h>
#include <akari/render_xpu.h>
#include <array>
namespace akari::scene {
    class SceneGraph;
}
namespace akari::render {
    template <class T>
    struct VarianceTracker {
        std::optional<T> mean, m2;
        int count = 0;
        void update(T value) {
            if (count == 0) {
                mean = value;
                m2   = T(0.0);
            } else {
                auto delta = value - *mean;
                *mean += delta / T(count + 1);
                *m2 += delta * (value - *mean);
            }
            count++;
        }
        std::optional<T> variance() const {
            if (count < 2) {
                return std::nullopt;
            }
            return *m2 / float(count * count);
        }
    };

#pragma region distribution
    /*
     * Return the largest index i such that
     * pred(i) is true
     * If no such index i, last is returned
     * */
    template <typename Pred>
    int upper_bound(int first, int last, Pred pred) {
        int lo = first;
        int hi = last;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (pred(mid)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return std::clamp<int>(hi - 1, 0, (last - first) - 2);
    }

    struct Distribution1D {
        friend struct Distribution2D;
        Distribution1D(const Float *f, size_t n, Allocator<> allocator)
            : func(f, f + n, allocator), cdf(n + 1, allocator) {
            cdf[0] = 0;
            for (size_t i = 0; i < n; i++) {
                cdf[i + 1] = cdf[i] + func[i] / n;
            }
            funcInt = cdf[n];
            if (funcInt == 0) {
                for (uint32_t i = 1; i < n + 1; ++i)
                    cdf[i] = Float(i) / Float(n);
            } else {
                for (uint32_t i = 1; i < n + 1; ++i)
                    cdf[i] /= funcInt;
            }
        }
        // y = F^{-1}(u)
        // P(Y <= y) = P(F^{-1}(U) <= u) = P(U <= F(u)) = F(u)
        // Assume: 0 <= i < n
        [[nodiscard]] Float pdf_discrete(int i) const { return func[i] / (funcInt * count()); }
        [[nodiscard]] Float pdf_continuous(Float x) const {
            uint32_t offset = std::clamp<uint32_t>(static_cast<uint32_t>(x * count()), 0, count() - 1);
            return func[offset] / funcInt;
        }
        std::pair<uint32_t, Float> sample_discrete(Float u) const {
            uint32_t i = upper_bound(0, cdf.size(), [=](int idx) { return cdf[idx] <= u; });
            return {i, pdf_discrete(i)};
        }

        Float sample_continuous(Float u, Float *pdf = nullptr, int *p_offset = nullptr) const {
            uint32_t offset = upper_bound(0, cdf.size(), [=](int idx) { return cdf[idx] <= u; });
            if (p_offset) {
                *p_offset = offset;
            }
            Float du = u - cdf[offset];
            if ((cdf[offset + 1] - cdf[offset]) > 0)
                du /= (cdf[offset + 1] - cdf[offset]);
            if (pdf)
                *pdf = func[offset] / funcInt;
            return ((float)offset + du) / count();
        }

        [[nodiscard]] size_t count() const { return func.size(); }
        [[nodiscard]] Float integral() const { return funcInt; }

      private:
        astd::pmr::vector<Float> func, cdf;
        Float funcInt;
    };

    struct Distribution2D {
        Allocator<> allocator;
        astd::pmr::vector<Distribution1D> pConditionalV;
        std::shared_ptr<Distribution1D> pMarginal;

      public:
        Distribution2D(const Float *data, size_t nu, size_t nv, Allocator<> allocator_)
            : allocator(allocator_), pConditionalV(allocator) {
            pConditionalV.reserve(nv);
            for (auto v = 0u; v < nv; v++) {
                pConditionalV.emplace_back(&data[v * nu], nu, allocator);
            }
            std::vector<Float> m;
            for (auto v = 0u; v < nv; v++) {
                m.emplace_back(pConditionalV[v].funcInt);
            }
            pMarginal = make_pmr_shared<Distribution1D>(allocator, &m[0], nv, allocator);
        }
        Vec2 sample_continuous(const Vec2 &u, Float *pdf) const {
            int v;
            Float pdfs[2];
            auto d1 = pMarginal->sample_continuous(u[0], &pdfs[0], &v);
            auto d0 = pConditionalV[v].sample_continuous(u[1], &pdfs[1]);
            *pdf    = pdfs[0] * pdfs[1];
            return Vec2(d0, d1);
        }
        Float pdf_continuous(const Vec2 &p) const {
            auto iu = std::clamp<int>(p[0] * pConditionalV[0].count(), 0, pConditionalV[0].count() - 1);
            auto iv = std::clamp<int>(p[1] * pMarginal->count(), 0, pMarginal->count() - 1);
            return pConditionalV[iv].func[iu] / pMarginal->funcInt;
        }
    };
#pragma endregion
#pragma endregion
    struct Rng {
        Rng(uint64_t sequence = 0) { pcg32_init(sequence); }
        uint32_t uniform_u32() { return pcg32(); }
        double uniform_float() { return pcg32() / double(0xffffffff); }

      private:
        uint64_t state                   = 0x4d595df4d0f33173; // Or something seed-dependent
        static uint64_t const multiplier = 6364136223846793005u;
        static uint64_t const increment  = 1442695040888963407u; // Or an arbitrary odd constant
        static uint32_t rotr32(uint32_t x, unsigned r) { return x >> r | x << (-r & 31); }
        uint32_t pcg32(void) {
            uint64_t x     = state;
            unsigned count = (unsigned)(x >> 59); // 59 = 64 - 5

            state = x * multiplier + increment;
            x ^= x >> 18;                              // 18 = (64 - 27)/2
            return rotr32((uint32_t)(x >> 27), count); // 27 = 32 - 5
        }
        void pcg32_init(uint64_t seed) {
            state = seed + increment;
            (void)pcg32();
        }
    };
    // http://zimbry.blogspot.ch/2011/09/better-bit-mixing-improving-on.html
    inline uint64_t mix_bits(uint64_t v) {
        v ^= (v >> 31);
        v *= 0x7fb5d329728ea185;
        v ^= (v >> 27);
        v *= 0x81dadef4bc2dd44d;
        v ^= (v >> 33);
        return v;
    }
    inline int permutation_element(uint32_t i, uint32_t l, uint32_t p) {
        uint32_t w = l - 1;
        w |= w >> 1;
        w |= w >> 2;
        w |= w >> 4;
        w |= w >> 8;
        w |= w >> 16;
        do {
            i ^= p;
            i *= 0xe170893d;
            i ^= p >> 16;
            i ^= (i & w) >> 4;
            i ^= p >> 8;
            i *= 0x0929eb3f;
            i ^= p >> 23;
            i ^= (i & w) >> 1;
            i *= 1 | p >> 27;
            i *= 0x6935fa69;
            i ^= (i & w) >> 11;
            i *= 0x74dcb303;
            i ^= (i & w) >> 2;
            i *= 0x9e501cc3;
            i ^= (i & w) >> 2;
            i *= 0xc860a3df;
            i &= w;
            i ^= i >> 5;
        } while (i >= l);
        return (i + p) % l;
    }
    struct SamplerConfig {
        enum Type {
            PCG,
            LCG,
            PMJ02BN,
        };
        Type type           = Type::PCG;
        int pixel_tile_size = 16;
        int spp             = 16;
    };
    class PMJ02BNSampler {
        int spp = 0;
        int seed;
        int dimension = 0, sample_index = 0;
        ivec2 pixel;
        std::shared_ptr<vec2[]> pixel_samples;
        int pixel_tile_size = 16;

      public:
        void start_pixel_sample(ivec2 p, uint32_t idx, uint32_t dim) {
            pixel        = p;
            sample_index = idx;
            dimension    = std::max(2u, dim);
        }
        Float next1d() {
            uint64_t hash =
                mix_bits(((uint64_t)pixel.x << 48) ^ ((uint64_t)pixel.y << 32) ^ ((uint64_t)dimension << 16) ^ seed);
            int index   = permutation_element(sample_index, spp, hash);
            Float delta = blue_nosie(dimension, pixel);
            ++dimension;
            return std::min((index + delta) / spp, OneMinusEpsilon);
        }
        vec2 next2d() {
            if (dimension == 0) {
                // Return pmj02bn pixel sample
                int px = pixel.x % pixel_tile_size, py = pixel.y % pixel_tile_size;
                int offset = (px + py * pixel_tile_size) * spp;
                dimension += 2;
                return (pixel_samples.get())[offset + sample_index];

            } else {
                // Compute index for 2D pmj02bn sample
                int index       = sample_index;
                int pmjInstance = dimension / 2;
                if (pmjInstance >= N_PMJ02BN_SETS) {
                    // Permute index to be used for pmj02bn sample array
                    uint64_t hash = mix_bits(((uint64_t)pixel.x << 48) ^ ((uint64_t)pixel.y << 32) ^
                                             ((uint64_t)dimension << 16) ^ seed);
                    index         = permutation_element(sample_index, spp, hash);
                }

                // Return randomized pmj02bn sample for current dimension
                auto u = pmj02bn(pmjInstance, index);
                // Apply Cranley-Patterson rotation to pmj02bn sample _u_
                u += vec2(blue_nosie(dimension, pixel), blue_nosie(dimension + 1, pixel));
                if (u.x >= 1)
                    u.x -= 1;
                if (u.y >= 1)
                    u.y -= 1;

                dimension += 2;
                return {std::min(u.x, OneMinusEpsilon), std::min(u.y, OneMinusEpsilon)};
            }
        }
        void start_next_sample() {}
    };

    class PCGSampler {
        Rng rng;

      public:
        void set_sample_index(uint64_t idx) { rng = Rng(idx); }
        Float next1d() { return rng.uniform_float(); }
        vec2 next2d() { return vec2(next1d(), next1d()); }
        void start_next_sample() {}
        PCGSampler(uint64_t seed = 0u) : rng(seed) {}
    };
    class LCGSampler {
        uint32_t seed;

      public:
        void set_sample_index(uint64_t idx) { seed = idx & 0xffffffff; }
        Float next1d() {
            seed = (1103515245 * seed + 12345);
            return (Float)seed / (Float)0xFFFFFFFF;
        }
        vec2 next2d() { return vec2(next1d(), next1d()); }
        void start_next_sample() {}
        LCGSampler(uint64_t seed = 0u) : seed(seed) {}
    };

    struct MLTSampler {
        struct PrimarySample {
            Float value;
            Float _backup;
            uint64_t last_modification_iteration;
            uint64_t last_modified_backup;

            void backup() {
                _backup              = value;
                last_modified_backup = last_modification_iteration;
            }

            void restore() {
                value                       = _backup;
                last_modification_iteration = last_modified_backup;
            }
        };
        explicit MLTSampler(unsigned int seed) : rng(seed) {}
        Rng rng;
        std::vector<PrimarySample> X;
        uint64_t current_iteration = 0;
        bool large_step            = true;
        uint64_t last_large_step   = 0;
        Float large_step_prob      = 0.25;
        uint32_t sample_index      = 0;
        uint64_t accepts = 0, rejects = 0;
        Float uniform() { return rng.uniform_float(); }
        void start_next_sample() {
            sample_index = 0;
            current_iteration++;
            large_step = uniform() < large_step_prob;
        }
        void set_sample_index(uint64_t idx) { AKR_PANIC("shouldn't be called"); }
        Float next1d() {
            if (sample_index >= X.size()) {
                X.resize(sample_index + 1u);
            }
            auto &Xi = X[sample_index];
            mutate(Xi, sample_index);
            sample_index += 1;
            return Xi.value;
        }
        vec2 next2d() { return vec2(next1d(), next1d()); }
        double mutate(double x, double s1, double s2) {
            double r = uniform();
            if (r < 0.5) {
                r = r * 2.0;
                x = x + s2 * std::exp(-std::log(s2 / s1) * r);
                if (x > 1.0)
                    x -= 1.0;
            } else {
                r = (r - 0.5) * 2.0;
                x = x - s2 * std::exp(-std::log(s2 / s1) * r);
                if (x < 0.0)
                    x += 1.0;
            }
            return x;
        }
        void mutate(PrimarySample &Xi, int sampleIndex) {
            double s1, s2;
            s1 = 1.0 / 1024.0, s2 = 1.0 / 64.0;

            if (Xi.last_modification_iteration < last_large_step) {
                Xi.value                       = uniform();
                Xi.last_modification_iteration = last_large_step;
            }

            if (large_step) {
                Xi.backup();
                Xi.value = uniform();
            } else {
                int64_t nSmall = current_iteration - Xi.last_modification_iteration;

                auto nSmallMinus = nSmall - 1;
                if (nSmallMinus > 0) {
                    auto x = Xi.value;
                    while (nSmallMinus > 0) {
                        nSmallMinus--;
                        x = mutate(x, s1, s2);
                    }
                    Xi.value                       = x;
                    Xi.last_modification_iteration = current_iteration - 1;
                }
                Xi.backup();
                Xi.value = mutate(Xi.value, s1, s2);
            }

            Xi.last_modification_iteration = current_iteration;
        }
        void accept() {
            if (large_step) {
                last_large_step = current_iteration;
            }
            accepts++;
        }

        void reject() {
            for (PrimarySample &Xi : X) {
                if (Xi.last_modification_iteration == current_iteration) {
                    Xi.restore();
                }
            }
            rejects++;
            --current_iteration;
        }
    };

    struct ReplaySampler {
        explicit ReplaySampler(astd::pmr::vector<Float> Xs, Rng rng) : rng(rng), Xs(std::move(Xs)) {}
        Float next1d() {
            if (idx < Xs.size()) {
                return Xs[idx++];
            }
            idx++;
            return rng.uniform_float();
        }
        vec2 next2d() { return vec2(next1d(), next1d()); }
        void start_next_sample() { idx = 0; }
        void set_sample_index(uint64_t) {}

      private:
        uint32_t idx = 0;
        Rng rng;
        astd::pmr::vector<Float> Xs;
    };
    struct Sampler : Variant<LCGSampler, PCGSampler, MLTSampler, ReplaySampler> {
        using Variant::Variant;
        Sampler() : Sampler(PCGSampler()) {}
        Float next1d() { AKR_VAR_DISPATCH(next1d); }
        vec2 next2d() { AKR_VAR_DISPATCH(next2d); }
        void start_next_sample() { AKR_VAR_DISPATCH(start_next_sample); }
        void set_sample_index(uint64_t idx) { AKR_VAR_DISPATCH(set_sample_index, idx); }
    };

    struct Film {
        Array2D<Spectrum> radiance;
        Array2D<Float> weight;
        Array2D<std::array<AtomicFloat, Spectrum::size>> splats;
        explicit Film(const ivec2 &dimension) : radiance(dimension), weight(dimension), splats(dimension) {}
        void add_sample(const ivec2 &p, const Spectrum &sample, Float weight_) {
            weight(p) += weight_;
            radiance(p) += sample;
        }
        void splat(const ivec2 &p, const Spectrum &sample) {
            for (size_t i = 0; i < Spectrum::size; i++) {
                splats(p)[i].add(sample[i]);
            }
        }
        [[nodiscard]] ivec2 resolution() const { return radiance.dimension(); }
        Array2D<Spectrum> to_array2d() const {
            Array2D<Spectrum> array(resolution());
            thread::parallel_for(resolution().y, [&](uint32_t y, uint32_t) {
                for (int x = 0; x < resolution().x; x++) {
                    Spectrum splat_s;
                    for (size_t i = 0; i < Spectrum::size; i++) {
                        splat_s[i] = splats(x, y)[i].value();
                    }
                    if (weight(x, y) != 0) {
                        auto color  = (radiance(x, y)) / weight(x, y);
                        array(x, y) = color + splat_s;
                    } else {
                        auto color  = radiance(x, y);
                        array(x, y) = color + splat_s;
                    }
                }
            });
            return array;
        }
        template <typename = std::enable_if_t<std::is_same_v<Spectrum, Color3f>>>
        Image to_rgb_image() const {
            Image image = rgb_image(resolution());
            thread::parallel_for(resolution().y, [&](uint32_t y, uint32_t) {
                for (int x = 0; x < resolution().x; x++) {
                    Spectrum splat_s;
                    for (size_t i = 0; i < Spectrum::size; i++) {
                        splat_s[i] = splats(x, y)[i].value();
                    }
                    if (weight(x, y) != 0) {
                        auto color     = (radiance(x, y)) / weight(x, y) + splat_s;
                        image(x, y, 0) = color[0];
                        image(x, y, 1) = color[1];
                        image(x, y, 2) = color[2];
                    } else {
                        auto color     = radiance(x, y) + splat_s;
                        image(x, y, 0) = color[0];
                        image(x, y, 1) = color[1];
                        image(x, y, 2) = color[2];
                    }
                }
            });
            return image;
        }
    };
    
    struct Camera : Variant<PerspectiveCamera> {
        using Variant::Variant;
        ivec2 resolution() const { AKR_VAR_DISPATCH(resolution); }
        CameraSample generate_ray(const vec2 &u1, const vec2 &u2, const ivec2 &raster) const {
            AKR_VAR_DISPATCH(generate_ray, u1, u2, raster);
        }
    };
    struct ShadingPoint {
        Vec2 texcoords;
        Vec3 p;
        Vec3 dpdu, dpdv;
        Vec3 n;
        Vec3 dndu, dndv;
        Vec3 ng;
        ShadingPoint() = default;
        ShadingPoint(Vec2 tc) : texcoords(tc) {}
    };

    struct ConstantTexture {
        Spectrum value;
        ConstantTexture(Float v) : value(v) {}
        ConstantTexture(Spectrum v) : value(v) {}
        Float evaluate_f(const ShadingPoint &sp) const { return value[0]; }
        Spectrum evaluate_s(const ShadingPoint &sp) const { return value; }
    };

    struct DeviceImageImpl;
    using DeviceImage = DeviceImageImpl *;
    struct ImageTexture {
        std::shared_ptr<Image> image;
        ImageTexture() = default;
        ImageTexture(std::shared_ptr<Image> image) : image(std::move(image)) {}
        Float evaluate_f(const ShadingPoint &sp) const {
            vec2 texcoords = sp.texcoords;
            vec2 tc        = glm::mod(texcoords, vec2(1.0f));
            tc.y           = 1.0f - tc.y;
            return (*image)(tc, 0);
        }
        Spectrum evaluate_s(const ShadingPoint &sp) const {
            vec2 texcoords = sp.texcoords;
            vec2 tc        = glm::mod(texcoords, vec2(1.0f));
            tc.y           = 1.0f - tc.y;
            return Spectrum((*image)(tc, 0), (*image)(tc, 1), (*image)(tc, 2));
        }
    };

    struct Texture : Variant<ConstantTexture, ImageTexture> {
        using Variant::Variant;
        Float evaluate_f(const ShadingPoint &sp) const { AKR_VAR_DISPATCH(evaluate_f, sp); }
        Spectrum evaluate_s(const ShadingPoint &sp) const { AKR_VAR_DISPATCH(evaluate_s, sp); }
        Texture() : Texture(ConstantTexture(0.0)) {}
    };

    enum class BSDFType : int {
        Unset                = 0u,
        Reflection           = 1u << 0,
        Transmission         = 1u << 1,
        Diffuse              = 1u << 2,
        Glossy               = 1u << 3,
        Specular             = 1u << 4,
        DiffuseReflection    = Diffuse | Reflection,
        DiffuseTransmission  = Diffuse | Transmission,
        GlossyReflection     = Glossy | Reflection,
        GlossyTransmission   = Glossy | Transmission,
        SpecularReflection   = Specular | Reflection,
        SpecularTransmission = Specular | Transmission,
        All                  = Diffuse | Glossy | Specular | Reflection | Transmission
    };
    AKR_XPU inline BSDFType operator&(BSDFType a, BSDFType b) { return BSDFType((int)a & (int)b); }
    AKR_XPU inline BSDFType operator|(BSDFType a, BSDFType b) { return BSDFType((int)a | (int)b); }
    AKR_XPU inline BSDFType operator~(BSDFType a) { return BSDFType(~(uint32_t)a); }

    struct BSDFValue {
        Spectrum diffuse;
        Spectrum glossy;
        Spectrum specular;
        static BSDFValue zero() { return BSDFValue{Spectrum(0.0), Spectrum(0.0), Spectrum(0.0)}; }
        static BSDFValue with_diffuse(Spectrum diffuse) { return BSDFValue{diffuse, Spectrum(0.0), Spectrum(0.0)}; }
        static BSDFValue with_glossy(Spectrum glossy) { return BSDFValue{Spectrum(0.0), glossy, Spectrum(0.0)}; }
        static BSDFValue with_specular(Spectrum specular) { return BSDFValue{Spectrum(0.0), Spectrum(0.0), specular}; }
        // linear interpolation
        static BSDFValue mix(Float alpha, const BSDFValue &x, const BSDFValue &y) {
            return BSDFValue{(1.0f - alpha) * x.diffuse + alpha * y.diffuse,
                             (1.0f - alpha) * x.glossy + alpha * y.glossy,
                             (1.0f - alpha) * x.specular + alpha * y.specular};
        }
        BSDFValue operator*(Float k) const { return BSDFValue{diffuse * k, glossy * k, specular * k}; }
        friend BSDFValue operator*(Float k, const BSDFValue &f) { return f * k; }
        BSDFValue operator*(const Spectrum &k) const { return BSDFValue{diffuse * k, glossy * k, specular * k}; }
        friend BSDFValue operator*(const Spectrum &k, const BSDFValue &f) { return f * k; }
        BSDFValue operator+(const BSDFValue &rhs) const {
            return BSDFValue{diffuse + rhs.diffuse, glossy + rhs.glossy, specular + rhs.specular};
        }
        Spectrum operator()() const { return diffuse + glossy + specular; }
    };

    struct BSDFSample {
        Vec3 wi;
        BSDFValue f   = BSDFValue::zero();
        Float pdf     = 0.0;
        BSDFType type = BSDFType::Unset;
    };
    class BSDFClosure;
    class DiffuseBSDF {
        Spectrum R;

      public:
        DiffuseBSDF(const Spectrum &R) : R(R) {}
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const {

            if (same_hemisphere(wo, wi)) {
                return cosine_hemisphere_pdf(std::abs(cos_theta(wi)));
            }
            return 0.0f;
        }
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const {

            if (same_hemisphere(wo, wi)) {
                return BSDFValue::with_diffuse(R * InvPi);
            }
            return BSDFValue::with_diffuse(Spectrum(0.0f));
        }
        [[nodiscard]] BSDFType type() const { return BSDFType::DiffuseReflection; }
        std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const {
            BSDFSample sample;
            sample.wi = cosine_hemisphere_sampling(u);
            if (!same_hemisphere(wo, sample.wi)) {
                sample.wi.y = -sample.wi.y;
            }
            sample.type = type();
            sample.pdf  = cosine_hemisphere_pdf(std::abs(cos_theta(sample.wi)));
            sample.f    = BSDFValue::with_diffuse(R * InvPi);
            return sample;
        }
        [[nodiscard]] BSDFValue albedo() const { return BSDFValue::with_diffuse(R); }
    };

    class MicrofacetReflection {
      public:
        Spectrum R;
        MicrofacetModel model;
        Float roughness;
        MicrofacetReflection(const Spectrum &R, Float roughness)
            : R(R), model(microfacet_new(MicrofacetGGX, roughness)), roughness(roughness) {}
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const {
            if (same_hemisphere(wo, wi)) {
                auto wh = normalize(wo + wi);
                return microfacet_evaluate_pdf(model, wh) / (Float(4.0f) * dot(wo, wh));
            }
            return 0.0f;
        }
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const {
            if (same_hemisphere(wo, wi)) {
                Float cosThetaO = abs_cos_theta(wo);
                Float cosThetaI = abs_cos_theta(wi);
                auto wh         = (wo + wi);
                if (cosThetaI == 0 || cosThetaO == 0)
                    return BSDFValue::zero();
                if (wh.x == 0 && wh.y == 0 && wh.z == 0)
                    return BSDFValue::zero();
                wh = normalize(wh);
                if (wh.y < 0) {
                    wh = -wh;
                }
                auto F = 1.0f; // fresnel->evaluate(dot(wi, wh));

                return BSDFValue::with_glossy(R * (microfacet_D(model, wh) * microfacet_G(model, wo, wi, wh) * F /
                                                   (Float(4.0f) * cosThetaI * cosThetaO)));
            }
            return BSDFValue::zero();
        }
        [[nodiscard]] BSDFType type() const { return BSDFType::GlossyReflection; }
        [[nodiscard]] std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const {
            BSDFSample sample;
            sample.type = type();
            auto wh     = microfacet_sample_wh(model, wo, u);
            sample.wi   = glm::reflect(-wo, wh);
            if (!same_hemisphere(wo, sample.wi)) {
                sample.pdf = 0;
                return std::nullopt;
            } else {
                if (wh.y < 0) {
                    wh = -wh;
                }
                sample.pdf = microfacet_evaluate_pdf(model, wh) / (Float(4.0f) * abs(dot(wo, wh)));
                AKR_ASSERT(sample.pdf >= 0.0);
            }
            sample.f = evaluate(wo, sample.wi);
            return sample;
        }
        [[nodiscard]] BSDFValue albedo() const { return BSDFValue::with_glossy(R); }
    };

    class FresnelNoOp {
      public:
        [[nodiscard]] Spectrum evaluate(Float cosThetaI) const;
    };

    class FresnelConductor {
        const Spectrum etaI, etaT, k;

      public:
        FresnelConductor(const Spectrum &etaI, const Spectrum &etaT, const Spectrum &k)
            : etaI(etaI), etaT(etaT), k(k) {}
        [[nodiscard]] Spectrum evaluate(Float cosThetaI) const;
    };
    class FresnelDielectric {
        Float etaI, etaT;

      public:
        FresnelDielectric(const Float &etaI, const Float &etaT) : etaI(etaI), etaT(etaT) {}
        [[nodiscard]] Spectrum evaluate(Float cosThetaI) const;
    };
    class Fresnel : public Variant<FresnelConductor, FresnelDielectric, FresnelNoOp> {
      public:
        using Variant::Variant;
        Fresnel() : Variant(FresnelNoOp()) {}
        [[nodiscard]] Spectrum evaluate(Float cosThetaI) const { AKR_VAR_DISPATCH(evaluate, cosThetaI); }
    };
    class SpecularReflection {
        Spectrum R;

      public:
        SpecularReflection(const Spectrum &R) : R(R) {}
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const { return 0.0f; }
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const { return BSDFValue::zero(); }
        [[nodiscard]] BSDFType type() const { return BSDFType::SpecularReflection; }
        std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const {
            BSDFSample sample;
            sample.wi   = glm::reflect(-wo, vec3(0, 1, 0));
            sample.type = type();
            sample.pdf  = 1.0;
            sample.f    = BSDFValue::with_specular(R / (std::abs(cos_theta(sample.wi))));
            return sample;
        }
        [[nodiscard]] BSDFValue albedo() const { return BSDFValue::with_specular(R); }
    };
    class SpecularTransmission {
        Spectrum R;
        Float eta;

      public:
        SpecularTransmission(const Spectrum &R, Float eta) : R(R), eta(eta) {}
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const { return 0.0f; }
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const { return BSDFValue::zero(); }
        [[nodiscard]] BSDFType type() const { return BSDFType::SpecularTransmission; }
        std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const {
            BSDFSample sample;
            Float etaIO = same_hemisphere(wo, vec3(0, 1, 0)) ? eta : 1.0f / eta;
            auto wt     = refract(wo, faceforward(wo, vec3(0, 1, 0)), etaIO);
            if (glm::all(glm::equal(wt, vec3(0)))) {
                return std::nullopt;
            }
            sample.wi   = wt;
            sample.type = type();
            sample.pdf  = 1.0;
            sample.f    = BSDFValue::with_specular(R / (std::abs(cos_theta(sample.wi))));
            return sample;
        }
        [[nodiscard]] BSDFValue albedo() const { return BSDFValue::with_specular(R); }
    };

    class AKR_EXPORT FresnelSpecular {
        Spectrum R, T;
        Float etaA, etaB;
        FresnelDielectric fresnel;

      public:
        explicit FresnelSpecular(const Spectrum &R, const Spectrum &T, Float etaA, Float etaB)
            : R(R), T(T), etaA(etaA), etaB(etaB), fresnel(etaA, etaB) {}
        [[nodiscard]] BSDFType type() const { return BSDFType::SpecularTransmission | BSDFType::SpecularReflection; }
        [[nodiscard]] Float evaluate_pdf(const vec3 &wo, const vec3 &wi) const { return 0; }
        [[nodiscard]] BSDFValue evaluate(const vec3 &wo, const vec3 &wi) const { return BSDFValue::zero(); }
        [[nodiscard]] std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const;
        [[nodiscard]] BSDFValue albedo() const { return BSDFValue::with_specular((R + T) * 0.5); }
    };
    class MixBSDF {
      public:
        Float fraction;
        const BSDFClosure *bsdf_A = nullptr;
        const BSDFClosure *bsdf_B = nullptr;
        MixBSDF(Float fraction, const BSDFClosure *bsdf_A, const BSDFClosure *bsdf_B)
            : fraction(fraction), bsdf_A(bsdf_A), bsdf_B(bsdf_B) {}
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const;
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const;
        [[nodiscard]] BSDFType type() const;
        std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const;
        BSDFValue albedo() const;
    };

    /*
    All BSDFClosure except MixBSDF must have *only* one of Diffuse, Glossy, Specular


    */
    class BSDFClosure : public Variant<DiffuseBSDF, MicrofacetReflection, SpecularReflection, SpecularTransmission,
                                       FresnelSpecular, MixBSDF> {
      public:
        using Variant::Variant;
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const {
            AKR_VAR_DISPATCH(evaluate_pdf, wo, wi);
        }
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const { AKR_VAR_DISPATCH(evaluate, wo, wi); }
        [[nodiscard]] BSDFType type() const { AKR_VAR_DISPATCH(type); }
        [[nodiscard]] bool match_flags(BSDFType flag) const { return ((uint32_t)type() & (uint32_t)flag) != 0; }
        [[nodiscard]] std::optional<BSDFSample> sample(const vec2 &u, const Vec3 &wo) const {
            AKR_VAR_DISPATCH(sample, u, wo);
        }
        [[nodiscard]] BSDFValue albedo() const { AKR_VAR_DISPATCH(albedo); }
    };
    struct BSDFSampleContext {
        Float u0;
        Vec2 u1;
        const Vec3 wo;
    };
    class BSDF {
        std::optional<BSDFClosure> closure_;
        Frame frame;
        Float choice_pdf = 1.0f;

      public:
        BSDF(const Frame &frame) : frame(frame) {}
        bool null() const { return !closure_.has_value(); }
        void set_closure(const BSDFClosure &closure) { closure_ = closure; }
        void set_choice_pdf(Float pdf) { choice_pdf = pdf; }
        const BSDFClosure &closure() const { return *closure_; }
        [[nodiscard]] Float evaluate_pdf(const Vec3 &wo, const Vec3 &wi) const {
            auto pdf = closure().evaluate_pdf(frame.world_to_local(wo), frame.world_to_local(wi));
            return pdf * choice_pdf;
        }
        [[nodiscard]] BSDFValue evaluate(const Vec3 &wo, const Vec3 &wi) const {
            auto f = closure().evaluate(frame.world_to_local(wo), frame.world_to_local(wi));
            return f;
        }

        [[nodiscard]] BSDFType type() const { return closure().type(); }
        [[nodiscard]] bool is_pure_delta() const {
            auto ty = type();
            if (BSDFType::Unset == (ty & BSDFType::Specular))
                return false;
            if (BSDFType::Unset != (ty & BSDFType::Diffuse))
                return false;
            if (BSDFType::Unset != (ty & BSDFType::Glossy))
                return false;
            return true;
        }
        [[nodiscard]] bool match_flags(BSDFType flag) const { return closure().match_flags(flag); }
        std::optional<BSDFSample> sample(const BSDFSampleContext &ctx) const {
            auto wo = frame.world_to_local(ctx.wo);
            if (auto sample = closure().sample(ctx.u1, wo)) {
                sample->wi = frame.local_to_world(sample->wi);
                sample->pdf *= choice_pdf;
                return sample;
            }
            return std::nullopt;
        }
    };

    struct Light;
    struct Material;
    struct Medium;
    struct Triangle {
        std::array<Vec3, 3> vertices;
        std::array<Vec3, 3> normals;
        std::array<vec2, 3> texcoords;
        const Material *material = nullptr;
        const Light *light       = nullptr;
        Vec3 p(const vec2 &uv) const { return lerp3(vertices[0], vertices[1], vertices[2], uv); }
        Float area() const { return length(cross(vertices[1] - vertices[0], vertices[2] - vertices[0])) * 0.5f; }
        Vec3 ng() const { return normalize(cross(vertices[1] - vertices[0], vertices[2] - vertices[0])); }
        Vec3 ns(const vec2 &uv) const { return normalize(lerp3(normals[0], normals[1], normals[2], uv)); }
        vec2 texcoord(const vec2 &uv) const { return lerp3(texcoords[0], texcoords[1], texcoords[2], uv); }
        Vec3 dpdu(Float u) const { return dlerp3du(vertices[0], vertices[1], vertices[2], u); }
        Vec3 dpdv(Float v) const { return dlerp3du(vertices[0], vertices[1], vertices[2], v); }

        std::pair<Vec3, Vec3> dnduv(const vec2 &uv) const {
            auto n   = ns(uv);
            Float il = 1.0 / length(n);
            n *= il;
            auto dn_du = (normals[1] - normals[0]) * il;
            auto dn_dv = (normals[2] - normals[0]) * il;
            dn_du      = -n * dot(n, dn_du) + dn_du;
            dn_dv      = -n * dot(n, dn_dv) + dn_dv;
            return std::make_pair(dn_du, dn_dv);
        }

        std::optional<std::pair<Float, Vec2>> intersect(const Ray &ray) const {
            auto &v0 = vertices[0];
            auto &v1 = vertices[1];
            auto &v2 = vertices[2];
            Vec3 e1  = (v1 - v0);
            Vec3 e2  = (v2 - v0);
            Float a, f, u, v;
            auto h = cross(ray.d, e2);
            a      = dot(e1, h);
            if (a > Float(-1e-6f) && a < Float(1e-6f))
                return std::nullopt;
            f      = 1.0f / a;
            auto s = ray.o - v0;
            u      = f * dot(s, h);
            if (u < 0.0 || u > 1.0)
                return std::nullopt;
            auto q = cross(s, e1);
            v      = f * dot(ray.d, q);
            if (v < 0.0 || u + v > 1.0)
                return std::nullopt;
            Float t = f * dot(e2, q);
            if (t > ray.tmin && t < ray.tmax) {
                return std::make_pair(t, Vec2(u, v));
            } else {
                return std::nullopt;
            }
        }
    };
    struct Material;
    struct MeshInstance;
    struct SurfaceInteraction;
    struct Material {
        Texture color;
        Texture metallic;
        Texture roughness;
        Texture specular;
        Texture emission;
        Texture transmission;
        Material() {}
        BSDF evaluate(Sampler &sampler, Allocator<> alloc, const SurfaceInteraction &si) const;
    };

    struct MeshInstance {
        Transform transform;
        BufferView<const vec3> vertices;
        BufferView<const uvec3> indices;
        BufferView<const vec3> normals;
        BufferView<const vec2> texcoords;
        std::vector<const Light *> lights;
        const scene::Mesh *mesh  = nullptr;
        const Material *material = nullptr;
        const Medium *medium     = nullptr;

        Triangle get_triangle(int prim_id) const {
            Triangle trig;
            for (int i = 0; i < 3; i++) {
                trig.vertices[i] = transform.apply_vector(vertices[indices[prim_id][i]]);
                trig.normals[i]  = transform.apply_normal(normals[indices[prim_id][i]]);
                if (!texcoords.empty())
                    trig.texcoords[i] = texcoords[indices[prim_id][i]];
                else {
                    trig.texcoords[i] = vec2(i > 1, i % 2 == 0);
                }
            }
            trig.material = material;
            if (!lights.empty()) {
                trig.light = lights[prim_id];
            }
            return trig;
        }
    };
    inline Float phase_hg(Float cosTheta, Float g) {
        Float denom = 1 + g * g + 2 * g * cosTheta;
        return Inv4Pi * (1 - g * g) / (denom * std::sqrt(denom));
    }

    struct PhaseHG {
        const Float g;
        inline Float evaluate(const Float cos_theta) const { return phase_hg(cos_theta, g); }
        std::pair<Vec3, Float> sample(const Vec3 &wo, const Vec2 &u) const {
            Float cos_theta = 0.0;
            if (std::abs(g) < 1e-3)
                cos_theta = 1 - 2 * u[0];
            else {
                Float sqr = (1 - g * g) / (1 + g - 2 * g * u[0]);
                cos_theta = -(1 + g * g - sqr * sqr) / (2 * g);
            }
            auto sin_theta = std::sqrt(std::max<Float>(0, 1.0 - cos_theta * cos_theta));
            auto phi       = 2.0 * Pi * u[1];
            Frame frame(wo);
            auto wi = spherical_to_xyz(sin_theta, cos_theta, phi);
            return std::make_pair(frame.local_to_world(wi), evaluate(cos_theta));
        }
    };
    struct PhaseFunction : Variant<PhaseHG> {
        using Variant::Variant;
        Float evaluate(Float cos_theta) const { AKR_VAR_DISPATCH(evaluate, cos_theta); }
        std::pair<Vec3, Float> sample(const Vec3 &wo, const Vec2 &u) const { AKR_VAR_DISPATCH(sample, wo, u); }
    };
    struct MediumInteraction {
        Vec3 p;
        PhaseFunction phase;
    };
    struct HomogeneousMedium {
        const Spectrum sigma_a, sigma_s;
        const Spectrum sigma_t;
        const Float g;
        HomogeneousMedium(Spectrum sigma_a, Spectrum sigma_s, Float g)
            : sigma_a(sigma_a), sigma_s(sigma_s), sigma_t(sigma_a + sigma_s), g(g) {}
        Spectrum transmittance(const Ray &ray, Sampler &sampler) const {
            return exp(-sigma_t * std::min<Float>(ray.tmax * length(ray.d), MaxFloat));
        }
        std::pair<std::optional<MediumInteraction>, Spectrum> sample(const Ray &ray, Sampler &sampler,
                                                                     Allocator<> alloc) const {
            int channel        = std::min<int>(sampler.next1d() * Spectrum::size, Spectrum::size - 1);
            auto dist          = -std::log(1.0 - sampler.next1d()) / sigma_t[channel];
            auto t             = std::min<double>(dist * length(ray.d), ray.tmax);
            bool sample_medium = t < ray.tmax;
            std::optional<MediumInteraction> mi;
            if (sample_medium) {
                mi.emplace(MediumInteraction{ray(t), PhaseHG{g}});
            }
            auto tr          = transmittance(ray, sampler);
            Spectrum density = sample_medium ? sigma_t * tr : tr;
            Float pdf        = hsum(density);
            pdf /= Spectrum::size;
            return std::make_pair(mi, Spectrum(sample_medium ? (tr * sigma_s / pdf) : (tr / pdf)));
        }
    };
    struct Medium : Variant<HomogeneousMedium> {
        using Variant::Variant;
        Spectrum transmittance(const Ray &ray, Sampler &sampler) const {
            AKR_VAR_DISPATCH(transmittance, ray, sampler);
        }
        std::pair<std::optional<MediumInteraction>, Spectrum> sample(const Ray &ray, Sampler &sampler,
                                                                     Allocator<> alloc) const {
            AKR_VAR_DISPATCH(sample, ray, sampler, alloc);
        }
    };

    struct SurfaceInteraction {
        Triangle triangle;
        Vec3 p;
        Vec3 ng, ns;
        vec2 texcoords;
        Vec3 dndu, dndv;
        Vec3 dpdu, dpdv;
        const MeshInstance *shape = nullptr;
        SurfaceInteraction(const vec2 &uv, const Triangle &triangle)
            : triangle(triangle), p(triangle.p(uv)), ng(triangle.ng()), ns(triangle.ns(uv)),
              texcoords(triangle.texcoord(uv)) {
            dpdu                 = triangle.dpdu(uv[0]);
            dpdv                 = triangle.dpdu(uv[1]);
            std::tie(dndu, dndv) = triangle.dnduv(uv);
        }
        const Light *light() const { return triangle.light; }
        const Material *material() const { return triangle.material; }
        const Medium *medium() const { return shape->medium; }
        ShadingPoint sp() const {
            ShadingPoint sp_;
            sp_.n         = ns;
            sp_.texcoords = texcoords;
            sp_.dndu      = dndu;
            sp_.dndv      = dndv;
            sp_.dpdu      = dpdu;
            sp_.dpdv      = dpdv;
            return sp_;
        }
    };

    struct PointGeometry {
        Vec3 p;
        Vec3 n;
    };

    struct LightSampleContext {
        vec2 u;
        Vec3 p;
        Vec3 n = Vec3(0);
    };
    struct LightSample {
        Vec3 ng = Vec3(0.0f);
        Vec3 wi; // w.r.t to the luminated surface; normalized
        Float pdf = 0.0f;
        Spectrum I;
        Ray shadow_ray;
    };
    struct LightRaySample {
        Ray ray;
        Spectrum E;
        vec3 ng;
        vec2 uv; // 2D parameterized position
        Float pdfPos = 0.0, pdfDir = 0.0;
    };

    struct AreaLight {
        Triangle triangle;
        Texture color;
        bool double_sided = false;
        AreaLight(Triangle triangle, Texture color, bool double_sided)
            : triangle(triangle), color(color), double_sided(double_sided) {
            ng = triangle.ng();
        }
        Spectrum Le(const Vec3 &wo, const ShadingPoint &sp) const {
            bool face_front = dot(wo, ng) > 0.0;
            if (double_sided || face_front) {
                return color.evaluate_s(sp);
            }
            return Spectrum(0.0);
        }
        Float pdf_incidence(const PointGeometry &ref, const vec3 &wi) const {
            Ray ray(ref.p, wi);
            auto hit = triangle.intersect(ray);
            if (!hit) {
                return 0.0f;
            }
            Float SA = triangle.area() * (-glm::dot(wi, triangle.ng())) / (hit->first * hit->first);
            return 1.0f / SA;
        }
        LightRaySample sample_emission(Sampler &sampler) const {
            LightRaySample sample;
            sample.uv   = sampler.next2d();
            auto coords = uniform_sample_triangle(sample.uv);
            auto p      = triangle.p(coords);

            sample.ng     = triangle.ng();
            sample.pdfPos = 1.0 / triangle.area();
            auto w        = cosine_hemisphere_sampling(sampler.next2d());
            Frame local(sample.ng);
            sample.pdfDir = cosine_hemisphere_pdf(std::abs(w.y));
            sample.ray    = Ray(p, local.local_to_world(w));
            sample.E      = color.evaluate_s(ShadingPoint(triangle.texcoord(coords)));
            return sample;
        }
        LightSample sample_incidence(const LightSampleContext &ctx) const {
            auto coords = uniform_sample_triangle(ctx.u);
            auto p      = triangle.p(coords);
            LightSample sample;
            sample.ng     = triangle.ng();
            sample.wi     = p - ctx.p;
            auto dist_sqr = dot(sample.wi, sample.wi);
            sample.wi /= sqrt(dist_sqr);
            sample.I       = color.evaluate_s(ShadingPoint(triangle.texcoord(coords)));
            auto cos_theta = dot(sample.wi, sample.ng);
            if (-cos_theta < 0.0)
                sample.pdf = 0.0;
            else
                sample.pdf = dist_sqr / max(Float(0.0), -cos_theta) / triangle.area();
            // sample.shadow_ray = Ray(p, -sample.wi, Eps / std::abs(dot(sample.wi, sample.ng)),
            // sqrt(dist_sqr) * (Float(1.0f) - ShadowEps));
            sample.shadow_ray = Ray(ctx.p, sample.wi, Eps, sqrt(dist_sqr) * (Float(1.0f) - ShadowEps));
            return sample;
        }

      private:
        Vec3 ng;
    };
    struct Light : Variant<AreaLight> {
        using Variant::Variant;
        Spectrum Le(const Vec3 &wo, const ShadingPoint &sp) const { AKR_VAR_DISPATCH(Le, wo, sp); }
        Float pdf_incidence(const PointGeometry &ref, const vec3 &wi) const {
            AKR_VAR_DISPATCH(pdf_incidence, ref, wi);
        }
        LightRaySample sample_emission(Sampler &sampler) const { AKR_VAR_DISPATCH(sample_emission, sampler); }
        LightSample sample_incidence(const LightSampleContext &ctx) const { AKR_VAR_DISPATCH(sample_incidence, ctx); }
    };
    struct Intersection {
        Float t = Inf;
        Vec2 uv;
        int geom_id = -1;
        int prim_id = -1;
        bool hit() const { return geom_id != -1; }
    };
    struct Scene;
    class EmbreeAccel {
      public:
        virtual void build(const Scene &scene, const std::shared_ptr<scene::SceneGraph> &scene_graph) = 0;
        virtual std::optional<Intersection> intersect1(const Ray &ray) const                          = 0;
        virtual bool occlude1(const Ray &ray) const                                                   = 0;
        virtual Bounds3f world_bounds() const                                                         = 0;
    };
    std::shared_ptr<EmbreeAccel> create_embree_accel();

    struct PowerLightSampler {
        PowerLightSampler(Allocator<> alloc, BufferView<const Light *> lights_, const std::vector<Float> &power)
            : light_distribution(power.data(), power.size(), alloc), lights(lights_) {
            for (uint32_t i = 0; i < power.size(); i++) {
                light_pdf[lights[i]] = light_distribution.pdf_discrete(i);
            }
        }
        Distribution1D light_distribution;
        BufferView<const Light *> lights;
        std::unordered_map<const Light *, Float> light_pdf;
        std::pair<const Light *, Float> sample(Vec2 u) const {
            auto [light_idx, pdf] = light_distribution.sample_discrete(u[0]);
            return std::make_pair(lights[light_idx], pdf);
        }
        Float pdf(const Light *light) const {
            auto it = light_pdf.find(light);
            if (it == light_pdf.end()) {
                return 0.0;
            }
            return it->second;
        }
    };
    struct LightSampler : Variant<std::shared_ptr<PowerLightSampler>> {
        using Variant::Variant;
        std::pair<const Light *, Float> sample(Vec2 u) const { AKR_VAR_PTR_DISPATCH(sample, u); }
        Float pdf(const Light *light) const { AKR_VAR_PTR_DISPATCH(pdf, light); }
    };
    struct Scene {
        std::optional<Camera> camera;
        std::vector<MeshInstance> instances;
        std::vector<const Material *> materials;
        std::vector<const Light *> lights;
        std::shared_ptr<EmbreeAccel> accel;
        Allocator<> allocator;
        std::optional<LightSampler> light_sampler;
        astd::pmr::monotonic_buffer_resource *rsrc;
        std::optional<SurfaceInteraction> intersect(const Ray &ray) const;
        bool occlude(const Ray &ray) const;
        Scene()              = default;
        Scene(const Scene &) = delete;
        ~Scene();
    };

    std::shared_ptr<const Scene> create_scene(Allocator<>, const std::shared_ptr<scene::SceneGraph> &scene_graph);

    // experimental path space denoising
    struct PSDConfig {
        size_t filter_radius = 8;
    };
    struct PTConfig {
        Sampler sampler;
        int min_depth = 3;
        int max_depth = 5;
        int spp       = 16;
    };
    Film render_pt(PTConfig config, const Scene &scene);
    struct UPTConfig {
        Sampler sampler;
        int min_depth = 3;
        int max_depth = 5;
        int spp       = 16;
    };
    Image render_unified(UPTConfig config, const Scene &scene);
    Image render_pt_psd(PTConfig config, PSDConfig psd_config, const Scene &scene);

    // separate emitter direct hit
    // useful for MLT
    std::pair<Spectrum, Spectrum> render_pt_pixel_separete_emitter_direct(PTConfig config, Allocator<>,
                                                                          const Scene &scene, Sampler &sampler,
                                                                          const vec2 &p_film);
    inline Spectrum render_pt_pixel_wo_emitter_direct(PTConfig config, Allocator<> allocator, const Scene &scene,
                                                      Sampler &sampler, const vec2 &p_film) {
        auto [_, rest] = render_pt_pixel_separete_emitter_direct(config, allocator, scene, sampler, p_film);
        return rest - _;
    }
    inline Spectrum render_pt_pixel(PTConfig config, Allocator<> allocator, const Scene &scene, Sampler &sampler,
                                    const vec2 &p_film) {
        auto [_, rest] = render_pt_pixel_separete_emitter_direct(config, allocator, scene, sampler, p_film);
        return rest;
    }

    struct IRConfig {
        Sampler sampler;
        int min_depth = 3;
        int max_depth = 5;
        uint32_t spp  = 16;
    };

    // instant radiosity
    Image render_ir(IRConfig config, const Scene &scene);

    struct SMSConfig {
        Sampler sampler;
        int min_depth = 3;
        int max_depth = 5;
        int spp       = 16;
    };

    // sms single scatter
    Film render_sms_ss(SMSConfig config, const Scene &scene);
    struct BDPTConfig {
        Sampler sampler;
        int min_depth = 3;
        int max_depth = 5;
        int spp       = 16;
    };

    Image render_bdpt(PTConfig config, const Scene &scene);

    struct MLTConfig {
        int num_bootstrap = 100000;
        int num_chains    = 1024;
        int min_depth     = 3;
        int max_depth     = 5;
        int spp           = 16;
    };
    Image render_mlt(MLTConfig config, const Scene &scene);
    Image render_smcmc(MLTConfig config, const Scene &scene);
} // namespace akari::render
