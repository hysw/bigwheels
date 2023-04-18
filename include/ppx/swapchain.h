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

#ifndef ppx_swapchain_h
#define ppx_swapchain_h

#include "ppx/grfx/grfx_config.h"

#include <functional>
#include <memory>

namespace ppx {

//! @class Swapchain
//!
//! The Swapchain interface could be backed with grfx::Swapchain,
//! or any implementation that resembles a swapchain.
//!
class Swapchain
{
public:
    Swapchain() = default;
    virtual ~Swapchain() {}

    Swapchain(const Swapchain&)            = delete;
    Swapchain& operator=(const Swapchain&) = delete;

public:
    virtual uint32_t     GetImageCount() const  = 0;
    virtual grfx::Format GetColorFormat() const = 0;
    virtual grfx::Format GetDepthFormat() const = 0;

    virtual Result GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const = 0;
    virtual Result GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const = 0;

    // TODO: remove these function, they are ambigious at best
    // Use GetImage{Width,Height} or [GetRenderPass().]GetRenderArea().
    uint32_t GetWidth() const { return GetImageWidth(); }
    uint32_t GetHeight() const { return GetImageHeight(); }

    // Full image width/height, might be larger than the render area
    virtual uint32_t GetImageWidth() const  = 0;
    virtual uint32_t GetImageHeight() const = 0;

    virtual Result GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const = 0;

    virtual Result AcquireNextImage(
        uint64_t         timeout,    // Nanoseconds
        grfx::Semaphore* pSemaphore, // Wait sempahore
        grfx::Fence*     pFence,     // Wait fence
        uint32_t*        pImageIndex) = 0;

    virtual Result Present(
        uint32_t                      imageIndex,
        uint32_t                      waitSemaphoreCount,
        const grfx::Semaphore* const* ppWaitSemaphores) = 0;

    virtual grfx::Rect GetRenderArea() const; // Returns ScissorRect.
    grfx::Viewport     GetViewport(float minDepth = 0.0, float maxDepth = 1.0) const;
    float              GetAspect() const;

    grfx::ImagePtr GetColorImage(uint32_t imageIndex) const;
    grfx::ImagePtr GetDepthImage(uint32_t imageIndex) const;

    grfx::RenderPassPtr GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp = grfx::ATTACHMENT_LOAD_OP_CLEAR) const;

    virtual grfx::Device* GetDevice() const = 0;

public:
    static std::unique_ptr<Swapchain> PresentHook(Swapchain* swapchain, std::function<void(grfx::CommandBuffer*)> f);

protected:
    virtual Result OnUpdate() { return ppx::SUCCESS; }
};

class DeviceSwapchainWrap : public Swapchain
{
public:
    static std::unique_ptr<DeviceSwapchainWrap> Create(grfx::Swapchain* swapchain, bool absorbError = true);

    virtual Result ResizeSwapchain(uint32_t w, uint32_t h) = 0;
    virtual Result ReplaceSwapchain(grfx::Swapchain*)      = 0;
    virtual bool   NeedUpdate()                            = 0;
    virtual void   SetNeedUpdate()                         = 0;
};

} // namespace ppx

#endif // ppx_swapchain_h
