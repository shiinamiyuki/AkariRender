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

#include <akari/render/cuda/gpuaccel.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
namespace akari::gpu {
    using namespace render;
    struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) RaygenRecord {
        alignas(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    };

    struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) MissRecord {
        alignas(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    };

    void GPUAccel::init_optix() {
        CUDA_CHECK(cudaFree(0));

        OptixDeviceContext context;
        CUcontext cu_ctx = 0; // zero means take the current context
        OPTIX_CHECK(optixInit());
        OptixDeviceContextOptions options = {};
        options.logCallbackFunction = [](unsigned int level, const char *tag, const char *message, void *cbdata) {
            if (level == 1) {
                fatal("optix: [{}] {}", tag, message);
            } else if (level == 2) {
                error("optix: [{}] {}", tag, message);
            } else if (level == 3) {
                warning("optix: [{}] {}", tag, message);
            } else if (level == 4) {
                info("optix: [{}] {}", tag, message);
            }
        };
        options.logCallbackLevel = 4;
        OPTIX_CHECK(optixDeviceContextCreate(cu_ctx, &options, &context));

        state.context = context;
    }
    void GPUAccel::build(const std::vector<OptixBuildInput> &inputs) {
        OptixAccelBuildOptions accel_options = {};
        accel_options.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
        accel_options.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes gas_buffer_sizes;
        OPTIX_CHECK(optixAccelComputeMemoryUsage(state.context, &accel_options, inputs.data(),
                                                 inputs.size(), // num_build_inputs
                                                 &gas_buffer_sizes));
        CUdeviceptr d_temp_buffer;
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_temp_buffer), gas_buffer_sizes.tempSizeInBytes));

        // non-compacted output
        CUdeviceptr d_buffer_temp_output_gas_and_compacted_size;
        size_t compactedSizeOffset = (gas_buffer_sizes.outputSizeInBytes + 7ull) & ~7ull;
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_buffer_temp_output_gas_and_compacted_size),
                              compactedSizeOffset + 8));

        OptixAccelEmitDesc emitProperty = {};
        emitProperty.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emitProperty.result = (CUdeviceptr)((char *)d_buffer_temp_output_gas_and_compacted_size + compactedSizeOffset);

        OPTIX_CHECK(optixAccelBuild(state.context,
                                    0, // CUDA stream
                                    &accel_options, inputs.data(),
                                    inputs.size(), // num build inputs
                                    d_temp_buffer, gas_buffer_sizes.tempSizeInBytes,
                                    d_buffer_temp_output_gas_and_compacted_size, gas_buffer_sizes.outputSizeInBytes,
                                    &state.gas_handle,
                                    &emitProperty, // emitted property list
                                    1              // num emitted properties
                                    ));
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(d_temp_buffer)));
    }
    OptixBuildInput GPUAccel::build(const MeshInstance &instance) {
        //
        // Build triangle GAS
        //
        uint32_t triangle_input_flags[1] = // One per SBT record for this build input
            {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        OptixBuildInput triangle_input = {};
        triangle_input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        triangle_input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        triangle_input.triangleArray.vertexStrideInBytes = sizeof(float) * 3;
        triangle_input.triangleArray.numVertices = static_cast<uint32_t>(instance.vertices.size() / 3);
        triangle_input.triangleArray.vertexBuffers = reinterpret_cast<CUdeviceptr const*>(&instance.vertices.data());
        triangle_input.triangleArray.flags = triangle_input_flags;
        triangle_input.triangleArray.numSbtRecords = 1;
        triangle_input.triangleArray.sbtIndexOffsetBuffer = reinterpret_cast<CUdeviceptr>(nullptr);
        triangle_input.triangleArray.sbtIndexOffsetSizeInBytes = 0;
        triangle_input.triangleArray.sbtIndexOffsetStrideInBytes = 0;
        return triangle_input;
    }
    void GPUAccel::build(const Scene *scene) {
        std::vector<OptixBuildInput> inputs;
        for (auto &instance : scene->meshes) {
            inputs.emplace_back(build(instance));
        }
        build(inputs);
    }
} // namespace akari::gpu