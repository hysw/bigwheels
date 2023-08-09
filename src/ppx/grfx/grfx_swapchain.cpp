// Copyright 2022 Google LLC
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

#include "ppx/grfx/grfx_swapchain.h"
#include "ppx/grfx/grfx_device.h"
#include "ppx/grfx/grfx_render_pass.h"
#include "ppx/grfx/grfx_instance.h"

namespace ppx {
namespace grfx {

Result Swapchain::Create(const grfx::SwapchainCreateInfo* pCreateInfo)
{
    PPX_ASSERT_NULL_ARG(pCreateInfo->pQueue);
    if (IsNull(pCreateInfo->pQueue)) {
        return ppx::ERROR_UNEXPECTED_NULL_ARGUMENT;
    }

    Result ppxres = grfx::DeviceObject<grfx::SwapchainCreateInfo>::Create(pCreateInfo);
    if (Failed(ppxres)) {
        return ppxres;
    }

    // Update the stored create info's image count since the actual
    // number of images might be different (hopefully more) than
    // what was originally requested.
    if (!IsHeadless()) {
        mCreateInfo.imageCount = CountU32(mDeviceTarget.colorImages);
    }
    if (mCreateInfo.imageCount != pCreateInfo->imageCount) {
        PPX_LOG_INFO("Swapchain actual image count is different from what was requested\n"
                     << "   actual    : " << mCreateInfo.imageCount << "\n"
                     << "   requested : " << pCreateInfo->imageCount);
    }

    //
    // NOTE: mCreateInfo will be used from this point on.
    //

    // Create RenderTarget and Depth/Stencil target for headless.
    if (IsHeadless()) {
        SetIndirectRenderSize(mCreateInfo.width, mCreateInfo.height);
    }

    // Create command buffers for headless / post processing
    for (uint32_t i = 0; i < mCreateInfo.imageCount; ++i) {
        grfx::CommandBufferPtr commandBuffer = nullptr;
        mCreateInfo.pQueue->CreateCommandBuffer(&commandBuffer, 0, 0);
        mCommandBuffers.push_back(commandBuffer);
        mIsRecording.push_back(false);
    }

    // Create semaphore for post processing sync.
    for (uint32_t i = 0; i < mCreateInfo.imageCount; ++i) {
        grfx::SemaphorePtr        semaphore;
        grfx::SemaphoreCreateInfo semaphoreCreateInfo = {};
        GetDevice()->CreateSemaphore(&semaphoreCreateInfo, &semaphore);
        mPostProcessSemaphores.push_back(semaphore);
    }

    if (IsHeadless()) {
        // Set mCurrentImageIndex to (imageCount - 1) so that the first
        // AcquireNextImage call acquires the first image at index 0.
        mCurrentImageIndex = mCreateInfo.imageCount - 1;
    }

    PPX_LOG_INFO("Swapchain created");
    PPX_LOG_INFO("   "
                 << "resolution  : " << mCreateInfo.width << "x" << mCreateInfo.height);
    PPX_LOG_INFO("   "
                 << "image count : " << mCreateInfo.imageCount);

    return ppx::SUCCESS;
}

void Swapchain::Destroy()
{
    DestroyIndirectTarget();
    DestroyDeviceTarget();

#if defined(PPX_BUILD_XR)
    if (mXrColorSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(mXrColorSwapchain);
    }
    if (mXrDepthSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(mXrDepthSwapchain);
    }
#endif

    for (auto& elem : mPostProcessSemaphores) {
        GetDevice()->DestroySemaphore(elem);
    }
    mPostProcessSemaphores.clear();

    for (auto& elem : mCommandBuffers) {
        if (elem) {
            mCreateInfo.pQueue->DestroyCommandBuffer(elem);
        }
    }
    mCommandBuffers.clear();
    mIsRecording.clear();

    grfx::DeviceObject<grfx::SwapchainCreateInfo>::Destroy();
}

Result Swapchain::Target::CreateColorImages(grfx::Device& device, grfx::Format format, uint32_t imageCount)
{
    for (uint32_t i = 0; i < imageCount; ++i) {
        grfx::ImageCreateInfo rtCreateInfo = ImageCreateInfo::RenderTarget2D(width, height, format);
        rtCreateInfo.ownership             = grfx::OWNERSHIP_RESTRICTED;
        rtCreateInfo.RTVClearValue         = {0.0f, 0.0f, 0.0f, 0.0f};
        rtCreateInfo.initialState          = grfx::RESOURCE_STATE_PRESENT;
        rtCreateInfo.usageFlags =
            grfx::IMAGE_USAGE_COLOR_ATTACHMENT |
            grfx::IMAGE_USAGE_TRANSFER_SRC |
            grfx::IMAGE_USAGE_TRANSFER_DST |
            grfx::IMAGE_USAGE_SAMPLED;

        grfx::ImagePtr renderTarget;
        auto           ppxres = device.CreateImage(&rtCreateInfo, &renderTarget);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "image create failed");
            return ppxres;
        }

        colorImages.push_back(renderTarget);
    }
    return ppx::SUCCESS;
}

Result Swapchain::Target::WrapColorImages(grfx::Device& device, grfx::Format format, const std::vector<void*>& handles)
{
    for (uint32_t i = 0; i < handles.size(); ++i) {
        grfx::ImageCreateInfo imageCreateInfo           = {};
        imageCreateInfo.type                            = grfx::IMAGE_TYPE_2D;
        imageCreateInfo.width                           = width;
        imageCreateInfo.height                          = height;
        imageCreateInfo.depth                           = 1;
        imageCreateInfo.format                          = format;
        imageCreateInfo.sampleCount                     = grfx::SAMPLE_COUNT_1;
        imageCreateInfo.mipLevelCount                   = 1;
        imageCreateInfo.arrayLayerCount                 = 1;
        imageCreateInfo.usageFlags.bits.transferSrc     = true;
        imageCreateInfo.usageFlags.bits.transferDst     = true;
        imageCreateInfo.usageFlags.bits.sampled         = true;
        imageCreateInfo.usageFlags.bits.storage         = true;
        imageCreateInfo.usageFlags.bits.colorAttachment = true;
        imageCreateInfo.pApiObject                      = handles[i];

        grfx::ImagePtr image;
        Result         ppxres = device.CreateImage(&imageCreateInfo, &image);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "image create failed");
            return ppxres;
        }

        colorImages.push_back(image);
    }
    return ppx::SUCCESS;
}

void Swapchain::Target::DestroyColorImages(grfx::Device& device)
{
    for (auto& elem : colorImages) {
        if (elem) {
            device.DestroyImage(elem);
        }
    }
    colorImages.clear();
}

Result Swapchain::Target::CreateDepthImages(grfx::Device& device, grfx::Format format, uint32_t imageCount)
{
    for (uint32_t i = 0; i < imageCount; ++i) {
        grfx::ImageCreateInfo dpCreateInfo = ImageCreateInfo::DepthStencilTarget(width, height, format);
        dpCreateInfo.ownership             = grfx::OWNERSHIP_RESTRICTED;
        dpCreateInfo.DSVClearValue         = {1.0f, 0xFF};

        grfx::ImagePtr depthStencilTarget;
        auto           ppxres = device.CreateImage(&dpCreateInfo, &depthStencilTarget);
        if (Failed(ppxres)) {
            return ppxres;
        }

        depthImages.push_back(depthStencilTarget);
    }
    return ppx::SUCCESS;
}

Result Swapchain::Target::WrapDepthImages(grfx::Device& device, grfx::Format format, const std::vector<void*>& handles)
{
    for (size_t i = 0; i < depthImages.size(); ++i) {
        grfx::ImageCreateInfo imageCreateInfo = grfx::ImageCreateInfo::DepthStencilTarget(width, height, format, grfx::SAMPLE_COUNT_1);
        imageCreateInfo.pApiObject            = (void*)(depthImages[i]);

        grfx::ImagePtr image;
        Result         ppxres = device.CreateImage(&imageCreateInfo, &image);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "image create failed");
            return ppxres;
        }

        depthImages.push_back(image);
    }
    return ppx::SUCCESS;
}

void Swapchain::Target::DestroyDepthImages(grfx::Device& device)
{
    for (auto& elem : depthImages) {
        if (elem) {
            device.DestroyImage(elem);
        }
    }
    depthImages.clear();
}

Result Swapchain::Target::CreateRenderPasses(grfx::Device& device)
{
    uint32_t imageCount = CountU32(colorImages);
    PPX_ASSERT_MSG((imageCount > 0), "No color images found for swapchain renderpasses");

    // Create render passes with grfx::ATTACHMENT_LOAD_OP_CLEAR for render target.
    for (size_t i = 0; i < imageCount; ++i) {
        grfx::RenderPassCreateInfo3 rpCreateInfo = {};
        rpCreateInfo.width                       = colorImages[i]->GetWidth();
        rpCreateInfo.height                      = colorImages[i]->GetHeight();
        rpCreateInfo.renderTargetCount           = 1;
        rpCreateInfo.pRenderTargetImages[0]      = colorImages[i];
        rpCreateInfo.pDepthStencilImage          = depthImages.empty() ? nullptr : depthImages[i];
        rpCreateInfo.renderTargetClearValues[0]  = {{0.0f, 0.0f, 0.0f, 0.0f}};
        rpCreateInfo.depthStencilClearValue      = {1.0f, 0xFF};
        rpCreateInfo.renderTargetLoadOps[0]      = grfx::ATTACHMENT_LOAD_OP_CLEAR;
        rpCreateInfo.depthLoadOp                 = grfx::ATTACHMENT_LOAD_OP_CLEAR;
        rpCreateInfo.ownership                   = grfx::OWNERSHIP_RESTRICTED;

        grfx::RenderPassPtr renderPass;
        auto                ppxres = device.CreateRenderPass(&rpCreateInfo, &renderPass);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "grfx::Swapchain::CreateRenderPass(CLEAR) failed");
            return ppxres;
        }

        clearRenderPasses.push_back(renderPass);
    }

    // Create render passes with grfx::ATTACHMENT_LOAD_OP_LOAD for render target.
    for (size_t i = 0; i < imageCount; ++i) {
        grfx::RenderPassCreateInfo3 rpCreateInfo = {};
        rpCreateInfo.width                       = colorImages[i]->GetWidth();
        rpCreateInfo.height                      = colorImages[i]->GetHeight();
        rpCreateInfo.renderTargetCount           = 1;
        rpCreateInfo.pRenderTargetImages[0]      = colorImages[i];
        rpCreateInfo.pDepthStencilImage          = depthImages.empty() ? nullptr : depthImages[i];
        rpCreateInfo.renderTargetClearValues[0]  = {{0.0f, 0.0f, 0.0f, 0.0f}};
        rpCreateInfo.depthStencilClearValue      = {1.0f, 0xFF};
        rpCreateInfo.renderTargetLoadOps[0]      = grfx::ATTACHMENT_LOAD_OP_LOAD;
        rpCreateInfo.depthLoadOp                 = grfx::ATTACHMENT_LOAD_OP_CLEAR;
        rpCreateInfo.ownership                   = grfx::OWNERSHIP_RESTRICTED;

        grfx::RenderPassPtr renderPass;
        auto                ppxres = device.CreateRenderPass(&rpCreateInfo, &renderPass);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "grfx::Swapchain::CreateRenderPass(LOAD) failed");
            return ppxres;
        }

        loadRenderPasses.push_back(renderPass);
    }

    return ppx::SUCCESS;
}

void Swapchain::Target::DestroyRenderPasses(grfx::Device& device)
{
    for (auto& elem : clearRenderPasses) {
        if (elem) {
            device.DestroyRenderPass(elem);
        }
    }
    clearRenderPasses.clear();

    for (auto& elem : loadRenderPasses) {
        if (elem) {
            device.DestroyRenderPass(elem);
        }
    }
    loadRenderPasses.clear();
}

Result Swapchain::CreateTargetImpl(Target& target, uint32_t width, uint32_t height, const std::vector<void*>* pColorHandles, const std::vector<void*>* pDepthHandles)
{
    target.width  = width;
    target.height = height;

    if (pColorHandles && !pColorHandles->empty()) {
        ppx::Result ppxres = target.WrapColorImages(*GetDevice(), mCreateInfo.colorFormat, *pColorHandles);
        if (ppxres != ppx::SUCCESS) {
            return ppxres;
        }
    }
    else {
        ppx::Result ppxres = target.CreateColorImages(*GetDevice(), mCreateInfo.colorFormat, mCreateInfo.imageCount);
        if (ppxres != ppx::SUCCESS) {
            return ppxres;
        }
    }

    if (pDepthHandles && !pDepthHandles->empty()) {
        ppx::Result ppxres = target.WrapDepthImages(*GetDevice(), mCreateInfo.depthFormat, *pDepthHandles);
        if (ppxres != ppx::SUCCESS) {
            return ppxres;
        }
    }
    else if (mCreateInfo.depthFormat != grfx::FORMAT_UNDEFINED) {
        ppx::Result ppxres = target.CreateDepthImages(*GetDevice(), mCreateInfo.depthFormat, mCreateInfo.imageCount);
        if (ppxres != ppx::SUCCESS) {
            return ppxres;
        }
    }

    {
        ppx::Result ppxres = target.CreateRenderPasses(*GetDevice());
        if (ppxres != ppx::SUCCESS) {
            return ppxres;
        }
    }
    return ppx::SUCCESS;
}

Result Swapchain::CreateTarget(Target& target, uint32_t width, uint32_t height, const std::vector<void*>* pColorHandles, const std::vector<void*>* pDepthHandles)
{
    Result ppxres = CreateTargetImpl(target, width, height, pColorHandles, pDepthHandles);
    if (ppxres != ppx::SUCCESS) {
        DestroyTarget(target);
    }
    return ppxres;
}

void Swapchain::DestroyTarget(Target& target)
{
    target.width  = 0;
    target.height = 0;
    target.DestroyRenderPasses(*GetDevice());
    target.DestroyDepthImages(*GetDevice());
    target.DestroyColorImages(*GetDevice());
}

Result Swapchain::CreateIndirectTarget(uint32_t width, uint32_t height)
{
    return CreateTarget(mIndirectTarget, mCreateInfo.width, mCreateInfo.height, nullptr, nullptr);
}

void Swapchain::DestroyIndirectTarget()
{
    return DestroyTarget(mIndirectTarget);
}

Result Swapchain::CreateDeviceTarget(const std::vector<void*>* pColorHandles, const std::vector<void*>* pDepthHandles)
{
    return CreateTarget(mDeviceTarget, mCreateInfo.width, mCreateInfo.height, pColorHandles, pDepthHandles);
}

void Swapchain::DestroyDeviceTarget()
{
    return DestroyTarget(mDeviceTarget);
}

bool Swapchain::IsHeadless() const
{
#if defined(PPX_BUILD_XR)
    return mCreateInfo.pXrComponent == nullptr && mCreateInfo.pSurface == nullptr;
#else
    return mCreateInfo.pSurface == nullptr;
#endif
}

Result Swapchain::GetColorImage(uint32_t imageIndex, grfx::Image** ppImage) const
{
    if (!IsIndexInRange(imageIndex, GetTarget().colorImages)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    *ppImage = GetTarget().colorImages[imageIndex];
    return ppx::SUCCESS;
}

Result Swapchain::GetDepthImage(uint32_t imageIndex, grfx::Image** ppImage) const
{
    if (!IsIndexInRange(imageIndex, GetTarget().depthImages)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    *ppImage = GetTarget().depthImages[imageIndex];
    return ppx::SUCCESS;
}

Result Swapchain::GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp, grfx::RenderPass** ppRenderPass) const
{
    if (!IsIndexInRange(imageIndex, GetTarget().clearRenderPasses)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    if (loadOp == grfx::ATTACHMENT_LOAD_OP_CLEAR) {
        *ppRenderPass = GetTarget().clearRenderPasses[imageIndex];
    }
    else {
        *ppRenderPass = GetTarget().loadRenderPasses[imageIndex];
    }
    return ppx::SUCCESS;
}

Result Swapchain::GetUIRenderPass(uint32_t imageIndex, grfx::RenderPass** ppRenderPass) const
{
    if (!IsIndexInRange(imageIndex, mDeviceTarget.loadRenderPasses)) {
        return ppx::ERROR_OUT_OF_RANGE;
    }
    *ppRenderPass = mDeviceTarget.loadRenderPasses[imageIndex];
    return ppx::SUCCESS;
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

grfx::RenderPassPtr Swapchain::GetRenderPass(uint32_t imageIndex, grfx::AttachmentLoadOp loadOp) const
{
    grfx::RenderPassPtr object;
    GetRenderPass(imageIndex, loadOp, &object);
    return object;
}

grfx::RenderPassPtr Swapchain::GetUIRenderPass(uint32_t imageIndex) const
{
    grfx::RenderPassPtr object;
    GetUIRenderPass(imageIndex, &object);
    return object;
}

Result Swapchain::AcquireNextImage(
    uint64_t         timeout,    // Nanoseconds
    grfx::Semaphore* pSemaphore, // Wait sempahore
    grfx::Fence*     pFence,     // Wait fence
    uint32_t*        pImageIndex)
{
#if defined(PPX_BUILD_XR)
    if (mCreateInfo.pXrComponent != nullptr) {
        PPX_ASSERT_MSG(mXrColorSwapchain != XR_NULL_HANDLE, "Invalid color xrSwapchain handle!");
        PPX_ASSERT_MSG(pSemaphore == nullptr, "Should not use semaphore when XR is enabled!");
        PPX_ASSERT_MSG(pFence == nullptr, "Should not use fence when XR is enabled!");
        XrSwapchainImageAcquireInfo acquire_info = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        CHECK_XR_CALL(xrAcquireSwapchainImage(mXrColorSwapchain, &acquire_info, pImageIndex));

        XrSwapchainImageWaitInfo wait_info = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait_info.timeout                  = XR_INFINITE_DURATION;
        CHECK_XR_CALL(xrWaitSwapchainImage(mXrColorSwapchain, &wait_info));

        if (mXrDepthSwapchain != XR_NULL_HANDLE) {
            uint32_t                    colorImageIndex = *pImageIndex;
            XrSwapchainImageAcquireInfo acquire_info    = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            CHECK_XR_CALL(xrAcquireSwapchainImage(mXrDepthSwapchain, &acquire_info, pImageIndex));

            XrSwapchainImageWaitInfo wait_info = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait_info.timeout                  = XR_INFINITE_DURATION;
            CHECK_XR_CALL(xrWaitSwapchainImage(mXrDepthSwapchain, &wait_info));

            PPX_ASSERT_MSG(colorImageIndex == *pImageIndex, "Color and depth swapchain image indices are different");
        }
        return ppx::SUCCESS;
    }
#endif

    if (IsHeadless()) {
        return AcquireNextImageHeadless(timeout, pSemaphore, pFence, pImageIndex);
    }

    return AcquireNextImageInternal(timeout, pSemaphore, pFence, pImageIndex);
}

Result Swapchain::Present(
    uint32_t                      imageIndex,
    uint32_t                      waitSemaphoreCount,
    const grfx::Semaphore* const* ppWaitSemaphores)
{
    if (IsHeadless()) {
        return PresentHeadless(imageIndex, waitSemaphoreCount, ppWaitSemaphores);
    }

    if (IsIndirect()) {
        RecordPreamble(imageIndex);
    }
    uint32_t                      nextWaitSemaphoreCount = waitSemaphoreCount;
    const grfx::Semaphore* const* ppNextWaitSemaphores   = ppWaitSemaphores;
    if (mIsRecording[imageIndex]) {
        grfx::CommandBufferPtr commandBuffer = mCommandBuffers[mCurrentImageIndex];

        commandBuffer->End();
        mIsRecording[imageIndex] = false;

        grfx::SubmitInfo sInfo     = {};
        sInfo.ppCommandBuffers     = &commandBuffer;
        sInfo.commandBufferCount   = 1;
        sInfo.ppWaitSemaphores     = ppWaitSemaphores;
        sInfo.waitSemaphoreCount   = waitSemaphoreCount;
        sInfo.ppSignalSemaphores   = &mPostProcessSemaphores[imageIndex];
        sInfo.signalSemaphoreCount = 1;
        ppNextWaitSemaphores       = &mPostProcessSemaphores[imageIndex];
        nextWaitSemaphoreCount     = 1;
        mCreateInfo.pQueue->Submit(&sInfo);
    }

    return PresentInternal(imageIndex, nextWaitSemaphoreCount, ppNextWaitSemaphores);
}

Result Swapchain::AcquireNextImageHeadless(uint64_t timeout, grfx::Semaphore* pSemaphore, grfx::Fence* pFence, uint32_t* pImageIndex)
{
    *pImageIndex       = (mCurrentImageIndex + 1u) % CountU32(mIndirectTarget.colorImages);
    mCurrentImageIndex = *pImageIndex;

    grfx::CommandBufferPtr commandBuffer = mCommandBuffers[mCurrentImageIndex];

    commandBuffer->Begin();
    commandBuffer->End();

    grfx::SubmitInfo sInfo     = {};
    sInfo.ppCommandBuffers     = &commandBuffer;
    sInfo.commandBufferCount   = 1;
    sInfo.pFence               = pFence;
    sInfo.ppSignalSemaphores   = &pSemaphore;
    sInfo.signalSemaphoreCount = 1;
    mCreateInfo.pQueue->Submit(&sInfo);

    return ppx::SUCCESS;
}

Result Swapchain::PresentHeadless(uint32_t imageIndex, uint32_t waitSemaphoreCount, const grfx::Semaphore* const* ppWaitSemaphores)
{
    grfx::CommandBufferPtr commandBuffer = mCommandBuffers[mCurrentImageIndex];

    commandBuffer->Begin();
    commandBuffer->End();

    grfx::SubmitInfo sInfo   = {};
    sInfo.ppCommandBuffers   = &commandBuffer;
    sInfo.commandBufferCount = 1;
    sInfo.ppWaitSemaphores   = ppWaitSemaphores;
    sInfo.waitSemaphoreCount = waitSemaphoreCount;
    mCreateInfo.pQueue->Submit(&sInfo);

    return ppx::SUCCESS;
}

void Swapchain::RecordUI(uint32_t imageIndex, std::function<void(grfx::CommandBufferPtr)> f)
{
    PPX_ASSERT_MSG(!IsHeadless(), "Render UI on headless swapchain is not supported.");

    RecordPreamble(imageIndex);

    grfx::CommandBufferPtr commandBuffer = mCommandBuffers[mCurrentImageIndex];
    grfx::ImagePtr         image         = mDeviceTarget.colorImages[imageIndex];
    grfx::RenderPassPtr    renderPass    = mDeviceTarget.loadRenderPasses[imageIndex];

    commandBuffer->TransitionImageLayout(image, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_RENDER_TARGET);
    {
        grfx::RenderPassBeginInfo beginInfo = {};
        beginInfo.pRenderPass               = renderPass;
        beginInfo.renderArea                = renderPass->GetRenderArea();
        beginInfo.RTVClearCount             = 1;
        beginInfo.RTVClearValues[0]         = {{0, 0, 0, 0}};
        beginInfo.DSVClearValue             = {1.0f, 0xFF};

        commandBuffer->BeginRenderPass(&beginInfo);
        f(commandBuffer);
        commandBuffer->EndRenderPass();
    }
    commandBuffer->TransitionImageLayout(image, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_RENDER_TARGET, grfx::RESOURCE_STATE_PRESENT);
}

void Swapchain::RecordPreamble(uint32_t imageIndex)
{
    if (IsHeadless()) {
        return;
    }
    if (mIsRecording[imageIndex]) {
        return;
    }
    grfx::CommandBufferPtr commandBuffer = mCommandBuffers[imageIndex];
    commandBuffer->Begin();
    mIsRecording[imageIndex] = true;

    if (IsIndirect()) {
        grfx::ImagePtr      srcImage = mIndirectTarget.colorImages[imageIndex];
        grfx::ImagePtr      dstImage = mDeviceTarget.colorImages[imageIndex];
        grfx::RenderPassPtr dstClear = mDeviceTarget.clearRenderPasses[imageIndex];
        commandBuffer->TransitionImageLayout(dstImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_RENDER_TARGET);
        {
            // Clear screen.
            grfx::RenderPassPtr renderPass = dstClear;

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
            grfx::ImageToImageCopyInfo imcopy = {};

            imcopy.extent.x = std::min(mCreateInfo.width, mIndirectTarget.width);
            imcopy.extent.y = std::min(mCreateInfo.height, mIndirectTarget.height);

            // Copy rendered image.
            // Note(tianc): this should be a image blit instead of copy.
            commandBuffer->TransitionImageLayout(srcImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_COPY_SRC);
            commandBuffer->CopyImageToImage(&imcopy, srcImage, dstImage);
            commandBuffer->TransitionImageLayout(srcImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_COPY_SRC, grfx::RESOURCE_STATE_PRESENT);
        }
        commandBuffer->TransitionImageLayout(dstImage, PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_COPY_DST, grfx::RESOURCE_STATE_PRESENT);
    }
}

Result Swapchain::SetIndirectRenderSize(uint32_t width, uint32_t height)
{
    mIsIndirect = (width != 0 && height != 0);
    if (mIndirectTarget.width == width && mIndirectTarget.height == height) {
        return ppx::SUCCESS;
    }
    GetDevice()->GetGraphicsQueue()->WaitIdle();
    DestroyIndirectTarget();
    if (!mIsIndirect) {
        return ppx::SUCCESS;
    }
    return CreateIndirectTarget(width, height);
}

} // namespace grfx
} // namespace ppx
