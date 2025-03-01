// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/RuleFeature.h"

#include "core/css/CSSSelectorList.h"
#include "core/css/RuleSet.h"
#include "core/css/StylePropertySet.h"
#include "core/css/StyleRule.h"
#include "core/css/invalidation/InvalidationSet.h"
#include "core/css/parser/CSSParser.h"
#include "core/dom/ElementTraversal.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLDocument.h"
#include "core/html/HTMLElement.h"
#include "core/html/HTMLHtmlElement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

// TODO(sof): consider making these part object helper abstractions
// available from platform/heap/.

template <typename T>
class HeapPartObject final
    : public GarbageCollectedFinalized<HeapPartObject<T>> {
 public:
  static HeapPartObject* Create() { return new HeapPartObject; }

  T* Get() { return &part_; }

  DEFINE_INLINE_TRACE() { visitor->Trace(part_); }

 private:
  HeapPartObject() {}

  T part_;
};

template <typename T>
class PersistentPartObject final {
  DISALLOW_NEW();

 public:
  PersistentPartObject() : part_(HeapPartObject<T>::Create()) {}

  T* operator->() const { return (*part_).Get(); }

 private:
  Persistent<HeapPartObject<T>> part_;
};

class RuleFeatureSetTest : public ::testing::Test {
 public:
  RuleFeatureSetTest() {}

  void SetUp() {
    document_ = HTMLDocument::CreateForTest();
    HTMLHtmlElement* html = HTMLHtmlElement::Create(*document_);
    html->AppendChild(HTMLBodyElement::Create(*document_));
    document_->AppendChild(html);

    document_->body()->setInnerHTML("<b><i></i></b>");
  }

  RuleFeatureSet::SelectorPreMatch CollectFeatures(
      const String& selector_text) {
    CSSSelectorList selector_list = CSSParser::ParseSelector(
        StrictCSSParserContext(), nullptr, selector_text);

    StyleRule* style_rule =
        StyleRule::Create(std::move(selector_list),
                          MutableStylePropertySet::Create(kHTMLStandardMode));
    RuleData rule_data(style_rule, 0, 0, kRuleHasNoSpecialState);
    return rule_feature_set_->CollectFeaturesFromRuleData(rule_data);
  }

  void ClearFeatures() { rule_feature_set_->Clear(); }

  void CollectInvalidationSetsForClass(InvalidationLists& invalidation_lists,
                                       const AtomicString& class_name) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_->CollectInvalidationSetsForClass(invalidation_lists,
                                                       *element, class_name);
  }

  void CollectInvalidationSetsForId(InvalidationLists& invalidation_lists,
                                    const AtomicString& id) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_->CollectInvalidationSetsForId(invalidation_lists,
                                                    *element, id);
  }

  void CollectInvalidationSetsForAttribute(
      InvalidationLists& invalidation_lists,
      const QualifiedName& attribute_name) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_->CollectInvalidationSetsForAttribute(
        invalidation_lists, *element, attribute_name);
  }

  void CollectInvalidationSetsForPseudoClass(
      InvalidationLists& invalidation_lists,
      CSSSelector::PseudoType pseudo) const {
    Element* element = Traversal<HTMLElement>::FirstChild(
        *Traversal<HTMLElement>::FirstChild(*document_->body()));
    rule_feature_set_->CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                                             *element, pseudo);
  }

  void CollectUniversalSiblingInvalidationSet(
      InvalidationLists& invalidation_lists) {
    rule_feature_set_->CollectUniversalSiblingInvalidationSet(
        invalidation_lists, 1);
  }

  void CollectNthInvalidationSet(InvalidationLists& invalidation_lists) {
    rule_feature_set_->CollectNthInvalidationSet(invalidation_lists);
  }

  const HashSet<AtomicString>& ClassSet(
      const InvalidationSet& invalidation_set) {
    return invalidation_set.ClassSetForTesting();
  }

  const HashSet<AtomicString>& IdSet(const InvalidationSet& invalidation_set) {
    return invalidation_set.IdSetForTesting();
  }

  const HashSet<AtomicString>& TagNameSet(
      const InvalidationSet& invalidation_set) {
    return invalidation_set.TagNameSetForTesting();
  }

  const HashSet<AtomicString>& AttributeSet(
      const InvalidationSet& invalidation_set) {
    return invalidation_set.AttributeSetForTesting();
  }

  void ExpectNoInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(0u, invalidation_sets.size());
  }

  void ExpectSelfInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_TRUE(invalidation_sets[0]->InvalidatesSelf());
  }

  void ExpectNoSelfInvalidation(InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_FALSE(invalidation_sets[0]->InvalidatesSelf());
  }

  void ExpectWholeSubtreeInvalidation(
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    EXPECT_TRUE(invalidation_sets[0]->WholeSubtreeInvalid());
  }

  void ExpectClassInvalidation(const AtomicString& class_name,
                               InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> classes = ClassSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, classes.size());
    EXPECT_TRUE(classes.Contains(class_name));
  }

  void ExpectSiblingInvalidation(unsigned max_direct_adjacent_selectors,
                                 const AtomicString& sibling_name,
                                 InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const SiblingInvalidationSet& sibling_invalidation_set =
        ToSiblingInvalidationSet(*invalidation_sets[0]);
    HashSet<AtomicString> classes = ClassSet(sibling_invalidation_set);
    EXPECT_EQ(1u, classes.size());
    EXPECT_TRUE(classes.Contains(sibling_name));
    EXPECT_EQ(max_direct_adjacent_selectors,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());
  }

  void ExpectSiblingDescendantInvalidation(
      unsigned max_direct_adjacent_selectors,
      const AtomicString& sibling_name,
      const AtomicString& descendant_name,
      InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    const SiblingInvalidationSet& sibling_invalidation_set =
        ToSiblingInvalidationSet(*invalidation_sets[0]);
    HashSet<AtomicString> classes = ClassSet(sibling_invalidation_set);
    EXPECT_EQ(1u, classes.size());
    EXPECT_TRUE(classes.Contains(sibling_name));
    EXPECT_EQ(max_direct_adjacent_selectors,
              sibling_invalidation_set.MaxDirectAdjacentSelectors());

    HashSet<AtomicString> descendant_classes =
        ClassSet(*sibling_invalidation_set.SiblingDescendants());
    EXPECT_EQ(1u, descendant_classes.size());
    EXPECT_TRUE(descendant_classes.Contains(descendant_name));
  }

  void ExpectClassesInvalidation(const AtomicString& first_class_name,
                                 const AtomicString& second_class_name,
                                 InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> classes = ClassSet(*invalidation_sets[0]);
    EXPECT_EQ(2u, classes.size());
    EXPECT_TRUE(classes.Contains(first_class_name));
    EXPECT_TRUE(classes.Contains(second_class_name));
  }

  void ExpectIdInvalidation(const AtomicString& id,
                            InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> ids = IdSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, ids.size());
    EXPECT_TRUE(ids.Contains(id));
  }

  void ExpectIdsInvalidation(const AtomicString& first_id,
                             const AtomicString& second_id,
                             InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> ids = IdSet(*invalidation_sets[0]);
    EXPECT_EQ(2u, ids.size());
    EXPECT_TRUE(ids.Contains(first_id));
    EXPECT_TRUE(ids.Contains(second_id));
  }

  void ExpectTagNameInvalidation(const AtomicString& tag_name,
                                 InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> tag_names = TagNameSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, tag_names.size());
    EXPECT_TRUE(tag_names.Contains(tag_name));
  }

  void ExpectTagNamesInvalidation(const AtomicString& first_tag_name,
                                  const AtomicString& second_tag_name,
                                  InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> tag_names = TagNameSet(*invalidation_sets[0]);
    EXPECT_EQ(2u, tag_names.size());
    EXPECT_TRUE(tag_names.Contains(first_tag_name));
    EXPECT_TRUE(tag_names.Contains(second_tag_name));
  }

  void ExpectAttributeInvalidation(const AtomicString& attribute,
                                   InvalidationSetVector& invalidation_sets) {
    EXPECT_EQ(1u, invalidation_sets.size());
    HashSet<AtomicString> attributes = AttributeSet(*invalidation_sets[0]);
    EXPECT_EQ(1u, attributes.size());
    EXPECT_TRUE(attributes.Contains(attribute));
  }

  void ExpectSiblingRuleCount(unsigned count) {
    EXPECT_EQ(count, rule_feature_set_->SiblingRules().size());
  }

  void ExpectUncommonAttributeRuleCount(unsigned count) {
    EXPECT_EQ(count, rule_feature_set_->UncommonAttributeRules().size());
  }

  void ExpectFullRecalcForRuleSetInvalidation(bool expected) {
    EXPECT_EQ(expected,
              rule_feature_set_->NeedsFullRecalcForRuleSetInvalidation());
  }

 private:
  PersistentPartObject<RuleFeatureSet> rule_feature_set_;
  Persistent<Document> document_;
};

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling1) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "p");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "o");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingInvalidation(1, "p", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling3) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "n");
  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("p", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling4) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "m");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingDescendantInvalidation(1, "n", "p", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling5) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".l ~ .m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "l");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingDescendantInvalidation(UINT_MAX, "n", "p",
                                      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, interleavedDescendantSibling6) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".k > .l ~ .m + .n .o + .p"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "k");
  ExpectClassInvalidation("p", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, anySibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(.q, .r) ~ .s .t"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "q");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectSiblingDescendantInvalidation(UINT_MAX, "s", "t",
                                      invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, any) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "w");
  ExpectSelfInvalidation(invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, anyIdDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :-webkit-any(#b, #c)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectIdsInvalidation("b", "c", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, anyTagDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a :-webkit-any(span, div)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectTagNamesInvalidation("span", "div", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, siblingAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".v ~ :-webkit-any(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "v");
  ExpectNoInvalidation(invalidation_lists.descendants);
  ExpectClassesInvalidation("w", "x", invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, descendantSiblingAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".u .v ~ :-webkit-any(.w, .x)"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "u");
  ExpectClassesInvalidation("w", "x", invalidation_lists.descendants);
  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, id) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#a #b"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForId(invalidation_lists, "a");
  ExpectIdInvalidation("b", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, attribute) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[c] [d]"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForAttribute(invalidation_lists,
                                      QualifiedName("", "c", ""));
  ExpectAttributeInvalidation("d", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, pseudoClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":valid"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                        CSSSelector::kPseudoValid);
  ExpectSelfInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, tagName) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":valid e"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForPseudoClass(invalidation_lists,
                                        CSSSelector::kPseudoValid);
  ExpectTagNameInvalidation("e", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, contentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a ::content .b"));
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".a .c"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassInvalidation("c", invalidation_lists.descendants);

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".a .b"));

  invalidation_lists.descendants.clear();
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectClassesInvalidation("b", "c", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nonMatchingHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches, CollectFeatures(".a:host"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host(.a)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("div :host .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":host:hover .a"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectNoInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nonMatchingHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(".a:host-context(*)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host-context(.a)"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("*:host-context(*) .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("div :host-context(div) .a"));
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":host-context(div):hover .a"));

  InvalidationLists invalidation_lists;
  CollectInvalidationSetsForClass(invalidation_lists, "a");
  ExpectNoInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, siblingRulesBeforeContentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("a + b ::content .c"));
  ExpectSiblingRuleCount(0);
}

TEST_F(RuleFeatureSetTest, siblingRulesBeforeContentPseudo2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("a + ::content .b"));
  ExpectSiblingRuleCount(0);
}

TEST_F(RuleFeatureSetTest, siblingRulesAfterContentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a ::content .b + .c"));
  ExpectSiblingRuleCount(1);
}

TEST_F(RuleFeatureSetTest, siblingRulesNthBeforeContentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2) ::content .a"));
  ExpectSiblingRuleCount(0);
}

TEST_F(RuleFeatureSetTest, siblingRulesNthAfterContentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a ::content :nth-child(2)"));
  ExpectSiblingRuleCount(1);
}

TEST_F(RuleFeatureSetTest, siblingRulesBeforeSlotted) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a + ::slotted(.b)"));
  ExpectSiblingRuleCount(0);
}

TEST_F(RuleFeatureSetTest, siblingRulesBeforeHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(".a + :host(.b)"));
  ExpectSiblingRuleCount(0);
}

TEST_F(RuleFeatureSetTest, siblingRulesBeforeHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(".a + :host-context(.b)"));
  ExpectSiblingRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesAfterContentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("div ::content [attr]"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesBeforeContentPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("[attr] ::content div"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesSlotted) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("::slotted([attr])"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesBeforeSlotted) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("[attr]::slotted(*)"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host([attr])"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesBeforeHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("[attr] :host"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesAfterHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":host[attr]"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesAfterHost2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host [attr]"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesHostBeforePseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host([attr])::before"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context([attr])"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesBeforeHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("[attr] :host-context(div)"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesBeforeHostContext2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures("[attr]:host-context(div)"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesAfterHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorNeverMatches,
            CollectFeatures(":host-context(*)[attr]"));
  ExpectUncommonAttributeRuleCount(0);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesAfterHostContext2) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context(*) [attr]"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, uncommonAttributeRulesAfterHostContextBeforePseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context([attr])::before"));
  ExpectUncommonAttributeRuleCount(1);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationDirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(1, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationMultipleDirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* + .a + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(2, "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest,
       universalSiblingInvalidationDirectAdjacentDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* + .a .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingDescendantInvalidation(1, "a", "b", invalidation_lists.siblings);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationIndirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* ~ .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(UINT_MAX, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest,
       universalSiblingInvalidationMultipleIndirectAdjacent) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* ~ .a ~ .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(UINT_MAX, "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest,
       universalSiblingInvalidationIndirectAdjacentDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* ~ .a .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingDescendantInvalidation(UINT_MAX, "a", "b",
                                      invalidation_lists.siblings);
  ExpectNoSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(1, "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("#x:not(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(1, "b", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("#x:-webkit-any(.a) + .b"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationType) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(1, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationType) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div#x + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, universalSiblingInvalidationLink) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":link + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectSiblingInvalidation(1, "a", invalidation_lists.siblings);
  ExpectSelfInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nonUniversalSiblingInvalidationLink) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#x:link + .a"));

  InvalidationLists invalidation_lists;
  CollectUniversalSiblingInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
}

TEST_F(RuleFeatureSetTest, nthInvalidationUniversal) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n)"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectWholeSubtreeInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:nth-child(2n)"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationUniversalDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) *"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectWholeSubtreeInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationSibling) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) + .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationSiblingDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":nth-child(2n) + .a .b"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoInvalidation(invalidation_lists.siblings);
  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("b", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(:nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectWholeSubtreeInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationNotClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:not(:nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationNotDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".blah:not(:nth-child(2n)) .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationAny) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(#nomatch, :nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectWholeSubtreeInvalidation(invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationAnyClass) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".a:-webkit-any(#nomatch, :nth-child(2n))"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, nthInvalidationAnyDescendant) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".blah:-webkit-any(#nomatch, :nth-child(2n)) .a"));

  InvalidationLists invalidation_lists;
  CollectNthInvalidationSet(invalidation_lists);

  ExpectNoSelfInvalidation(invalidation_lists.descendants);
  ExpectClassInvalidation("a", invalidation_lists.descendants);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationTypeSelector) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("div"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("* div"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("body *"));
  ExpectFullRecalcForRuleSetInvalidation(true);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationClassIdAttr) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".c"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(".c *"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#i"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("#i *"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[attr]"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("[attr] *"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationHoverActiveFocus) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":hover:active:focus"));
  ExpectFullRecalcForRuleSetInvalidation(true);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationHostContext) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context(.x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":host-context(.x) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationHost) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host(.x)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host(*) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":host(.x) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationNot) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":not(.x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(.x) :hover"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures(":not(.x) .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":not(.x) + .y"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationCustomPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("::-webkit-slider-thumb"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x::-webkit-slider-thumb"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x + ::-webkit-slider-thumb"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationSlotted) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch, CollectFeatures("::slotted(*)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("::slotted(.y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x::slotted(.y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures("[x] ::slotted(.y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
}

TEST_F(RuleFeatureSetTest, RuleSetInvalidationAnyPseudo) {
  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(*, #x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(".x:-webkit-any(*, #y)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(:-webkit-any(.a, .b), #x)"));
  ExpectFullRecalcForRuleSetInvalidation(false);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(:-webkit-any(.a, *), #x)"));
  ExpectFullRecalcForRuleSetInvalidation(true);
  ClearFeatures();

  EXPECT_EQ(RuleFeatureSet::kSelectorMayMatch,
            CollectFeatures(":-webkit-any(*, .a) *"));
  ExpectFullRecalcForRuleSetInvalidation(true);
}

}  // namespace blink
