/*
 * Copyright (C) 2007 Henry Mason (hmason@mac.com)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights
 * reserved.
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
 *
 */

#ifndef MessageEvent_h
#define MessageEvent_h

#include <memory>
#include "bindings/core/v8/serialization/SerializedScriptValue.h"
#include "bindings/core/v8/serialization/UnpackedSerializedScriptValue.h"
#include "core/CoreExport.h"
#include "core/dom/MessagePort.h"
#include "core/events/Event.h"
#include "core/events/EventTarget.h"
#include "core/events/MessageEventInit.h"
#include "core/fileapi/Blob.h"
#include "core/typed_arrays/DOMArrayBuffer.h"
#include "platform/wtf/Compiler.h"

namespace blink {

class CORE_EXPORT MessageEvent final : public Event {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static MessageEvent* Create() { return new MessageEvent; }
  static MessageEvent* Create(MessagePortArray* ports,
                              const String& origin = String(),
                              const String& last_event_id = String(),
                              EventTarget* source = nullptr,
                              const String& suborigin = String()) {
    return new MessageEvent(origin, last_event_id, source, ports, suborigin);
  }
  static MessageEvent* Create(MessagePortArray* ports,
                              RefPtr<SerializedScriptValue> data,
                              const String& origin = String(),
                              const String& last_event_id = String(),
                              EventTarget* source = nullptr,
                              const String& suborigin = String()) {
    return new MessageEvent(std::move(data), origin, last_event_id, source,
                            ports, suborigin);
  }
  static MessageEvent* Create(MessagePortChannelArray channels,
                              RefPtr<SerializedScriptValue> data,
                              const String& origin = String(),
                              const String& last_event_id = String(),
                              EventTarget* source = nullptr,
                              const String& suborigin = String()) {
    return new MessageEvent(std::move(data), origin, last_event_id, source,
                            std::move(channels), suborigin);
  }
  static MessageEvent* Create(const String& data,
                              const String& origin = String(),
                              const String& suborigin = String()) {
    return new MessageEvent(data, origin, suborigin);
  }
  static MessageEvent* Create(Blob* data,
                              const String& origin = String(),
                              const String& suborigin = String()) {
    return new MessageEvent(data, origin, suborigin);
  }
  static MessageEvent* Create(DOMArrayBuffer* data,
                              const String& origin = String(),
                              const String& suborigin = String()) {
    return new MessageEvent(data, origin, suborigin);
  }
  static MessageEvent* Create(const AtomicString& type,
                              const MessageEventInit& initializer,
                              ExceptionState&);
  ~MessageEvent() override;

  void initMessageEvent(const AtomicString& type,
                        bool can_bubble,
                        bool cancelable,
                        ScriptValue data,
                        const String& origin,
                        const String& last_event_id,
                        EventTarget* source,
                        MessagePortArray*);
  void initMessageEvent(const AtomicString& type,
                        bool can_bubble,
                        bool cancelable,
                        PassRefPtr<SerializedScriptValue> data,
                        const String& origin,
                        const String& last_event_id,
                        EventTarget* source,
                        MessagePortArray*);
  void initMessageEvent(const AtomicString& type,
                        bool can_bubble,
                        bool cancelable,
                        const String& data,
                        const String& origin,
                        const String& last_event_id,
                        EventTarget* source,
                        MessagePortArray*);

  const String& origin() const { return origin_; }
  const String& suborigin() const { return suborigin_; }
  const String& lastEventId() const { return last_event_id_; }
  EventTarget* source() const { return source_.Get(); }
  MessagePortArray ports(bool& is_null) const;
  MessagePortArray ports() const;

  MessagePortChannelArray ReleaseChannels() { return std::move(channels_); }

  const AtomicString& InterfaceName() const override;

  enum DataType {
    kDataTypeScriptValue,
    kDataTypeSerializedScriptValue,
    kDataTypeString,
    kDataTypeBlob,
    kDataTypeArrayBuffer
  };
  DataType GetDataType() const { return data_type_; }
  ScriptValue DataAsScriptValue() const {
    DCHECK_EQ(data_type_, kDataTypeScriptValue);
    return data_as_script_value_;
  }
  // Use with caution. Since the data has already been unpacked, the underlying
  // SerializedScriptValue will no longer contain transferred contents.
  SerializedScriptValue* DataAsSerializedScriptValue() const {
    DCHECK_EQ(data_type_, kDataTypeSerializedScriptValue);
    return data_as_serialized_script_value_->Value();
  }
  UnpackedSerializedScriptValue* DataAsUnpackedSerializedScriptValue() const {
    DCHECK_EQ(data_type_, kDataTypeSerializedScriptValue);
    return data_as_serialized_script_value_.Get();
  }
  String DataAsString() const {
    DCHECK_EQ(data_type_, kDataTypeString);
    return data_as_string_;
  }
  Blob* DataAsBlob() const {
    DCHECK_EQ(data_type_, kDataTypeBlob);
    return data_as_blob_.Get();
  }
  DOMArrayBuffer* DataAsArrayBuffer() const {
    DCHECK_EQ(data_type_, kDataTypeArrayBuffer);
    return data_as_array_buffer_.Get();
  }

  void EntangleMessagePorts(ExecutionContext*);

  DECLARE_VIRTUAL_TRACE();

  WARN_UNUSED_RESULT v8::Local<v8::Object> AssociateWithWrapper(
      v8::Isolate*,
      const WrapperTypeInfo*,
      v8::Local<v8::Object> wrapper) override;

 private:
  MessageEvent();
  MessageEvent(const AtomicString&, const MessageEventInit&);
  MessageEvent(const String& origin,
               const String& last_event_id,
               EventTarget* source,
               MessagePortArray*,
               const String& suborigin);
  MessageEvent(RefPtr<SerializedScriptValue> data,
               const String& origin,
               const String& last_event_id,
               EventTarget* source,
               MessagePortArray*,
               const String& suborigin);
  MessageEvent(RefPtr<SerializedScriptValue> data,
               const String& origin,
               const String& last_event_id,
               EventTarget* source,
               MessagePortChannelArray,
               const String& suborigin);

  MessageEvent(const String& data,
               const String& origin,
               const String& suborigin);
  MessageEvent(Blob* data, const String& origin, const String& suborigin);
  MessageEvent(DOMArrayBuffer* data,
               const String& origin,
               const String& suborigin);

  DataType data_type_;
  ScriptValue data_as_script_value_;
  Member<UnpackedSerializedScriptValue> data_as_serialized_script_value_;
  String data_as_string_;
  Member<Blob> data_as_blob_;
  Member<DOMArrayBuffer> data_as_array_buffer_;
  String origin_;
  String last_event_id_;
  Member<EventTarget> source_;
  // m_ports are the MessagePorts in an entangled state, and m_channels are
  // the MessageChannels in a disentangled state. Only one of them can be
  // non-empty at a time. entangleMessagePorts() moves between the states.
  Member<MessagePortArray> ports_;
  MessagePortChannelArray channels_;
  String suborigin_;
};

}  // namespace blink

#endif  // MessageEvent_h
