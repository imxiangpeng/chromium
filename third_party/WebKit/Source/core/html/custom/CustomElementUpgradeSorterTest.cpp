// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/html/custom/CustomElementUpgradeSorter.h"

#include <memory>
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/StringOrDictionary.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/ShadowRoot.h"
#include "core/dom/ShadowRootInit.h"
#include "core/html/HTMLDocument.h"
#include "core/testing/DummyPageHolder.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/text/AtomicString.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class CustomElementUpgradeSorterTest : public ::testing::Test {
 protected:
  void SetUp() override { page_ = DummyPageHolder::Create(IntSize(1, 1)); }

  void TearDown() override { page_ = nullptr; }

  Element* CreateElementWithId(const char* local_name, const char* id) {
    NonThrowableExceptionState no_exceptions;
    Element* element = GetDocument()->createElement(
        local_name, StringOrDictionary(), no_exceptions);
    element->setAttribute(HTMLNames::idAttr, id);
    return element;
  }

  Document* GetDocument() { return &page_->GetDocument(); }

  ScriptState* GetScriptState() {
    return ToScriptStateForMainWorld(&page_->GetFrame());
  }

  ShadowRoot* AttachShadowTo(Element* element) {
    NonThrowableExceptionState no_exceptions;
    ShadowRootInit shadow_root_init;
    shadow_root_init.setMode("open");
    return element->attachShadow(GetScriptState(), shadow_root_init,
                                 no_exceptions);
  }

 private:
  std::unique_ptr<DummyPageHolder> page_;
};

TEST_F(CustomElementUpgradeSorterTest, inOtherDocument_notInSet) {
  NonThrowableExceptionState no_exceptions;
  Element* element =
      GetDocument()->createElement("a-a", StringOrDictionary(), no_exceptions);

  Document* other_document = HTMLDocument::CreateForTest();
  other_document->AppendChild(element);
  EXPECT_EQ(other_document, element->ownerDocument())
      << "sanity: another document should have adopted an element on append";

  CustomElementUpgradeSorter sorter;
  sorter.Add(element);

  HeapVector<Member<Element>> elements;
  sorter.Sorted(&elements, GetDocument());
  EXPECT_EQ(0u, elements.size())
      << "the adopted-away candidate should not have been included";
}

TEST_F(CustomElementUpgradeSorterTest, oneCandidate) {
  NonThrowableExceptionState no_exceptions;
  Element* element =
      GetDocument()->createElement("a-a", StringOrDictionary(), no_exceptions);
  GetDocument()->documentElement()->AppendChild(element);

  CustomElementUpgradeSorter sorter;
  sorter.Add(element);

  HeapVector<Member<Element>> elements;
  sorter.Sorted(&elements, GetDocument());
  EXPECT_EQ(1u, elements.size())
      << "exactly one candidate should be in the result set";
  EXPECT_TRUE(elements.Contains(element))
      << "the candidate should be the element that was added";
}

TEST_F(CustomElementUpgradeSorterTest, candidatesInDocumentOrder) {
  Element* a = CreateElementWithId("a-a", "a");
  Element* b = CreateElementWithId("a-a", "b");
  Element* c = CreateElementWithId("a-a", "c");

  GetDocument()->documentElement()->AppendChild(a);
  a->AppendChild(b);
  GetDocument()->documentElement()->AppendChild(c);

  CustomElementUpgradeSorter sorter;
  sorter.Add(b);
  sorter.Add(a);
  sorter.Add(c);

  HeapVector<Member<Element>> elements;
  sorter.Sorted(&elements, GetDocument());
  EXPECT_EQ(3u, elements.size());
  EXPECT_EQ(a, elements[0].Get());
  EXPECT_EQ(b, elements[1].Get());
  EXPECT_EQ(c, elements[2].Get());
}

TEST_F(CustomElementUpgradeSorterTest, sorter_ancestorInSet) {
  // A*
  // + B
  //   + C*
  Element* a = CreateElementWithId("a-a", "a");
  Element* b = CreateElementWithId("a-a", "b");
  Element* c = CreateElementWithId("a-a", "c");

  GetDocument()->documentElement()->AppendChild(a);
  a->AppendChild(b);
  b->AppendChild(c);

  CustomElementUpgradeSorter sort;
  sort.Add(c);
  sort.Add(a);

  HeapVector<Member<Element>> elements;
  sort.Sorted(&elements, GetDocument());
  EXPECT_EQ(2u, elements.size());
  EXPECT_EQ(a, elements[0].Get());
  EXPECT_EQ(c, elements[1].Get());
}

TEST_F(CustomElementUpgradeSorterTest, sorter_deepShallow) {
  // A
  // + B*
  // C*
  Element* a = CreateElementWithId("a-a", "a");
  Element* b = CreateElementWithId("a-a", "b");
  Element* c = CreateElementWithId("a-a", "c");

  GetDocument()->documentElement()->AppendChild(a);
  a->AppendChild(b);
  GetDocument()->documentElement()->AppendChild(c);

  CustomElementUpgradeSorter sort;
  sort.Add(b);
  sort.Add(c);

  HeapVector<Member<Element>> elements;
  sort.Sorted(&elements, GetDocument());
  EXPECT_EQ(2u, elements.size());
  EXPECT_EQ(b, elements[0].Get());
  EXPECT_EQ(c, elements[1].Get());
}

TEST_F(CustomElementUpgradeSorterTest, sorter_shallowDeep) {
  // A*
  // B
  // + C*
  Element* a = CreateElementWithId("a-a", "a");
  Element* b = CreateElementWithId("a-a", "b");
  Element* c = CreateElementWithId("a-a", "c");

  GetDocument()->documentElement()->AppendChild(a);
  GetDocument()->documentElement()->AppendChild(b);
  b->AppendChild(c);

  CustomElementUpgradeSorter sort;
  sort.Add(a);
  sort.Add(c);

  HeapVector<Member<Element>> elements;
  sort.Sorted(&elements, GetDocument());
  EXPECT_EQ(2u, elements.size());
  EXPECT_EQ(a, elements[0].Get());
  EXPECT_EQ(c, elements[1].Get());
}

TEST_F(CustomElementUpgradeSorterTest, sorter_shadow) {
  // A*
  // + {ShadowRoot}
  // | + B
  // |   + C*
  // + D*
  Element* a = CreateElementWithId("a-a", "a");
  Element* b = CreateElementWithId("a-a", "b");
  Element* c = CreateElementWithId("a-a", "c");
  Element* d = CreateElementWithId("a-a", "d");

  GetDocument()->documentElement()->AppendChild(a);
  ShadowRoot* s = AttachShadowTo(a);
  a->AppendChild(d);

  s->AppendChild(b);
  b->AppendChild(c);

  CustomElementUpgradeSorter sort;
  sort.Add(a);
  sort.Add(c);
  sort.Add(d);

  HeapVector<Member<Element>> elements;
  sort.Sorted(&elements, GetDocument());
  EXPECT_EQ(3u, elements.size());
  EXPECT_EQ(a, elements[0].Get());
  EXPECT_EQ(c, elements[1].Get());
  EXPECT_EQ(d, elements[2].Get());
}

// TODO(kochi): Add test cases which uses HTML imports.

}  // namespace blink
