// Copyright 2010-2012, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dictionary/dictionary_impl.h"

#include <string>
#include "base/base.h"
#include "base/util.h"
#include "config/config.pb.h"
#include "config/config_handler.h"
#include "converter/node.h"
#include "dictionary/pos_matcher.h"
#include "dictionary/suppression_dictionary.h"
#include "dictionary/system/system_dictionary.h"
#include "dictionary/system/value_dictionary.h"
#include "dictionary/user_dictionary.h"

namespace mozc {

DictionaryImpl::DictionaryImpl(const char *dic_data, int dic_data_size)
    : suppression_dictionary_(
        SuppressionDictionary::GetSuppressionDictionary()),
      system_dictionary_(SystemDictionary::CreateSystemDictionaryFromImage(
          dic_data, dic_data_size)),
      value_dictionary_(ValueDictionary::CreateValueDictionaryFromImage(
          dic_data, dic_data_size)) {
  CHECK(suppression_dictionary_ != NULL);
  CHECK(system_dictionary_.get() != NULL);
  CHECK(value_dictionary_.get() != NULL);

  dics_.push_back(system_dictionary_.get());
  dics_.push_back(value_dictionary_.get());
  dics_.push_back(UserDictionary::GetUserDictionary());
}

DictionaryImpl::~DictionaryImpl() {
  dics_.clear();
}

Node *DictionaryImpl::LookupPredictiveWithLimit(
    const char *str, int size,
    const Limit &limit,
    NodeAllocatorInterface *allocator) const {
  return LookupInternal(str, size, PREDICTIVE, limit, allocator);
}

Node *DictionaryImpl::LookupPredictive(
    const char *str, int size,
    NodeAllocatorInterface *allocator) const {
  return LookupInternal(str, size, PREDICTIVE, Limit(), allocator);
}

Node *DictionaryImpl::LookupPrefixWithLimit(
    const char *str, int size,
    const Limit &limit,
    NodeAllocatorInterface *allocator) const {
  return LookupInternal(str, size, PREFIX, limit, allocator);
}

Node *DictionaryImpl::LookupPrefix(
    const char *str, int size,
    NodeAllocatorInterface *allocator) const {
  return LookupInternal(str, size, PREFIX, Limit(), allocator);
}

Node *DictionaryImpl::LookupReverse(const char *str, int size,
                                    NodeAllocatorInterface *allocator) const {
  return LookupInternal(str, size, REVERSE, Limit(), allocator);
}

bool DictionaryImpl::Reload() {
  return UserDictionary::GetUserDictionary()->Reload();
}

void DictionaryImpl::PopulateReverseLookupCache(
    const char *str, int size, NodeAllocatorInterface *allocator) const {
  for (size_t i = 0; i < dics_.size(); ++i) {
    dics_[i]->PopulateReverseLookupCache(str, size, allocator);
  }
}

void DictionaryImpl::ClearReverseLookupCache(
    NodeAllocatorInterface *allocator) const {
  for (size_t i = 0; i < dics_.size(); ++i) {
    dics_[i]->ClearReverseLookupCache(allocator);
  }
}

Node *DictionaryImpl::MaybeRemoveSpecialNodes(Node *node) const {
  const bool use_spelling_correction = GET_CONFIG(use_spelling_correction);
  const bool use_zip_code_conversion = GET_CONFIG(use_zip_code_conversion);
  const bool use_t13n_conversion = GET_CONFIG(use_t13n_conversion);

  if (use_spelling_correction && use_zip_code_conversion &&
      use_t13n_conversion) {
    return node;
  }

  Node *head = NULL;
  Node *prev = NULL;

  for (Node *n = node; n != NULL; n = n->bnext) {
    // Skip the filtering logic if the node is generated by user dictionary.
    if (!(n->attributes & Node::USER_DICTIONARY)) {
      if (!use_spelling_correction &&
          n->attributes & Node::SPELLING_CORRECTION) {
        continue;
      }

      if (!use_zip_code_conversion && POSMatcher::IsZipcode(n->lid)) {
        continue;
      }

      if (!use_t13n_conversion && Util::IsEnglishTransliteration(n->value)) {
        continue;
      }
    }

    if (head == NULL) {
      head = n;
    }
    if (prev != NULL) {
      prev->bnext = n;
    }
    prev = n;
  }

  if (prev != NULL) {
    prev->bnext = NULL;
  }

  return head;
}

Node *DictionaryImpl::LookupInternal(const char *str, int size,
                                     LookupType type,
                                     const Limit &limit,
                                     NodeAllocatorInterface *allocator) const {
  Node *head = NULL;
  for (size_t i = 0; i < dics_.size(); ++i) {
    Node *nodes = NULL;
    switch (type) {
      case PREDICTIVE:
        nodes = dics_[i]->LookupPredictiveWithLimit(
            str, size, limit, allocator);
        break;
      case PREFIX:
        nodes = dics_[i]->LookupPrefixWithLimit(str, size, limit, allocator);
        break;
      case REVERSE:
        nodes = dics_[i]->LookupReverse(str, size, allocator);
        break;
    }
    if (head == NULL) {
      head = nodes;
    } else if (nodes != NULL) {
      Node *last = NULL;
      // TODO(taku)  this is O(n^2) algorithm. can be fixed.
      for (last = nodes; last->bnext != NULL; last = last->bnext) {}
      last->bnext = head;
      head = nodes;
    }
  }

  head = MaybeRemoveSpecialNodes(head);
  head = suppression_dictionary_->SuppressNodes(head);

  return head;
}

}  // namespace mozc
