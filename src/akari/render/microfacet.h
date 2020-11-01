// AUTO GENERATED BY AKARI UNIFIED SHADING LANGUAGE COMPILER
#pragma once
#include <akari/core/math.h>
#include <akari/render/geometry.h>
namespace akari::render {
    static const int32_t MicrofacetGGX = int32_t(0);
    static const int32_t MicrofacetBeckmann = int32_t(1);
    static const int32_t MicrofacetPhong = int32_t(2);
    inline float BeckmannD(float alpha, const glm::vec3 &m) {
        if ((m.y <= float(0.0)))
            return float(0.0);
        float c = cos2_theta(m);
        float t = tan2_theta(m);
        float a2 = (alpha * alpha);
        return (glm::exp((-t / a2)) / (((Pi * a2) * c) * c));
    }
    inline float BeckmannG1(float alpha, const glm::vec3 &v, const glm::vec3 &m) {
        if (((glm::dot(v, m) * v.y) <= float(0.0))) {
            return float(0.0);
        }
        float a = (float(1.0) / (alpha * tan_theta(v)));
        if ((a < float(1.6))) {
            return (((float(3.535) * a) + ((float(2.181) * a) * a)) /
                    ((float(1.0) + (float(2.276) * a)) + ((float(2.577) * a) * a)));
        } else {
            return float(1.0);
        }
    }
    inline float PhongG1(float alpha, const glm::vec3 &v, const glm::vec3 &m) {
        if (((glm::dot(v, m) * v.y) <= float(0.0))) {
            return float(0.0);
        }
        float a = (glm::sqrt(((float(0.5) * alpha) + float(1.0))) / tan_theta(v));
        if ((a < float(1.6))) {
            return (((float(3.535) * a) + ((float(2.181) * a) * a)) /
                    ((float(1.0) + (float(2.276) * a)) + ((float(2.577) * a) * a)));
        } else {
            return float(1.0);
        }
    }
    inline float PhongD(float alpha, const glm::vec3 &m) {
        if ((m.y <= float(0.0)))
            return float(0.0);
        return (((alpha + float(2.0)) / (float(2.0) * Pi)) * glm::pow(m.y, alpha));
    }
    inline float GGX_D(float alpha, const glm::vec3 &m) {
        if ((m.y <= float(0.0)))
            return float(0.0);
        float a2 = (alpha * alpha);
        float c2 = cos2_theta(m);
        float t2 = tan2_theta(m);
        float at = (a2 + t2);
        return (a2 / ((((Pi * c2) * c2) * at) * at));
    }
    inline float GGX_G1(float alpha, const glm::vec3 &v, const glm::vec3 &m) {
        if (((glm::dot(v, m) * v.y) <= float(0.0))) {
            return float(0.0);
        }
        return (float(2.0) / (float(1.0) + glm::sqrt((float(1.0) + ((alpha * alpha) * tan2_theta(m))))));
    }
    struct MicrofacetModel {
        int32_t type;
        float alpha;
    };
    inline MicrofacetModel microfacet_new(int32_t type, float roughness) {
        float alpha = float();
        if ((type == MicrofacetPhong)) {
            alpha = ((float(2.0) / (roughness * roughness)) - float(2.0));
        } else {
            alpha = roughness;
        }
        return MicrofacetModel{type, alpha};
    }
    inline float microfacet_D(const MicrofacetModel &model, const glm::vec3 &m) {
        int32_t type = model.type;
        float alpha = model.alpha;
        switch (type) {
        case MicrofacetBeckmann: {
            return BeckmannD(alpha, m);
        }
        case MicrofacetPhong: {
            return PhongD(alpha, m);
        }
        case MicrofacetGGX: {
            return GGX_D(alpha, m);
        }
        }
        return float(0.0);
    }
    inline float microfacet_G1(const MicrofacetModel &model, const glm::vec3 &v, const glm::vec3 &m) {
        int32_t type = model.type;
        float alpha = model.alpha;
        switch (type) {
        case MicrofacetBeckmann: {
            return BeckmannG1(alpha, v, m);
        }
        case MicrofacetPhong: {
            return PhongG1(alpha, v, m);
        }
        case MicrofacetGGX: {
            return GGX_G1(alpha, v, m);
        }
        }
        return float(0.0);
    }
    inline float microfacet_G(const MicrofacetModel &model, const glm::vec3 &i, const glm::vec3 &o,
                                      const glm::vec3 &m) {
        return (microfacet_G1(model, i, m) * microfacet_G1(model, o, m));
    }
    inline glm::vec3 microfacet_sample_wh(const MicrofacetModel &model, const glm::vec3 &wo,
                                                  const glm::vec2 &u) {
        int32_t type = model.type;
        float alpha = model.alpha;
        float phi = ((float(2.0) * Pi) * u.y);
        float cosTheta = float(0.0);
        switch (type) {
        case MicrofacetBeckmann: {
            float t2 = ((-alpha * alpha) * glm::log((float(1.0) - u.x)));
            cosTheta = (float(1.0) / glm::sqrt((float(1.0) + t2)));
            break;
        }
        case MicrofacetPhong: {
            cosTheta = glm::pow(u.x, float((float(1.0) / (alpha + float(2.0)))));
            break;
        }
        case MicrofacetGGX: {
            float t2 = (((alpha * alpha) * u.x) / (float(1.0) - u.x));
            cosTheta = (float(1.0) / glm::sqrt((float(1.0) + t2)));
            break;
        }
        }
        float sinTheta = glm::sqrt(glm::max(float(0.0), (float(1.0) - (cosTheta * cosTheta))));
        glm::vec3 wh = glm::vec3((glm::cos(phi) * sinTheta), cosTheta, (glm::sin(phi) * sinTheta));
        if (!same_hemisphere(wo, wh))
            wh = -wh;
        return wh;
    }
    inline float microfacet_evaluate_pdf(const MicrofacetModel &m, const glm::vec3 &wh) {
        return (microfacet_D(m, wh) * abs_cos_theta(wh));
    }
} // namespace akari::render
