/*
 * Copyright (C) 2006 Apple Computer, Inc.  All rights reserved.
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ImageDecoder_h
#define ImageDecoder_h

#include <memory>
#include "platform/PlatformExport.h"
#include "platform/SharedBuffer.h"
#include "platform/graphics/ColorBehavior.h"
#include "platform/graphics/ImageOrientation.h"
#include "platform/image-decoders/ImageAnimation.h"
#include "platform/image-decoders/ImageFrame.h"
#include "platform/image-decoders/SegmentReader.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/Platform.h"
#include "third_party/skia/include/core/SkColorSpaceXform.h"

namespace blink {

#if SK_B32_SHIFT
inline SkColorSpaceXform::ColorFormat XformColorFormat() {
  return SkColorSpaceXform::kRGBA_8888_ColorFormat;
}
#else
inline SkColorSpaceXform::ColorFormat XformColorFormat() {
  return SkColorSpaceXform::kBGRA_8888_ColorFormat;
}
#endif

// ImagePlanes can be used to decode color components into provided buffers
// instead of using an ImageFrame.
class PLATFORM_EXPORT ImagePlanes final {
  USING_FAST_MALLOC(ImagePlanes);
  WTF_MAKE_NONCOPYABLE(ImagePlanes);

 public:
  ImagePlanes();
  ImagePlanes(void* planes[3], const size_t row_bytes[3]);

  void* Plane(int);
  size_t RowBytes(int) const;

 private:
  void* planes_[3];
  size_t row_bytes_[3];
};

// ImageDecoder is a base for all format-specific decoders
// (e.g. JPEGImageDecoder). This base manages the ImageFrame cache.
//
class PLATFORM_EXPORT ImageDecoder {
  WTF_MAKE_NONCOPYABLE(ImageDecoder);
  USING_FAST_MALLOC(ImageDecoder);

 public:
  static const size_t kNoDecodedImageByteLimit =
      Platform::kNoDecodedImageByteLimit;

  enum AlphaOption { kAlphaPremultiplied, kAlphaNotPremultiplied };

  virtual ~ImageDecoder() {}

  // Returns a caller-owned decoder of the appropriate type.  Returns nullptr if
  // we can't sniff a supported type from the provided data (possibly
  // because there isn't enough data yet).
  // Sets |max_decoded_bytes_| to Platform::MaxImageDecodedBytes().
  static std::unique_ptr<ImageDecoder> Create(RefPtr<SegmentReader> data,
                                              bool data_complete,
                                              AlphaOption,
                                              const ColorBehavior&);
  static std::unique_ptr<ImageDecoder> Create(
      RefPtr<SharedBuffer> data,
      bool data_complete,
      AlphaOption alpha_option,
      const ColorBehavior& color_behavior) {
    return Create(SegmentReader::CreateFromSharedBuffer(std::move(data)),
                  data_complete, alpha_option, color_behavior);
  }

  virtual String FilenameExtension() const = 0;

  bool IsAllDataReceived() const { return is_all_data_received_; }

  // Returns true if the buffer holds enough data to instantiate a decoder.
  // This is useful for callers to determine whether a decoder instantiation
  // failure is due to insufficient or bad data.
  static bool HasSufficientDataToSniffImageType(const SharedBuffer&);

  void SetData(PassRefPtr<SegmentReader> data, bool all_data_received) {
    if (failed_)
      return;
    data_ = std::move(data);
    is_all_data_received_ = all_data_received;
    OnSetData(data_.Get());
  }

  void SetData(RefPtr<SharedBuffer> data, bool all_data_received) {
    SetData(SegmentReader::CreateFromSharedBuffer(std::move(data)),
            all_data_received);
  }

  virtual void OnSetData(SegmentReader* data) {}

  bool IsSizeAvailable() {
    if (failed_)
      return false;
    if (!size_available_)
      DecodeSize();
    return IsDecodedSizeAvailable();
  }

  bool IsDecodedSizeAvailable() const { return !failed_ && size_available_; }

  virtual IntSize Size() const { return size_; }

  // Decoders which downsample images should override this method to
  // return the actual decoded size.
  virtual IntSize DecodedSize() const { return Size(); }

  // Image decoders that support YUV decoding must override this to
  // provide the size of each component.
  virtual IntSize DecodedYUVSize(int component) const {
    NOTREACHED();
    return IntSize();
  }

  // Image decoders that support YUV decoding must override this to
  // return the width of each row of the memory allocation.
  virtual size_t DecodedYUVWidthBytes(int component) const {
    NOTREACHED();
    return 0;
  }

  // This will only differ from size() for ICO (where each frame is a
  // different icon) or other formats where different frames are different
  // sizes. This does NOT differ from size() for GIF or WebP, since
  // decoding GIF or WebP composites any smaller frames against previous
  // frames to create full-size frames.
  virtual IntSize FrameSizeAtIndex(size_t) const { return Size(); }

  // Returns whether the size is legal (i.e. not going to result in
  // overflow elsewhere).  If not, marks decoding as failed.
  virtual bool SetSize(unsigned width, unsigned height) {
    if (SizeCalculationMayOverflow(width, height))
      return SetFailed();

    size_ = IntSize(width, height);
    size_available_ = true;
    return true;
  }

  // Calls DecodeFrameCount() to get the frame count (if possible), without
  // decoding the individual frames.  Resizes |frame_buffer_cache_| to the
  // correct size and returns its size.
  size_t FrameCount();

  virtual int RepetitionCount() const { return kAnimationNone; }

  // Decodes as much of the requested frame as possible, and returns an
  // ImageDecoder-owned pointer.
  ImageFrame* DecodeFrameBufferAtIndex(size_t);

  // Whether the requested frame has alpha.
  virtual bool FrameHasAlphaAtIndex(size_t) const;

  // Whether or not the frame is fully received.
  virtual bool FrameIsReceivedAtIndex(size_t) const;

  // Returns true if a cached complete decode is available.
  bool FrameIsDecodedAtIndex(size_t) const;

  // Duration for displaying a frame in milliseconds. This method is only used
  // by animated images.
  virtual float FrameDurationAtIndex(size_t) const { return 0; }

  // Number of bytes in the decoded frame. Returns 0 if the decoder doesn't
  // have this frame cached (either because it hasn't been decoded, or because
  // it has been cleared).
  virtual size_t FrameBytesAtIndex(size_t) const;

  ImageOrientation Orientation() const { return orientation_; }

  bool IgnoresColorSpace() const { return color_behavior_.IsIgnore(); }
  const ColorBehavior& GetColorBehavior() const { return color_behavior_; }

  // This returns the color space that will be included in the SkImageInfo of
  // SkImages created from this decoder. This will be nullptr unless the
  // decoder was created with the option ColorSpaceTagged.
  sk_sp<SkColorSpace> ColorSpaceForSkImages() const;

  // This returns whether or not the image included a not-ignored embedded
  // color space. This is independent of whether or not that space's transform
  // has been baked into the pixel values.
  bool HasEmbeddedColorSpace() const { return embedded_color_space_.get(); }

  // Set the embedded color space directly or via ICC profile.
  void SetEmbeddedColorProfile(const char* icc_data, unsigned icc_length);
  void SetEmbeddedColorSpace(sk_sp<SkColorSpace> src_space);

  // Transformation from embedded color space to target color space.
  SkColorSpaceXform* ColorTransform();

  AlphaOption GetAlphaOption() const {
    return premultiply_alpha_ ? kAlphaPremultiplied : kAlphaNotPremultiplied;
  }

  // Sets the "decode failure" flag.  For caller convenience (since so
  // many callers want to return false after calling this), returns false
  // to enable easy tailcalling.  Subclasses may override this to also
  // clean up any local data.
  virtual bool SetFailed() {
    failed_ = true;
    return false;
  }

  bool Failed() const { return failed_; }

  // Clears decoded pixel data from all frames except the provided frame. If
  // subsequent frames depend on this frame's required previous frame, then that
  // frame is also kept in cache to prevent re-decoding from the beginning.
  // Callers may pass WTF::kNotFound to clear all frames.
  // Note: If |frame_buffer_cache_| contains only one frame, it won't be
  // cleared. Returns the number of bytes of frame data actually cleared.
  //
  // This is a virtual method because MockImageDecoder needs to override it in
  // order to run the test ImageFrameGeneratorTest::ClearMultiFrameDecode.
  //
  // @TODO  Let MockImageDecoder override ImageFrame::ClearFrameBuffer instead,
  //        so this method can be made non-virtual. It is used in the test
  //        ImageFrameGeneratorTest::ClearMultiFrameDecode. The test needs to
  //        be modified since two frames may be kept in cache, instead of
  //        always just one, with this ClearCacheExceptFrame implementation.
  virtual size_t ClearCacheExceptFrame(size_t);

  // If the image has a cursor hot-spot, stores it in the argument
  // and returns true. Otherwise returns false.
  virtual bool HotSpot(IntPoint&) const { return false; }

  virtual void SetMemoryAllocator(SkBitmap::Allocator* allocator) {
    // FIXME: this doesn't work for images with multiple frames.
    if (frame_buffer_cache_.IsEmpty()) {
      // Ensure that InitializeNewFrame is called, after parsing if
      // necessary.
      if (!FrameCount())
        return;
    }

    frame_buffer_cache_[0].SetMemoryAllocator(allocator);
  }

  virtual bool CanDecodeToYUV() { return false; }
  virtual bool DecodeToYUV() { return false; }
  virtual void SetImagePlanes(std::unique_ptr<ImagePlanes>) {}

 protected:
  ImageDecoder(AlphaOption alpha_option,
               const ColorBehavior& color_behavior,
               size_t max_decoded_bytes)
      : premultiply_alpha_(alpha_option == kAlphaPremultiplied),
        color_behavior_(color_behavior),
        max_decoded_bytes_(max_decoded_bytes),
        purge_aggressively_(false) {}

  // Calculates the most recent frame whose image data may be needed in
  // order to decode frame |frame_index|, based on frame disposal methods
  // and |frame_rect_is_opaque|, where |frame_rect_is_opaque| signifies whether
  // the rectangle of frame at |frame_index| is known to be opaque.
  // If no previous frame's data is required, returns WTF::kNotFound.
  //
  // This function requires that the previous frame's
  // |required_previous_frame_index_| member has been set correctly. The
  // easiest way to ensure this is for subclasses to call this method and
  // store the result on the frame via SetRequiredPreviousFrameIndex()
  // as soon as the frame has been created and parsed sufficiently to
  // determine the disposal method; assuming this happens for all frames
  // in order, the required invariant will hold.
  //
  // Image formats which do not use more than one frame do not need to
  // worry about this; see comments on
  // ImageFrame::required_previous_frame+index_.
  size_t FindRequiredPreviousFrame(size_t frame_index,
                                   bool frame_rect_is_opaque);

  // This is called by ClearCacheExceptFrame() if that method decides it wants
  // to preserve another frame, to avoid unnecessary redecoding.
  size_t ClearCacheExceptTwoFrames(size_t, size_t);
  virtual void ClearFrameBuffer(size_t frame_index);

  // Decodes the image sufficiently to determine the image size.
  virtual void DecodeSize() = 0;

  // Decodes the image sufficiently to determine the number of frames and
  // returns that number.
  virtual size_t DecodeFrameCount() { return 1; }

  // Called to initialize the frame buffer with the given index, based on the
  // provided and previous frame's characteristics. Returns true on success.
  // Before calling this method, the caller must verify that the frame exists.
  // On failure, the client should call SetFailed. This method does not call
  // SetFailed itself because that might delete the object directly making this
  // call.
  bool InitFrameBuffer(size_t);

  // Performs any additional setup of the requested frame after it has been
  // initially created, e.g. setting a duration or disposal method.
  virtual void InitializeNewFrame(size_t) {}

  // Decodes the requested frame.
  virtual void Decode(size_t) = 0;

  // This method is only required for animated images. It returns a vector with
  // all frame indices that need to be decoded in order to succesfully decode
  // the provided frame.  The indices are returned in reverse order, so the
  // last frame needs to be decoded first.  Before calling this method, the
  // caller must verify that the frame exists.
  Vector<size_t> FindFramesToDecode(size_t) const;

  // This is called by Decode() after decoding a frame in an animated image.
  // Before calling this method, the caller must verify that the frame exists.
  // @return true  if the frame was fully decoded,
  //         false otherwise.
  bool PostDecodeProcessing(size_t);

  // The GIF and PNG decoders set the default alpha setting of the ImageFrame to
  // true. When the frame rect does not contain any (semi-) transparent pixels,
  // this may need to be changed to false. This depends on whether the required
  // previous frame adds transparency to the image, outside of the frame rect.
  // This methods corrects the alpha setting of the frame buffer to false when
  // the whole frame is opaque.
  //
  // This method should be called by the GIF and PNG decoder when the pixels in
  // the frame rect do *not* contain any transparent pixels. Before calling
  // this method, the caller must verify that the frame exists.
  void CorrectAlphaWhenFrameBufferSawNoAlpha(size_t);

  RefPtr<SegmentReader> data_;  // The encoded data.
  Vector<ImageFrame, 1> frame_buffer_cache_;
  const bool premultiply_alpha_;
  const ColorBehavior color_behavior_;
  ImageOrientation orientation_;

  // The maximum amount of memory a decoded image should require. Ideally,
  // image decoders should downsample large images to fit under this limit
  // (and then return the downsampled size from DecodedSize()). Ignoring
  // this limit can cause excessive memory use or even crashes on low-
  // memory devices.
  const size_t max_decoded_bytes_;

  // While decoding, we may learn that there are so many animation frames that
  // we would go beyond our cache budget.
  // If that happens, purge_aggressively_ is set to true. This signals
  // future decodes to purge old frames as it goes.
  void UpdateAggressivePurging(size_t index);

  // The method is only relevant for multi-frame images.
  //
  // This method indicates whether the provided frame has enough data to decode
  // successive frames that depend on it. It is used by ClearCacheExceptFrame
  // to determine which frame to keep in cache when the indicated frame is not
  // yet sufficiently decoded.
  //
  // The default condition is that the frame status needs to be FramePartial or
  // FrameComplete, since the data of previous frames is copied in
  // InitFrameBuffer() before setting the status to FramePartial. For WebP,
  // however, the status needs to be FrameComplete since the complete buffer is
  // used to do alpha blending in WEBPImageDecoder::ApplyPostProcessing().
  //
  // Before calling this, verify that frame |index| exists by checking that
  // |index| is smaller than |frame_buffer_cache_|.size().
  virtual bool FrameStatusSufficientForSuccessors(size_t index) {
    DCHECK(index < frame_buffer_cache_.size());
    return frame_buffer_cache_[index].GetStatus() != ImageFrame::kFrameEmpty;
  }

 private:
  // Some code paths compute the size of the image as "width * height * 4"
  // and return it as a (signed) int.  Avoid overflow.
  static bool SizeCalculationMayOverflow(unsigned width, unsigned height) {
    unsigned long long total_size = static_cast<unsigned long long>(width) *
                                    static_cast<unsigned long long>(height);
    return total_size > ((1 << 29) - 1);
  }

  bool purge_aggressively_;

  // This methods gets called at the end of InitFrameBuffer. Subclasses can do
  // format specific initialization, for e.g. alpha settings, here.
  virtual void OnInitFrameBuffer(size_t){};

  // Called by InitFrameBuffer to determine if it can take the bitmap of the
  // previous frame. This condition is different for GIF and WEBP.
  virtual bool CanReusePreviousFrameBuffer(size_t) const { return false; }

  IntSize size_;
  bool size_available_ = false;
  bool is_all_data_received_ = false;
  bool failed_ = false;
  bool has_histogrammed_color_space_ = false;

  sk_sp<SkColorSpace> embedded_color_space_ = nullptr;
  bool source_to_target_color_transform_needs_update_ = false;
  std::unique_ptr<SkColorSpaceXform> source_to_target_color_transform_;
};

}  // namespace blink

#endif
