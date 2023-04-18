// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ppx/swapchain.h"

#include "ppx/config.h"
#include "ppx/grfx/grfx_device.h"
#include "ppx/grfx/grfx_enums.h"
#include "ppx/grfx/grfx_render_pass.h"
#include "ppx/grfx/grfx_instance.h"
#include "ppx/grfx/grfx_swapchain.h"
#include "ppx/grfx/grfx_queue.h"

namespace ppx {

// -------------------------------------------------------------------------------------------------
// Swapchain

grfx::Rect Swapchain::GetRenderArea() const
{
    return grfx::Rect{0, 0, GetImageWidth(), GetImageHeight()};
}

grfx::ImagePtr Swapchain::GetColorImage(uint32_t imageIndex) const
{
    grfx::ImagePtr object;
    GetColorImage(imageIndex, &object);
    return object;
}

grfx::ImagePtr Swapchain::GetDepthImage(uint32_t imageIndex) const
{
    grfx::ImagePtr object;
    GetDepthImage(imageIndex, &object);
    return object;
}

float Swapchain::GetAspect() const
{
    grfx::Rect rect = GetRenderArea();
    return static_cast<float>(rect.width) / rect.height;
}

grfx::Viewport Swapchain::GetViewport(float minDepth, float maxDepth) const
{
    grfx::Rect rect = GetRenderArea();
    return grfx::Viewport{
        static_cast<float>(rect.x),
        static_cast<float>(rect.y),
        static_cast<float>(rect.width),
        static_cast<float>(rect.height),
        minDepth,
        maxDepth,
    };
}

grfx::RenderPassPtr Swapchain::GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp) const
{
    grfx::RenderPassPtr object;
    GetRenderPass(imageIndex, loadOp, &object);
    return object;
}

// -------------------------------------------------------------------------------------------------
// DeviceSwapchainWrap

class DeviceSwapchainWrapImpl : public DeviceSwapchainWrap
{
public:
    static std::unique_ptr<DeviceSwapchainWrap> Create(grfx::Swapchain* swapchain);

public:
    uint32_t     GetImageCount() const final;
    grfx::Format GetColorFormat() const final;
    grfx::Format GetDepthFormat() const final;

    uint32_t GetImageWidth() const final;
    uint32_t GetImageHeight() const final;

    Result GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const final;
    Result GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const final;

    Result GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const final;

    Result AcquireNextImage(
        uint64_t         timeout,
        grfx::Semaphore* pSemaphore,
        grfx::Fence*     pFence,
        uint32_t*        pImageIndex) final;

    Result Present(
        uint32_t                      imageIndex,
        uint32_t                      waitSemaphoreCount,
        const grfx::Semaphore* const* ppWaitSemaphores) final;

    grfx::Device* GetDevice() const final;

    DeviceSwapchainWrapImpl(grfx::Swapchain* swapchain);

private:
    grfx::Swapchain* mSwapchain = nullptr;
};

std::unique_ptr<DeviceSwapchainWrap> DeviceSwapchainWrap::Create(grfx::Swapchain* swapchain)
{
    return std::unique_ptr<DeviceSwapchainWrap>(new DeviceSwapchainWrapImpl(swapchain));
}

DeviceSwapchainWrapImpl::DeviceSwapchainWrapImpl(grfx::Swapchain* swapchain)
    : mSwapchain(swapchain)
{
}

uint32_t DeviceSwapchainWrapImpl::GetImageCount() const
{
    return mSwapchain->GetImageCount();
}

grfx::Format DeviceSwapchainWrapImpl::GetColorFormat() const
{
    return mSwapchain->GetColorFormat();
}

grfx::Format DeviceSwapchainWrapImpl::GetDepthFormat() const
{
    return mSwapchain->GetDepthFormat();
}

uint32_t DeviceSwapchainWrapImpl::GetImageWidth() const
{
    return mSwapchain->GetWidth();
}

uint32_t DeviceSwapchainWrapImpl::GetImageHeight() const
{
    return mSwapchain->GetHeight();
}

Result DeviceSwapchainWrapImpl::GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const
{
    return mSwapchain->GetColorImage(imageIndex, ppImage);
}

Result DeviceSwapchainWrapImpl::GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const
{
    return mSwapchain->GetDepthImage(imageIndex, ppImage);
}

Result DeviceSwapchainWrapImpl::GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const
{
    return mSwapchain->GetRenderPass(imageIndex, loadOp, ppRenderPass);
}

Result DeviceSwapchainWrapImpl::AcquireNextImage(
    uint64_t         timeout,
    grfx::Semaphore* pSemaphore,
    grfx::Fence*     pFence,
    uint32_t*        pImageIndex)
{
    return mSwapchain->AcquireNextImage(timeout, pSemaphore, pFence, pImageIndex);
}

Result DeviceSwapchainWrapImpl::Present(
    uint32_t                      imageIndex,
    uint32_t                      waitSemaphoreCount,
    const grfx::Semaphore* const* ppWaitSemaphores)
{
    return mSwapchain->Present(imageIndex, waitSemaphoreCount, ppWaitSemaphores);
}

grfx::Device* DeviceSwapchainWrapImpl::GetDevice() const
{
    return mSwapchain->GetDevice();
}

} // namespace ppx
