#include "FrameUploadCBufferPerFrame.h"

#include <ShaderUtils\CBuffers.h>
#include <ResourceManager\UploadBufferManager.h>
#include <Utils\DebugUtils.h>

namespace BRE {
FrameUploadCBufferPerFrame::FrameUploadCBufferPerFrame()
{
    const std::size_t frameCBufferElemSize{ UploadBuffer::GetRoundedConstantBufferSizeInBytes(sizeof(FrameCBuffer)) };
    for (std::uint32_t i = 0U; i < _countof(mFrameCBuffers); ++i) {
        mFrameCBuffers[i] = &UploadBufferManager::CreateUploadBuffer(frameCBufferElemSize, 1U);
    }
}

UploadBuffer&
FrameUploadCBufferPerFrame::GetNextFrameCBuffer() noexcept
{
    UploadBuffer* frameCBuffer{ mFrameCBuffers[mCurrentFrameIndex] };
    BRE_ASSERT(frameCBuffer != nullptr);

    mCurrentFrameIndex = (mCurrentFrameIndex + 1) % ApplicationSettings::sQueuedFrameCount;

    return *frameCBuffer;
}
}

