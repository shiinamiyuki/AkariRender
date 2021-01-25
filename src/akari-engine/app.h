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
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <akari/util.h>
#include <akari-engine/util.h>

namespace akari::engine {
    class AppWindow {
        const std::string title;
        ivec2 size;
        GLFWwindow *window = nullptr;

        vk::SurfaceKHR surface;
        vk::UniqueInstance g_instance;
        vk::PhysicalDevice g_physical_device;
        vk::AllocationCallbacks g_allocator;
        vk::UniqueDevice g_device;
        vk::UniqueDescriptorPool g_descriptor_pool;
        vk::Queue g_queue;
        uint32_t g_queue_family = (uint32_t)-1;
        ImGui_ImplVulkanH_Window g_mainwindow_data;
        int g_MinImageCount = 2;

        // VkAllocationCallbacks *g_Allocator     = NULL;
        // VkInstance g_Instance                  = VK_NULL_HANDLE;
        // VkPhysicalDevice g_PhysicalDevice      = VK_NULL_HANDLE;
        // VkDevice g_Device                      = VK_NULL_HANDLE;
        // uint32_t g_QueueFamily                 = (uint32_t)-1;
        // VkQueue g_Queue                        = VK_NULL_HANDLE;
        // VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
        // VkPipelineCache g_PipelineCache        = VK_NULL_HANDLE;
        // VkDescriptorPool g_DescriptorPool      = VK_NULL_HANDLE;

        // ImGui_ImplVulkanH_Window g_MainWindowData;
        // int g_MinImageCount     = 2;
        // bool g_SwapChainRebuild = false;

        void init();
        void setup_vulkan(const char **extensions, uint32_t extensions_count);
        void setup_vulkan_window(int width, int height);

      public:
        AppWindow(const char *title, ivec2 size) : title(title), size(size) {init();}
        void show();
        ~AppWindow();
    };
} // namespace akari::engine