/*
 * Copyright (C) 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef InvalidationSet_h
#define InvalidationSet_h

#include <memory>
#include "core/CoreExport.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/HashSet.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/text/AtomicStringHash.h"
#include "platform/wtf/text/StringHash.h"

namespace blink {

class Element;
class TracedValue;

enum InvalidationType { kInvalidateDescendants, kInvalidateSiblings };

// Tracks data to determine which descendants in a DOM subtree, or
// siblings and their descendants, need to have style recalculated.
//
// Some example invalidation sets:
//
// .z {}
//   For class z we will have a DescendantInvalidationSet with invalidatesSelf
//   (the element itself is invalidated).
//
// .y .z {}
//   For class y we will have a DescendantInvalidationSet containing class z.
//
// .x ~ .z {}
//   For class x we will have a SiblingInvalidationSet containing class z, with
//   invalidatesSelf (the sibling itself is invalidated).
//
// .w ~ .y .z {}
//   For class w we will have a SiblingInvalidationSet containing class y, with
//   the SiblingInvalidationSet havings siblingDescendants containing class z.
//
// .v * {}
//   For class v we will have a DescendantInvalidationSet with
//   wholeSubtreeInvalid.
//
// .u ~ * {}
//   For class u we will have a SiblingInvalidationSet with wholeSubtreeInvalid
//   and invalidatesSelf (for all siblings, the sibling itself is invalidated).
//
// .t .v, .t ~ .z {}
//   For class t we will have a SiblingInvalidationSet containing class z, with
//   the SiblingInvalidationSet also holding descendants containing class v.
//
// We avoid virtual functions to minimize space consumption.
class CORE_EXPORT InvalidationSet {
  WTF_MAKE_NONCOPYABLE(InvalidationSet);
  USING_FAST_MALLOC_WITH_TYPE_NAME(blink::InvalidationSet);

 public:
  InvalidationType GetType() const {
    return static_cast<InvalidationType>(type_);
  }
  bool IsDescendantInvalidationSet() const {
    return GetType() == kInvalidateDescendants;
  }
  bool IsSiblingInvalidationSet() const {
    return GetType() == kInvalidateSiblings;
  }

  static void CacheTracingFlag();

  bool InvalidatesElement(Element&) const;

  void AddClass(const AtomicString& class_name);
  void AddId(const AtomicString& id);
  void AddTagName(const AtomicString& tag_name);
  void AddAttribute(const AtomicString& attribute_local_name);

  void SetWholeSubtreeInvalid();
  bool WholeSubtreeInvalid() const { return all_descendants_might_be_invalid_; }

  void SetInvalidatesSelf() { invalidates_self_ = true; }
  bool InvalidatesSelf() const { return invalidates_self_; }

  void SetTreeBoundaryCrossing() { tree_boundary_crossing_ = true; }
  bool TreeBoundaryCrossing() const { return tree_boundary_crossing_; }

  void SetInsertionPointCrossing() { insertion_point_crossing_ = true; }
  bool InsertionPointCrossing() const { return insertion_point_crossing_; }

  void SetCustomPseudoInvalid() { custom_pseudo_invalid_ = true; }
  bool CustomPseudoInvalid() const { return custom_pseudo_invalid_; }

  void SetInvalidatesSlotted() { invalidates_slotted_ = true; }
  bool InvalidatesSlotted() const { return invalidates_slotted_; }

  bool IsEmpty() const {
    return !classes_ && !ids_ && !tag_names_ && !attributes_ &&
           !custom_pseudo_invalid_ && !insertion_point_crossing_ &&
           !invalidates_slotted_;
  }

  bool IsAlive() const { return is_alive_; }

  void ToTracedValue(TracedValue*) const;

#ifndef NDEBUG
  void Show() const;
#endif

  const HashSet<AtomicString>& ClassSetForTesting() const {
    DCHECK(classes_);
    return *classes_;
  }
  const HashSet<AtomicString>& IdSetForTesting() const {
    DCHECK(ids_);
    return *ids_;
  }
  const HashSet<AtomicString>& TagNameSetForTesting() const {
    DCHECK(tag_names_);
    return *tag_names_;
  }
  const HashSet<AtomicString>& AttributeSetForTesting() const {
    DCHECK(attributes_);
    return *attributes_;
  }

  void Ref() { ++ref_count_; }
  void Deref() {
    DCHECK_GT(ref_count_, 0);
    --ref_count_;
    if (!ref_count_)
      Destroy();
  }

  void Combine(const InvalidationSet& other);

 protected:
  explicit InvalidationSet(InvalidationType);

  ~InvalidationSet() {
    CHECK(is_alive_);
    is_alive_ = false;
  }

 private:
  void Destroy();

  HashSet<AtomicString>& EnsureClassSet();
  HashSet<AtomicString>& EnsureIdSet();
  HashSet<AtomicString>& EnsureTagNameSet();
  HashSet<AtomicString>& EnsureAttributeSet();

  // Implement reference counting manually so we can call a derived
  // class destructor when the reference count decreases to 0.
  // If we use RefCounted instead, at least one of our compilers
  // requires the ability for RefCounted<InvalidationSet>::Deref()
  // to call ~InvalidationSet(), but this is not a virtual call.
  int ref_count_;

  // FIXME: optimize this if it becomes a memory issue.
  std::unique_ptr<HashSet<AtomicString>> classes_;
  std::unique_ptr<HashSet<AtomicString>> ids_;
  std::unique_ptr<HashSet<AtomicString>> tag_names_;
  std::unique_ptr<HashSet<AtomicString>> attributes_;

  unsigned type_ : 1;

  // If true, all descendants might be invalidated, so a full subtree recalc is
  // required.
  unsigned all_descendants_might_be_invalid_ : 1;

  // If true, the element or sibling itself is invalid.
  unsigned invalidates_self_ : 1;

  // If true, all descendants which are custom pseudo elements must be
  // invalidated.
  unsigned custom_pseudo_invalid_ : 1;

  // If true, the invalidation must traverse into ShadowRoots with this set.
  unsigned tree_boundary_crossing_ : 1;

  // If true, insertion point descendants must be invalidated.
  unsigned insertion_point_crossing_ : 1;

  // If true, distributed nodes of <slot> elements need to be invalidated.
  unsigned invalidates_slotted_ : 1;

  // If true, the instance is alive and can be used.
  unsigned is_alive_ : 1;
};

class CORE_EXPORT DescendantInvalidationSet final : public InvalidationSet {
 public:
  static RefPtr<DescendantInvalidationSet> Create() {
    return AdoptRef(new DescendantInvalidationSet);
  }

 private:
  DescendantInvalidationSet() : InvalidationSet(kInvalidateDescendants) {}
};

class CORE_EXPORT SiblingInvalidationSet final : public InvalidationSet {
 public:
  static RefPtr<SiblingInvalidationSet> Create(
      RefPtr<DescendantInvalidationSet> descendants) {
    return AdoptRef(new SiblingInvalidationSet(std::move(descendants)));
  }

  unsigned MaxDirectAdjacentSelectors() const {
    return max_direct_adjacent_selectors_;
  }
  void UpdateMaxDirectAdjacentSelectors(unsigned value) {
    max_direct_adjacent_selectors_ =
        std::max(value, max_direct_adjacent_selectors_);
  }

  DescendantInvalidationSet* SiblingDescendants() const {
    return sibling_descendant_invalidation_set_.Get();
  }
  DescendantInvalidationSet& EnsureSiblingDescendants();

  DescendantInvalidationSet* Descendants() const {
    return descendant_invalidation_set_.Get();
  }
  DescendantInvalidationSet& EnsureDescendants();

 private:
  explicit SiblingInvalidationSet(
      RefPtr<DescendantInvalidationSet> descendants);

  // Indicates the maximum possible number of siblings affected.
  unsigned max_direct_adjacent_selectors_;

  // Indicates the descendants of siblings.
  RefPtr<DescendantInvalidationSet> sibling_descendant_invalidation_set_;

  // Null if a given feature (class, attribute, id, pseudo-class) has only
  // a SiblingInvalidationSet and not also a DescendantInvalidationSet.
  RefPtr<DescendantInvalidationSet> descendant_invalidation_set_;
};

using InvalidationSetVector = Vector<RefPtr<InvalidationSet>>;

struct InvalidationLists {
  InvalidationSetVector descendants;
  InvalidationSetVector siblings;
};

DEFINE_TYPE_CASTS(DescendantInvalidationSet,
                  InvalidationSet,
                  value,
                  value->IsDescendantInvalidationSet(),
                  value.IsDescendantInvalidationSet());
DEFINE_TYPE_CASTS(SiblingInvalidationSet,
                  InvalidationSet,
                  value,
                  value->IsSiblingInvalidationSet(),
                  value.IsSiblingInvalidationSet());

}  // namespace blink

#endif  // InvalidationSet_h
