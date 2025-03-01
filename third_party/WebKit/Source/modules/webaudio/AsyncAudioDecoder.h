/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef AsyncAudioDecoder_h
#define AsyncAudioDecoder_h

#include <memory>
#include "platform/heap/Handle.h"

namespace blink {

class BaseAudioContext;
class AudioBuffer;
class AudioBus;
class DecodeErrorCallback;
class DecodeSuccessCallback;
class DOMArrayBuffer;
class ScriptPromiseResolver;

// AsyncAudioDecoder asynchronously decodes audio file data from a
// DOMArrayBuffer in the background thread. Upon successful decoding, a
// completion callback will be invoked with the decoded PCM data in an
// AudioBuffer.

class AsyncAudioDecoder {
  DISALLOW_NEW();
  WTF_MAKE_NONCOPYABLE(AsyncAudioDecoder);

 public:
  AsyncAudioDecoder(){};
  ~AsyncAudioDecoder(){};

  // Must be called on the main thread.  |decodeAsync| and callees must not
  // modify any of the parameters except |audioData|.  They are used to
  // associate this decoding instance with the caller to process the decoding
  // appropriately when finished.
  void DecodeAsync(DOMArrayBuffer* audio_data,
                   float sample_rate,
                   DecodeSuccessCallback*,
                   DecodeErrorCallback*,
                   ScriptPromiseResolver*,
                   BaseAudioContext*);

 private:
  AudioBuffer* CreateAudioBufferFromAudioBus(AudioBus*);
  static void DecodeOnBackgroundThread(DOMArrayBuffer* audio_data,
                                       float sample_rate,
                                       DecodeSuccessCallback*,
                                       DecodeErrorCallback*,
                                       ScriptPromiseResolver*,
                                       BaseAudioContext*);
  static void NotifyComplete(DOMArrayBuffer* audio_data,
                             DecodeSuccessCallback*,
                             DecodeErrorCallback*,
                             AudioBus*,
                             ScriptPromiseResolver*,
                             BaseAudioContext*);
};

}  // namespace blink

#endif  // AsyncAudioDecoder_h
