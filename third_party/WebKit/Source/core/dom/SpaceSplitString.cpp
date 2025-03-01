/*
 * Copyright (C) 2007 David Smith (catfish.man@gmail.com)
 * Copyright (C) 2007, 2008, 2011, 2012 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "core/dom/SpaceSplitString.h"

#include "core/html/parser/HTMLParserIdioms.h"
#include "platform/wtf/HashSet.h"
#include "platform/wtf/text/AtomicStringHash.h"

namespace blink {

// https://dom.spec.whatwg.org/#concept-ordered-set-parser
template <typename CharacterType>
inline void SpaceSplitString::Data::CreateVector(
    const AtomicString& source,
    const CharacterType* characters,
    unsigned length) {
  DCHECK_EQ(0u, vector_.size());
  HashSet<AtomicString> token_set;
  unsigned start = 0;
  while (true) {
    while (start < length && IsHTMLSpace<CharacterType>(characters[start]))
      ++start;
    if (start >= length)
      break;
    unsigned end = start + 1;
    while (end < length && IsNotHTMLSpace<CharacterType>(characters[end]))
      ++end;

    if (start == 0 && end == length) {
      vector_.push_back(source);
      return;
    }

    AtomicString token(characters + start, end - start);
    // We skip adding |token| to |token_set| for the first token to reduce the
    // cost of HashSet<>::insert(), and adjust |token_set| when the second
    // unique token is found.
    if (vector_.size() == 0) {
      vector_.push_back(token);
    } else if (vector_.size() == 1) {
      if (vector_[0] != token) {
        token_set.insert(vector_[0]);
        token_set.insert(token);
        vector_.push_back(token);
      }
    } else if (token_set.insert(token).is_new_entry) {
      vector_.push_back(token);
    }

    start = end + 1;
  }
}

void SpaceSplitString::Data::CreateVector(const AtomicString& string) {
  unsigned length = string.length();

  if (string.Is8Bit()) {
    CreateVector(string, string.Characters8(), length);
    return;
  }

  CreateVector(string, string.Characters16(), length);
}

bool SpaceSplitString::Data::ContainsAll(Data& other) {
  if (this == &other)
    return true;

  size_t this_size = vector_.size();
  size_t other_size = other.vector_.size();
  for (size_t i = 0; i < other_size; ++i) {
    const AtomicString& name = other.vector_[i];
    size_t j;
    for (j = 0; j < this_size; ++j) {
      if (vector_[j] == name)
        break;
    }
    if (j == this_size)
      return false;
  }
  return true;
}

void SpaceSplitString::Data::Add(const AtomicString& string) {
  DCHECK(HasOneRef());
  DCHECK(!Contains(string));
  vector_.push_back(string);
}

void SpaceSplitString::Data::Remove(unsigned index) {
  DCHECK(HasOneRef());
  vector_.erase(index);
}

void SpaceSplitString::Add(const AtomicString& string) {
  if (Contains(string))
    return;
  EnsureUnique();
  if (data_)
    data_->Add(string);
  else
    data_ = Data::Create(string);
}

bool SpaceSplitString::Remove(const AtomicString& string) {
  if (!data_)
    return false;
  unsigned i = 0;
  bool changed = false;
  while (i < data_->size()) {
    if ((*data_)[i] == string) {
      if (!changed)
        EnsureUnique();
      data_->Remove(i);
      changed = true;
      continue;
    }
    ++i;
  }
  return changed;
}

void SpaceSplitString::Remove(size_t index) {
  DCHECK_LT(index, size());
  EnsureUnique();
  data_->Remove(index);
}

void SpaceSplitString::ReplaceAt(size_t index, const AtomicString& token) {
  DCHECK_LT(index, size());
  EnsureUnique();
  (*data_)[index] = token;
}

SpaceSplitString::DataMap& SpaceSplitString::SharedDataMap() {
  DEFINE_STATIC_LOCAL(DataMap, map, ());
  return map;
}

void SpaceSplitString::Set(const AtomicString& input_string) {
  if (input_string.IsNull()) {
    Clear();
    return;
  }
  data_ = Data::Create(input_string);
}

SpaceSplitString::Data::~Data() {
  if (!key_string_.IsNull())
    SharedDataMap().erase(key_string_);
}

RefPtr<SpaceSplitString::Data> SpaceSplitString::Data::Create(
    const AtomicString& string) {
  Data*& data = SharedDataMap().insert(string, nullptr).stored_value->value;
  if (!data) {
    data = new Data(string);
    return AdoptRef(data);
  }
  return data;
}

RefPtr<SpaceSplitString::Data> SpaceSplitString::Data::CreateUnique(
    const Data& other) {
  return AdoptRef(new SpaceSplitString::Data(other));
}

SpaceSplitString::Data::Data(const AtomicString& string) : key_string_(string) {
  DCHECK(!string.IsNull());
  CreateVector(string);
}

SpaceSplitString::Data::Data(const SpaceSplitString::Data& other)
    : RefCounted<Data>(), vector_(other.vector_) {
  // Note that we don't copy m_keyString to indicate to the destructor that
  // there's nothing to be removed from the sharedDataMap().
}

}  // namespace blink
