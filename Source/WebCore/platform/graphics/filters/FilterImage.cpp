/*
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FilterImage.h"

#include "Filter.h"
#include "GraphicsContext.h"
#include "ImageBuffer.h"
#include "PixelBuffer.h"
#include "PixelBufferConversion.h"
#include <wtf/text/ParsingUtilities.h>

#if HAVE(ARM_NEON_INTRINSICS)
#include <arm_neon.h>
#endif

namespace WebCore {

RefPtr<FilterImage> FilterImage::create(const FloatRect& primitiveSubregion, const FloatRect& imageRect, const IntRect& absoluteImageRect, bool isAlphaImage, bool isValidPremultiplied, RenderingMode renderingMode, const DestinationColorSpace& colorSpace, ImageBufferAllocator& allocator)
{
    ASSERT(!ImageBuffer::sizeNeedsClamping(absoluteImageRect.size()));
    return adoptRef(new FilterImage(primitiveSubregion, imageRect, absoluteImageRect, isAlphaImage, isValidPremultiplied, renderingMode, colorSpace, allocator));
}

RefPtr<FilterImage> FilterImage::create(const FloatRect& primitiveSubregion, const FloatRect& imageRect, const IntRect& absoluteImageRect, Ref<ImageBuffer>&& imageBuffer, ImageBufferAllocator& allocator)
{
    return adoptRef(*new FilterImage(primitiveSubregion, imageRect, absoluteImageRect, WTFMove(imageBuffer), allocator));
}

FilterImage::FilterImage(const FloatRect& primitiveSubregion, const FloatRect& imageRect, const IntRect& absoluteImageRect, bool isAlphaImage, bool isValidPremultiplied, RenderingMode renderingMode, const DestinationColorSpace& colorSpace, ImageBufferAllocator& allocator)
    : m_primitiveSubregion(primitiveSubregion)
    , m_imageRect(imageRect)
    , m_absoluteImageRect(absoluteImageRect)
    , m_isAlphaImage(isAlphaImage)
    , m_isValidPremultiplied(isValidPremultiplied)
    , m_renderingMode(renderingMode)
    , m_colorSpace(colorSpace)
    , m_allocator(allocator)
{
}

FilterImage::FilterImage(const FloatRect& primitiveSubregion, const FloatRect& imageRect, const IntRect& absoluteImageRect, Ref<ImageBuffer>&& imageBuffer, ImageBufferAllocator& allocator)
    : m_primitiveSubregion(primitiveSubregion)
    , m_imageRect(imageRect)
    , m_absoluteImageRect(absoluteImageRect)
    , m_renderingMode(imageBuffer->renderingMode())
    , m_colorSpace(imageBuffer->colorSpace())
    , m_imageBuffer(WTFMove(imageBuffer))
    , m_allocator(allocator)
{
}

FloatRect FilterImage::maxEffectRect(const Filter& filter) const
{
    return filter.maxEffectRect(m_primitiveSubregion);
}

IntRect FilterImage::absoluteImageRectRelativeTo(const FilterImage& origin) const
{
    return m_absoluteImageRect - origin.absoluteImageRect().location();
}

FloatPoint FilterImage::mappedAbsolutePoint(const FloatPoint& point) const
{
    return FloatPoint(point - m_absoluteImageRect.location());
}

size_t FilterImage::memoryCost() const
{
    CheckedSize memoryCost;

    if (m_imageBuffer)
        memoryCost += m_imageBuffer->memoryCost();

    if (m_unpremultipliedPixelBuffer)
        memoryCost += m_unpremultipliedPixelBuffer->bytes().size();

    if (m_premultipliedPixelBuffer)
        memoryCost += m_premultipliedPixelBuffer->bytes().size();

#if USE(CORE_IMAGE)
    if (m_ciImage)
        memoryCost += memoryCostOfCIImage();
#endif

    return memoryCost;
}

ImageBuffer* FilterImage::imageBuffer()
{
#if USE(CORE_IMAGE)
    if (m_ciImage)
        return imageBufferFromCIImage();
#endif
    return imageBufferFromPixelBuffer();
}

ImageBuffer* FilterImage::imageBufferFromPixelBuffer()
{
    if (m_imageBuffer)
        return m_imageBuffer.get();

    RefPtr imageBuffer = m_allocator.createImageBuffer(m_absoluteImageRect.size(), m_colorSpace, m_renderingMode);
    m_imageBuffer = imageBuffer;
    if (!imageBuffer)
        return nullptr;

    auto imageBufferRect = IntRect { { }, m_absoluteImageRect.size() };

    if (pixelBufferSlot(AlphaPremultiplication::Premultiplied))
        imageBuffer->putPixelBuffer(*pixelBufferSlot(AlphaPremultiplication::Premultiplied), imageBufferRect);
    else if (pixelBufferSlot(AlphaPremultiplication::Unpremultiplied))
        imageBuffer->putPixelBuffer(*pixelBufferSlot(AlphaPremultiplication::Unpremultiplied), imageBufferRect);

    return m_imageBuffer.get();
}

static void copyImageBytes(const PixelBuffer& sourcePixelBuffer, PixelBuffer& destinationPixelBuffer)
{
    ASSERT(sourcePixelBuffer.size() == destinationPixelBuffer.size());

    auto destinationSize = destinationPixelBuffer.size();
    auto rowBytes = CheckedUint32(destinationSize.width()) * 4;
    if (rowBytes.hasOverflowed()) [[unlikely]]
        return;

    ConstPixelBufferConversionView source { sourcePixelBuffer.format(), rowBytes, sourcePixelBuffer.bytes() };
    PixelBufferConversionView destination { destinationPixelBuffer.format(), rowBytes, destinationPixelBuffer.bytes() };

    convertImagePixels(source, destination, destinationSize);
}

static void copyImageBytes(const PixelBuffer& sourcePixelBuffer, PixelBuffer& destinationPixelBuffer, const IntRect& sourceRect)
{
    auto sourcePixelBufferRect = IntRect { { }, sourcePixelBuffer.size() };
    auto destinationPixelBufferRect = IntRect { { }, destinationPixelBuffer.size() };

    auto sourceRectClipped = intersection(sourcePixelBufferRect, sourceRect);
    auto destinationRect = IntRect { { }, sourceRectClipped.size() };

    if (sourceRect.x() < 0)
        destinationRect.setX(-sourceRect.x());

    if (sourceRect.y() < 0)
        destinationRect.setY(-sourceRect.y());

    destinationRect.intersect(destinationPixelBufferRect);
    sourceRectClipped.setSize(destinationRect.size());

    // Initialize the destination to transparent black, if not entirely covered by the source.
    if (destinationRect.size() != destinationPixelBufferRect.size())
        destinationPixelBuffer.zeroFill();

    // Early return if the rect does not intersect with the source.
    if (destinationRect.isEmpty())
        return;

    auto size = CheckedUint32(sourceRectClipped.width()) * 4;
    auto destinationBytesPerRow = CheckedUint32(destinationPixelBufferRect.width()) * 4;
    auto sourceBytesPerRow = CheckedUint32(sourcePixelBufferRect.width()) * 4;
    auto destinationOffset = destinationRect.y() * destinationBytesPerRow + CheckedUint32(destinationRect.x()) * 4;
    auto sourceOffset = sourceRectClipped.y() * sourceBytesPerRow + CheckedUint32(sourceRectClipped.x()) * 4;

    if (size.hasOverflowed() || destinationBytesPerRow.hasOverflowed() || sourceBytesPerRow.hasOverflowed() || destinationOffset.hasOverflowed() || sourceOffset.hasOverflowed()) [[unlikely]]
        return;

    auto destinationPixel = destinationPixelBuffer.bytes().subspan(destinationOffset.value());
    auto sourcePixel = sourcePixelBuffer.bytes().subspan(sourceOffset.value());

    for (int y = 0; y < sourceRectClipped.height(); ++y) {
        if (y) {
            skip(destinationPixel, destinationBytesPerRow);
            skip(sourcePixel, sourceBytesPerRow);
        }
        memcpySpan(destinationPixel, sourcePixel.first(size));
    }
}

static RefPtr<PixelBuffer> getConvertedPixelBuffer(ImageBuffer& imageBuffer, AlphaPremultiplication alphaFormat, const IntRect& sourceRect, DestinationColorSpace colorSpace, ImageBufferAllocator& allocator)
{
    auto clampedSize = ImageBuffer::clampedSize(sourceRect.size());
    auto convertedImageBuffer = allocator.createImageBuffer(clampedSize, colorSpace, RenderingMode::Unaccelerated);
    
    if (!convertedImageBuffer)
        return nullptr;

    // Color space conversion happens internally when drawing from one image buffer to another
    convertedImageBuffer->context().drawImageBuffer(imageBuffer, sourceRect);
    PixelBufferFormat format { alphaFormat, PixelFormat::RGBA8, colorSpace };
    return convertedImageBuffer->getPixelBuffer(format, sourceRect, allocator);
}

static RefPtr<PixelBuffer> getConvertedPixelBuffer(PixelBuffer& sourcePixelBuffer, AlphaPremultiplication alphaFormat, DestinationColorSpace colorSpace, ImageBufferAllocator& allocator)
{
    auto sourceRect = IntRect { { } , sourcePixelBuffer.size() };
    auto clampedSize = ImageBuffer::clampedSize(sourceRect.size());

    auto& sourceColorSpace = sourcePixelBuffer.format().colorSpace;
    auto imageBuffer = allocator.createImageBuffer(clampedSize, sourceColorSpace, RenderingMode::Unaccelerated);
    if (!imageBuffer)
        return nullptr;

    imageBuffer->putPixelBuffer(sourcePixelBuffer, sourceRect);
    return getConvertedPixelBuffer(*imageBuffer, alphaFormat, sourceRect, colorSpace, allocator);
}

bool FilterImage::requiresPixelBufferColorSpaceConversion(std::optional<DestinationColorSpace> colorSpace) const
{
#if USE(CG) || USE(SKIA)
    // This function determines whether we need the step of an extra color space conversion
    // We only need extra color conversion when 1) color space is different in the input
    // AND 2) the filter is manipulating raw pixels
    return colorSpace && m_colorSpace != *colorSpace;
#else
    // Additional color space conversion is not needed on non-CG
    UNUSED_PARAM(colorSpace);
    return false;
#endif
}

RefPtr<PixelBuffer>& FilterImage::pixelBufferSlot(AlphaPremultiplication alphaFormat)
{
    return alphaFormat == AlphaPremultiplication::Unpremultiplied ? m_unpremultipliedPixelBuffer : m_premultipliedPixelBuffer;
}

PixelBuffer* FilterImage::pixelBuffer(AlphaPremultiplication alphaFormat)
{
    auto& pixelBuffer = pixelBufferSlot(alphaFormat);
    if (pixelBuffer)
        return pixelBuffer.get();

    PixelBufferFormat format { alphaFormat, PixelFormat::RGBA8, m_colorSpace };

    if (RefPtr imageBuffer = m_imageBuffer) {
        pixelBuffer = imageBuffer->getPixelBuffer(format, { { }, m_absoluteImageRect.size() }, m_allocator);
        if (!pixelBuffer)
            return nullptr;
        return pixelBuffer.get();
    }

    IntSize logicalSize(m_absoluteImageRect.size());
    ASSERT(!ImageBuffer::sizeNeedsClamping(logicalSize));

    pixelBuffer = m_allocator.createPixelBuffer(format, logicalSize);
    if (!pixelBuffer)
        return nullptr;

    if (alphaFormat == AlphaPremultiplication::Unpremultiplied) {
        if (auto& sourcePixelBuffer = pixelBufferSlot(AlphaPremultiplication::Premultiplied))
            copyImageBytes(*sourcePixelBuffer, *pixelBuffer);
    } else {
        if (auto& sourcePixelBuffer = pixelBufferSlot(AlphaPremultiplication::Unpremultiplied))
            copyImageBytes(*sourcePixelBuffer, *pixelBuffer);
    }

    return pixelBuffer.get();
}

RefPtr<PixelBuffer> FilterImage::getPixelBuffer(AlphaPremultiplication alphaFormat, const IntRect& sourceRect, std::optional<DestinationColorSpace> colorSpace)
{
    ASSERT(!ImageBuffer::sizeNeedsClamping(sourceRect.size()));

    PixelBufferFormat format { alphaFormat, PixelFormat::RGBA8, colorSpace? *colorSpace : m_colorSpace };

    auto pixelBuffer = m_allocator.createPixelBuffer(format, sourceRect.size());
    if (!pixelBuffer)
        return nullptr;

    copyPixelBuffer(*pixelBuffer, sourceRect);
    return pixelBuffer;
}

void FilterImage::copyPixelBuffer(PixelBuffer& destinationPixelBuffer, const IntRect& sourceRect)
{
    auto alphaFormat = destinationPixelBuffer.format().alphaFormat;
    auto& colorSpace = destinationPixelBuffer.format().colorSpace;

    RefPtr sourcePixelBuffer = pixelBufferSlot(alphaFormat) ? pixelBufferSlot(alphaFormat).get() : nullptr;

    if (!sourcePixelBuffer) {
        if (requiresPixelBufferColorSpaceConversion(colorSpace)) {
            // We prefer a conversion from the image buffer.
            if (m_imageBuffer) {
                IntRect rect { { }, m_absoluteImageRect.size() };
                if (auto convertedPixelBuffer = getConvertedPixelBuffer(Ref { *m_imageBuffer }, alphaFormat, rect, colorSpace, m_allocator))
                    copyImageBytes(*convertedPixelBuffer, destinationPixelBuffer, sourceRect);
                return;
            }
        }

        sourcePixelBuffer = this->pixelBuffer(alphaFormat);
    }

    if (!sourcePixelBuffer)
        return;

    if (requiresPixelBufferColorSpaceConversion(colorSpace)) {
        if (auto convertedPixelBuffer = getConvertedPixelBuffer(*sourcePixelBuffer, alphaFormat, colorSpace, m_allocator))
            copyImageBytes(*convertedPixelBuffer, destinationPixelBuffer, sourceRect);
        return;
    }

    copyImageBytes(*sourcePixelBuffer, destinationPixelBuffer, sourceRect);
}

void FilterImage::correctPremultipliedPixelBuffer()
{
    // Must operate on pre-multiplied results; other formats cannot have invalid pixels.
    if (!m_premultipliedPixelBuffer || m_isValidPremultiplied)
        return;

    auto pixelBytes = m_premultipliedPixelBuffer->bytes();
    size_t index = 0;

    // We must have four bytes per pixel, and complete pixels
    ASSERT(!(pixelBytes.size() % 4));

#if HAVE(ARM_NEON_INTRINSICS)
    if (pixelBytes.size() >= 64) {
        size_t endIndex = pixelBytes.size() & ~0x3f;
        do {
            // Increments pixelBytes by 64.
            auto* currentBytes = pixelBytes.subspan(index).data();
            uint8x16x4_t sixteenPixels = vld4q_u8(currentBytes);
            sixteenPixels.val[0] = vminq_u8(sixteenPixels.val[0], sixteenPixels.val[3]);
            sixteenPixels.val[1] = vminq_u8(sixteenPixels.val[1], sixteenPixels.val[3]);
            sixteenPixels.val[2] = vminq_u8(sixteenPixels.val[2], sixteenPixels.val[3]);
            vst4q_u8(currentBytes, sixteenPixels);
            index += 64;
        } while (index < endIndex);

        skip(pixelBytes, index);
        index = 0;
        if (pixelBytes.empty())
            return;
    }
#endif

    int numPixels = pixelBytes.size() / 4;

    // Iterate over each pixel, checking alpha and adjusting color components if necessary
    while (--numPixels >= 0) {
        // Alpha is the 4th byte in a pixel
        uint8_t a = pixelBytes[index + 3];
        // Clamp each component to alpha, and increment the pixel location
        for (int i = 0; i < 3; ++i) {
            if (pixelBytes[index] > a)
                pixelBytes[index] = a;
            ++index;
        }
        // Increment for alpha
        ++index;
    }
}

void FilterImage::transformToColorSpace(const DestinationColorSpace& colorSpace)
{
#if USE(CG) || USE(SKIA)
    // CG and SKIA handle color space adjustments internally.
    UNUSED_PARAM(colorSpace);
#else
    if (colorSpace == m_colorSpace)
        return;

    // FIXME: We can avoid this potentially unnecessary ImageBuffer conversion by adding
    // color space transform support for the {pre,un}multiplied arrays.
    if (RefPtr imageBuffer = this->imageBuffer())
        imageBuffer->transformToColorSpace(colorSpace);

    m_colorSpace = colorSpace;
    m_unpremultipliedPixelBuffer = nullptr;
    m_premultipliedPixelBuffer = nullptr;
#endif
}

} // namespace WebCore
