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
// Implement RenderPass for Swapchain

namespace {

class RenderPassImpl
{
public:
    Result GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const;
    Result UpdateRenderPass(Swapchain* swapchain);
    void   Cleanup();
    ~RenderPassImpl();

private:
    std::vector<grfx::RenderPassPtr> mClearRenderPasses;
    std::vector<grfx::RenderPassPtr> mLoadRenderPasses;

    grfx::Device* device = nullptr;
};

template <typename T>
class WithRenderPass : public T
{
public:
    using T::GetRenderPass;
    using T::T;
    Result GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const final
    {
        return mRenderPass.GetRenderPass(imageIndex, loadOp, ppRenderPass);
    }

protected:
    Result OnUpdate() override
    {
        Result ppxres = mRenderPass.UpdateRenderPass(this);
        if (Failed(ppxres)) {
            return ppxres;
        }
        return T::OnUpdate();
    }

private:
    RenderPassImpl mRenderPass;
};

RenderPassImpl::~RenderPassImpl()
{
    Cleanup();
}

Result RenderPassImpl::GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const
{
    if (!IsIndexInRange(imageIndex, mClearRenderPasses)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    if (loadOp == grfx::ATTACHMENT_LOAD_OP_CLEAR) {
        *ppRenderPass = mClearRenderPasses[imageIndex];
    }
    else {
        *ppRenderPass = mLoadRenderPasses[imageIndex];
    }
    return ppx::SUCCESS;
}

void RenderPassImpl::Cleanup()
{
    if (!device) {
        return;
    }
    for (auto rp : mClearRenderPasses) {
        device->DestroyRenderPass(rp);
    }
    mClearRenderPasses.clear();
    for (auto rp : mLoadRenderPasses) {
        device->DestroyRenderPass(rp);
    }
    mLoadRenderPasses.clear();
}

Result RenderPassImpl::UpdateRenderPass(Swapchain* swapchain)
{
    device = swapchain->GetDevice();
    Cleanup();

    bool hasDepthImage = swapchain->GetDepthFormat() != grfx::FORMAT_UNDEFINED;

    // Create render passes with grfx::ATTACHMENT_LOAD_OP_CLEAR for render target.
    for (uint32_t i = 0; i < swapchain->GetImageCount(); i++) {
        grfx::RenderPassCreateInfo3 rpCreateInfo = {};
        rpCreateInfo.width                       = swapchain->GetImageWidth();
        rpCreateInfo.height                      = swapchain->GetImageHeight();
        rpCreateInfo.renderTargetCount           = 1;
        rpCreateInfo.pRenderTargetImages[0]      = swapchain->GetColorImage(i);
        rpCreateInfo.pDepthStencilImage          = hasDepthImage ? swapchain->GetDepthImage(i) : nullptr;
        rpCreateInfo.renderTargetClearValues[0]  = {{0.0f, 0.0f, 0.0f, 0.0f}};
        rpCreateInfo.depthStencilClearValue      = {1.0f, 0xFF};
        rpCreateInfo.renderTargetLoadOps[0]      = grfx::ATTACHMENT_LOAD_OP_CLEAR;
        rpCreateInfo.depthLoadOp                 = grfx::ATTACHMENT_LOAD_OP_CLEAR;
        rpCreateInfo.ownership                   = grfx::OWNERSHIP_RESTRICTED;

        grfx::RenderPassPtr renderPass;

        Result ppxres = swapchain->GetDevice()->CreateRenderPass(&rpCreateInfo, &renderPass);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "RenderPassImpl::UpdateRenderPass(CLEAR) failed");
            return ppxres;
        }

        mClearRenderPasses.push_back(renderPass);
    }

    // Create render passes with grfx::ATTACHMENT_LOAD_OP_LOAD for render target.
    for (uint32_t i = 0; i < swapchain->GetImageCount(); i++) {
        grfx::RenderPassCreateInfo3 rpCreateInfo = {};
        rpCreateInfo.width                       = swapchain->GetImageWidth();
        rpCreateInfo.height                      = swapchain->GetImageHeight();
        rpCreateInfo.renderTargetCount           = 1;
        rpCreateInfo.pRenderTargetImages[0]      = swapchain->GetColorImage(i);
        rpCreateInfo.pDepthStencilImage          = hasDepthImage ? swapchain->GetDepthImage(i) : nullptr;
        rpCreateInfo.renderTargetClearValues[0]  = {{0.0f, 0.0f, 0.0f, 0.0f}};
        rpCreateInfo.depthStencilClearValue      = {1.0f, 0xFF};
        rpCreateInfo.renderTargetLoadOps[0]      = grfx::ATTACHMENT_LOAD_OP_LOAD;
        rpCreateInfo.depthLoadOp                 = grfx::ATTACHMENT_LOAD_OP_CLEAR;
        rpCreateInfo.ownership                   = grfx::OWNERSHIP_RESTRICTED;

        grfx::RenderPassPtr renderPass;

        Result ppxres = swapchain->GetDevice()->CreateRenderPass(&rpCreateInfo, &renderPass);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "RenderPassImpl::UpdateRenderPass(LOAD) failed");
            return ppxres;
        }

        mLoadRenderPasses.push_back(renderPass);
    }

    return ppx::SUCCESS;
}

} // namespace

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
    uint32_t     GetImageCount() const final;
    grfx::Format GetColorFormat() const final;
    grfx::Format GetDepthFormat() const final;

    uint32_t GetImageWidth() const final;
    uint32_t GetImageHeight() const final;

    Result GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const final;
    Result GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const final;

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

    DeviceSwapchainWrapImpl(grfx::Swapchain* swapchain, bool absorbError = true);

    Result ResizeSwapchain(uint32_t w, uint32_t h) final;
    Result ReplaceSwapchain(grfx::Swapchain* swapchain) final;

    bool NeedUpdate() final;
    void SetNeedUpdate() final;

private:
    grfx::Swapchain* mSwapchain   = nullptr;
    bool             mNeedUpdate  = false;
    bool             mAbsorbError = true;
};

std::unique_ptr<DeviceSwapchainWrap> DeviceSwapchainWrap::Create(grfx::Swapchain* swapchain, bool absorbError)
{
    std::unique_ptr<DeviceSwapchainWrap> swapchainWrap =
        std::unique_ptr<DeviceSwapchainWrap>(new WithRenderPass<DeviceSwapchainWrapImpl>(swapchain, absorbError));
    swapchainWrap->OnUpdate();
    return swapchainWrap;
}

DeviceSwapchainWrapImpl::DeviceSwapchainWrapImpl(grfx::Swapchain* swapchain, bool absorbError)
    : mSwapchain(swapchain), mAbsorbError(absorbError)
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

Result DeviceSwapchainWrapImpl::AcquireNextImage(
    uint64_t         timeout,
    grfx::Semaphore* pSemaphore,
    grfx::Fence*     pFence,
    uint32_t*        pImageIndex)
{
    Result ppxres = mSwapchain->AcquireNextImage(timeout, pSemaphore, pFence, pImageIndex);
    if (ppxres == ppx::ERROR_OUT_OF_DATE) {
        mNeedUpdate = true;

        if (mAbsorbError) {
            *pImageIndex = 0;

            grfx::SubmitInfo sInfo     = {};
            sInfo.ppCommandBuffers     = nullptr;
            sInfo.commandBufferCount   = 0;
            sInfo.pFence               = pFence;
            sInfo.ppSignalSemaphores   = &pSemaphore;
            sInfo.signalSemaphoreCount = 1;
            GetDevice()->GetGraphicsQueue()->Submit(&sInfo);
        }

        return mAbsorbError ? ppx::SUCCESS : ppxres;
    }
    if (ppxres == ppx::ERROR_SUBOPTIMAL) {
        mNeedUpdate = true;
        return mAbsorbError ? ppx::SUCCESS : ppxres;
    }
    return ppxres;
}

Result DeviceSwapchainWrapImpl::Present(
    uint32_t                      imageIndex,
    uint32_t                      waitSemaphoreCount,
    const grfx::Semaphore* const* ppWaitSemaphores)
{
    Result ppxres = mSwapchain->Present(imageIndex, waitSemaphoreCount, ppWaitSemaphores);
    if (ppxres == ppx::ERROR_OUT_OF_DATE) {
        mNeedUpdate = true;
        return mAbsorbError ? ppx::SUCCESS : ppxres;
    }
    if (ppxres == ppx::ERROR_SUBOPTIMAL) {
        mNeedUpdate = true;
        return mAbsorbError ? ppx::SUCCESS : ppxres;
    }
    return ppxres;
}

grfx::Device* DeviceSwapchainWrapImpl::GetDevice() const
{
    return mSwapchain->GetDevice();
}

Result DeviceSwapchainWrapImpl::ResizeSwapchain(uint32_t w, uint32_t h)
{
    Result ppxres = mSwapchain->Resize(w, h);
    if (ppxres == ppx::SUCCESS) {
        mNeedUpdate = false;
        return OnUpdate();
    }
    return ppxres;
}

Result DeviceSwapchainWrapImpl::ReplaceSwapchain(grfx::Swapchain* swapchain)
{
    mSwapchain  = swapchain;
    mNeedUpdate = false;
    return OnUpdate();
}

bool DeviceSwapchainWrapImpl::NeedUpdate()
{
    return mNeedUpdate;
}

void DeviceSwapchainWrapImpl::SetNeedUpdate()
{
    mNeedUpdate = true;
}

// -------------------------------------------------------------------------------------------------
// SwapchainWrap, deligate to another swapchain like object

class SwapchainWrap : public Swapchain
{
public:
    SwapchainWrap(Swapchain* impl)
        : mImpl(impl) {}

    uint32_t     GetImageCount() const override { return mImpl->GetImageCount(); }
    grfx::Format GetColorFormat() const override { return mImpl->GetColorFormat(); }
    grfx::Format GetDepthFormat() const override { return mImpl->GetDepthFormat(); }

    using Swapchain::GetColorImage;
    using Swapchain::GetDepthImage;

    Result GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const override { return mImpl->GetColorImage(imageIndex, ppImage); }
    Result GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const override { return mImpl->GetDepthImage(imageIndex, ppImage); }

    // Full image width/height, might be larger than the render area
    uint32_t GetImageWidth() const override { return mImpl->GetImageWidth(); }
    uint32_t GetImageHeight() const override { return mImpl->GetImageHeight(); }

    using Swapchain::GetRenderPass;
    Result GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const override
    {
        return mImpl->GetRenderPass(imageIndex, loadOp, ppRenderPass);
    }

    Result AcquireNextImage(
        uint64_t         timeout,    // Nanoseconds
        grfx::Semaphore* pSemaphore, // Wait sempahore
        grfx::Fence*     pFence,     // Wait fence
        uint32_t*        pImageIndex) override
    {
        return mImpl->AcquireNextImage(timeout, pSemaphore, pFence, pImageIndex);
    }

    Result Present(
        uint32_t                      imageIndex,
        uint32_t                      waitSemaphoreCount,
        const grfx::Semaphore* const* ppWaitSemaphores) override
    {
        return mImpl->Present(imageIndex, waitSemaphoreCount, ppWaitSemaphores);
    }

    grfx::Rect GetRenderArea() const override { return mImpl->GetRenderArea(); }

    grfx::Device* GetDevice() const final { return mImpl->GetDevice(); }

protected:
    Swapchain* mImpl;
};

// -------------------------------------------------------------------------------------------------
// PresentHook
namespace {

template <typename T>
class WithPostProcess : public T
{
public:
    using T::T;
    Result Present(
        uint32_t                      imageIndex,
        uint32_t                      waitSemaphoreCount,
        const grfx::Semaphore* const* ppWaitSemaphores) final
    {
        grfx::CommandBufferPtr commandBuffer = mCommandBuffers[imageIndex];

        commandBuffer->Begin();
        this->RecordCommands(imageIndex, commandBuffer);
        commandBuffer->End();

        grfx::Semaphore* pSignalSemaphore = mSemaphores[imageIndex].Get();

        grfx::SubmitInfo sInfo     = {};
        sInfo.ppCommandBuffers     = &commandBuffer;
        sInfo.commandBufferCount   = 1;
        sInfo.ppWaitSemaphores     = ppWaitSemaphores;
        sInfo.waitSemaphoreCount   = waitSemaphoreCount;
        sInfo.ppSignalSemaphores   = &pSignalSemaphore;
        sInfo.signalSemaphoreCount = 1;
        mQueue->Submit(&sInfo);

        return T::Present(imageIndex, 1, &pSignalSemaphore);
    }

    void InitPostProcess(grfx::Queue* queue)
    {
        mQueue = queue;

        for (uint32_t i = 0; i < this->GetImageCount(); ++i) {
            grfx::CommandBufferPtr commandBuffer = nullptr;
            mQueue->CreateCommandBuffer(&commandBuffer, 0, 0);
            mCommandBuffers.push_back(commandBuffer);
        }
        grfx::Device* device = mQueue->GetDevice();
        for (uint32_t i = 0; i < this->GetImageCount(); i++) {
            grfx::SemaphoreCreateInfo info = {};
            grfx::SemaphorePtr        pSemaphore;
            PPX_CHECKED_CALL(device->CreateSemaphore(&info, &pSemaphore));
            mSemaphores.push_back(pSemaphore);
        }
    }

private:
    grfx::Queue* mQueue = nullptr;

    std::vector<grfx::CommandBufferPtr> mCommandBuffers;
    std::vector<grfx::SemaphorePtr>     mSemaphores;
};

} // namespace

// -------------------------------------------------------------------------------------------------
// PresentHook

class SwapchainPresentHook : public SwapchainWrap
{
public:
    SwapchainPresentHook(Swapchain* impl, std::function<void(grfx::CommandBuffer*)> f)
        : SwapchainWrap(impl), mOnPresent(f) {}

    void RecordCommands(uint32_t imageIndex, grfx::CommandBuffer* commandBuffer)
    {
        commandBuffer->TransitionImageLayout(GetColorImage(imageIndex), PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_RENDER_TARGET);
        {
            grfx::RenderPassPtr renderPass = GetRenderPass(imageIndex, grfx::ATTACHMENT_LOAD_OP_LOAD);

            grfx::RenderPassBeginInfo beginInfo = {};
            beginInfo.pRenderPass               = renderPass;
            beginInfo.renderArea                = renderPass->GetRenderArea();
            beginInfo.RTVClearCount             = 1;
            beginInfo.RTVClearValues[0]         = {{0.5, 0.5, 0.5, 0}};
            beginInfo.DSVClearValue             = {1.0f, 0xFF};

            commandBuffer->BeginRenderPass(&beginInfo);
            if (mOnPresent) {
                commandBuffer->SetViewports(GetViewport());
                commandBuffer->SetScissors(GetRenderArea());
                mOnPresent(commandBuffer);
            }
            commandBuffer->EndRenderPass();
        }
        commandBuffer->TransitionImageLayout(GetColorImage(imageIndex), PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_RENDER_TARGET, grfx::RESOURCE_STATE_PRESENT);
    }

private:
    std::function<void(grfx::CommandBuffer*)> mOnPresent;
};

std::unique_ptr<Swapchain> Swapchain::PresentHook(Swapchain* swapchain, std::function<void(grfx::CommandBuffer*)> f)
{
    auto res = std::make_unique<WithPostProcess<SwapchainPresentHook>>(swapchain, f);
    res->InitPostProcess(swapchain->GetDevice()->GetGraphicsQueue());
    return res;
}

// -------------------------------------------------------------------------------------------------
// VirtualSwapchain

grfx::Rect VirtualSwapchain::GetRenderArea() const
{
    if (mRenderArea.width == 0 || mRenderArea.height == 0) {
        return {0, 0, GetImageWidth(), GetImageHeight()};
    }
    return mRenderArea;
}

void VirtualSwapchain::UpdateRenderArea(grfx::Rect renderArea)
{
    grfx::Rect current = GetRenderArea();
    if (renderArea.x == current.x &&
        renderArea.y == current.y &&
        renderArea.width == current.width &&
        renderArea.height == current.height) {
        return;
    }
    if (renderArea.x < 0 || renderArea.y < 0 ||
        renderArea.width == 0 || renderArea.height == 0 ||
        renderArea.x + renderArea.width > GetImageWidth() ||
        renderArea.y + renderArea.height > GetImageHeight()) {
        return;
    }
    mRenderArea = renderArea;
}

class VirtualSwapchainImpl : public VirtualSwapchain
{
public:
    uint32_t     GetImageCount() const final { return mCreateInfo.imageCount; }
    grfx::Format GetColorFormat() const final { return mCreateInfo.colorFormat; }
    grfx::Format GetDepthFormat() const final { return mCreateInfo.depthFormat; }

    uint32_t GetImageWidth() const final { return mCreateInfo.width; }
    uint32_t GetImageHeight() const final { return mCreateInfo.height; }

    Result GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const final;
    Result GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const final;

    grfx::Device* GetDevice() const final;

    std::vector<grfx::ImagePtr> mDepthImages;
    std::vector<grfx::ImagePtr> mColorImages;

    Result InitVirtualSwapchain();

protected:
    VirtualSwapchainImpl(const CreateInfo& createInfo);

    grfx::Queue* GetQueue() const { return mCreateInfo.pQueue; }

private:
    CreateInfo mCreateInfo = {};
};

VirtualSwapchainImpl::VirtualSwapchainImpl(const CreateInfo& createInfo)
    : mCreateInfo(createInfo)
{
}

Result VirtualSwapchainImpl::InitVirtualSwapchain()
{
    // Create color images if needed. This is only needed if we're creating
    // a headless swapchain.
    if (mColorImages.empty()) {
        for (uint32_t i = 0; i < mCreateInfo.imageCount; ++i) {
            grfx::ImageCreateInfo rtCreateInfo = grfx::ImageCreateInfo::RenderTarget2D(mCreateInfo.width, mCreateInfo.height, mCreateInfo.colorFormat);
            rtCreateInfo.ownership             = grfx::OWNERSHIP_RESTRICTED;
            rtCreateInfo.RTVClearValue         = {0.0f, 0.0f, 0.0f, 0.0f};
            rtCreateInfo.initialState          = grfx::RESOURCE_STATE_PRESENT;
            rtCreateInfo.usageFlags =
                grfx::IMAGE_USAGE_COLOR_ATTACHMENT |
                grfx::IMAGE_USAGE_TRANSFER_SRC |
                grfx::IMAGE_USAGE_TRANSFER_DST |
                grfx::IMAGE_USAGE_SAMPLED;

            grfx::ImagePtr renderTargetOLD;

            Result ppxres = GetQueue()->GetDevice()->CreateImage(&rtCreateInfo, &renderTargetOLD);
            if (Failed(ppxres)) {
                return ppxres;
            }

            mColorImages.push_back(renderTargetOLD);
        }
    }

    // Create depth images if needed. This is usually needed for both normal swapchains
    // and headless swapchains, but not needed for XR swapchains which create their own
    // depth images.
    if (mCreateInfo.depthFormat != grfx::FORMAT_UNDEFINED && mDepthImages.empty()) {
        for (uint32_t i = 0; i < mCreateInfo.imageCount; ++i) {
            grfx::ImageCreateInfo dpCreateInfo = grfx::ImageCreateInfo::DepthStencilTarget(mCreateInfo.width, mCreateInfo.height, mCreateInfo.depthFormat);
            dpCreateInfo.ownership             = grfx::OWNERSHIP_RESTRICTED;
            dpCreateInfo.DSVClearValue         = {1.0f, 0xFF};

            grfx::ImagePtr depthStencilTarget;

            Result ppxres = GetQueue()->GetDevice()->CreateImage(&dpCreateInfo, &depthStencilTarget);
            if (Failed(ppxres)) {
                return ppxres;
            }

            mDepthImages.push_back(depthStencilTarget);
        }
    }

    return OnUpdate();
}

Result VirtualSwapchainImpl::GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const
{
    if (!IsIndexInRange(imageIndex, mColorImages)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    *ppImage = mColorImages[imageIndex];
    return ppx::SUCCESS;
}

Result VirtualSwapchainImpl::GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const
{
    if (!IsIndexInRange(imageIndex, mDepthImages)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    *ppImage = mDepthImages[imageIndex];
    return ppx::SUCCESS;
}

grfx::Device* VirtualSwapchainImpl::GetDevice() const
{
    return mCreateInfo.pQueue->GetDevice();
}

// -------------------------------------------------------------------------------------------------
// IndirectSwapchain

class IndirectSwapchain : public VirtualSwapchainImpl
{
public:
    Result AcquireNextImage(
        uint64_t         timeout,    // Nanoseconds
        grfx::Semaphore* pSemaphore, // Wait sempahore
        grfx::Fence*     pFence,     // Wait fence
        uint32_t*        pImageIndex) final;

    Result Present(
        uint32_t                      imageIndex,
        uint32_t                      waitSemaphoreCount,
        const grfx::Semaphore* const* ppWaitSemaphores) override;

public:
    IndirectSwapchain(Swapchain* next, const CreateInfo& createInfo);

protected:
    void RecordCommands(uint32_t imageIndex, grfx::CommandBuffer* commandBuffer);

private:
    Swapchain* mNext = nullptr;
};

IndirectSwapchain::IndirectSwapchain(Swapchain* next, const CreateInfo& createInfo)
    : VirtualSwapchainImpl(createInfo), mNext(next)
{
}

Result IndirectSwapchain::AcquireNextImage(
    uint64_t         timeout,    // Nanoseconds
    grfx::Semaphore* pSemaphore, // Wait sempahore
    grfx::Fence*     pFence,     // Wait fence
    uint32_t*        pImageIndex)
{
    Result ppxres = mNext->AcquireNextImage(timeout, pSemaphore, pFence, pImageIndex);
    if (ppxres == ppx::ERROR_SUBOPTIMAL) {
        return ppx::SUCCESS;
    }
    if (ppxres == ppx::ERROR_OUT_OF_DATE) {
        // In case swapchain resize, just release the image directly.
        *pImageIndex = 0;

        grfx::SubmitInfo sInfo     = {};
        sInfo.ppCommandBuffers     = nullptr;
        sInfo.commandBufferCount   = 0;
        sInfo.pFence               = pFence;
        sInfo.ppSignalSemaphores   = &pSemaphore;
        sInfo.signalSemaphoreCount = 1;
        GetQueue()->Submit(&sInfo);

        return ppx::SUCCESS;
    }
    return ppxres;
}

Result IndirectSwapchain::Present(
    uint32_t                      imageIndex,
    uint32_t                      waitSemaphoreCount,
    const grfx::Semaphore* const* ppWaitSemaphores)
{
    Result ppxres = mNext->Present(
        imageIndex,
        waitSemaphoreCount,
        ppWaitSemaphores);
    if (ppxres == ppx::ERROR_SUBOPTIMAL) {
        return ppx::SUCCESS;
    }
    if (ppxres == ppx::ERROR_OUT_OF_DATE) {
        return ppx::SUCCESS;
    }
    return ppxres;
}

void IndirectSwapchain::RecordCommands(uint32_t imageIndex, grfx::CommandBuffer* commandBuffer)
{
    grfx::ImageToImageCopyInfo imcopy = {};
    {
        grfx::Rect src           = GetRenderArea();
        grfx::Rect dst           = mNext->GetRenderArea();
        imcopy.srcImage.offset.x = src.x;
        imcopy.srcImage.offset.y = src.y;
        imcopy.dstImage.offset.x = dst.x;
        imcopy.dstImage.offset.y = dst.y;
        if (src.width > dst.width) {
            imcopy.srcImage.offset.x += (src.width - dst.width) / 2;
        }
        else {
            imcopy.dstImage.offset.x += (dst.width - src.width) / 2;
        }
        if (src.height > dst.height) {
            imcopy.srcImage.offset.y += (src.height - dst.height) / 2;
        }
        else {
            imcopy.dstImage.offset.y += (dst.height - src.height) / 2;
        }
        imcopy.extent.x = std::min(src.width, dst.width);
        imcopy.extent.y = std::min(src.height, dst.height);
    }

    grfx::Image* srcImage = Swapchain::GetColorImage(imageIndex);
    grfx::Image* dstImage = mNext->GetColorImage(imageIndex);

    commandBuffer->TransitionImageLayout(dstImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_RENDER_TARGET);
    {
        // Clear screen.
        grfx::RenderPassPtr renderPass = mNext->GetRenderPass(imageIndex, grfx::ATTACHMENT_LOAD_OP_CLEAR);

        grfx::RenderPassBeginInfo beginInfo = {};
        beginInfo.pRenderPass               = renderPass;
        beginInfo.renderArea                = renderPass->GetRenderArea();
        beginInfo.RTVClearCount             = 1;
        beginInfo.RTVClearValues[0]         = {{0.5, 0.5, 0.5, 0}};
        beginInfo.DSVClearValue             = {1.0f, 0xFF};

        commandBuffer->BeginRenderPass(&beginInfo);
        commandBuffer->EndRenderPass();
    }
    commandBuffer->TransitionImageLayout(dstImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_RENDER_TARGET, grfx::RESOURCE_STATE_COPY_DST);
    {
        // Copy rendered image.
        // Note(tianc): this should be a image blit instead of copy.
        commandBuffer->TransitionImageLayout(srcImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_COPY_SRC);
        commandBuffer->CopyImageToImage(&imcopy, srcImage, dstImage);
        commandBuffer->TransitionImageLayout(srcImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_COPY_SRC, grfx::RESOURCE_STATE_PRESENT);
    }
    commandBuffer->TransitionImageLayout(dstImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_COPY_DST, grfx::RESOURCE_STATE_PRESENT);
}

std::unique_ptr<VirtualSwapchain> VirtualSwapchain::Create(Swapchain* realSwapchain, const CreateInfo& createInfo)
{
    auto res = std::make_unique<WithRenderPass<WithPostProcess<IndirectSwapchain>>>(realSwapchain, createInfo);
    res->InitVirtualSwapchain();
    res->InitPostProcess(createInfo.pQueue);
    static_cast<VirtualSwapchain*>(res.get())->OnUpdate();
    return res;
}

} // namespace ppx
