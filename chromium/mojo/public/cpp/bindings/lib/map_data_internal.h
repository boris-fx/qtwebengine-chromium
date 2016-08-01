// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_MAP_DATA_INTERNAL_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_MAP_DATA_INTERNAL_H_

#include "mojo/public/cpp/bindings/lib/array_internal.h"
#include "mojo/public/cpp/bindings/lib/validate_params.h"
#include "mojo/public/cpp/bindings/lib/validation_errors.h"
#include "mojo/public/cpp/bindings/lib/validation_util.h"

namespace mojo {
namespace internal {

// Map serializes into a struct which has two arrays as struct fields, the keys
// and the values.
template <typename Key, typename Value>
class Map_Data {
 public:
  static Map_Data* New(Buffer* buf) {
    return new (buf->Allocate(sizeof(Map_Data))) Map_Data();
  }

  // |validate_params| must have non-null |key_validate_params| and
  // |element_validate_params| members.
  static bool Validate(const void* data,
                       ValidationContext* validation_context,
                       const ContainerValidateParams* validate_params) {
    if (!data)
      return true;

    if (!ValidateStructHeaderAndClaimMemory(data, validation_context))
      return false;

    const Map_Data* object = static_cast<const Map_Data*>(data);
    if (object->header_.num_bytes != sizeof(Map_Data) ||
        object->header_.version != 0) {
      ReportValidationError(validation_context,
                            VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER);
      return false;
    }

    if (!ValidateEncodedPointer(&object->keys.offset)) {
      ReportValidationError(validation_context,
                            VALIDATION_ERROR_ILLEGAL_POINTER);
      return false;
    }
    if (!object->keys.offset) {
      ReportValidationError(validation_context,
                            VALIDATION_ERROR_UNEXPECTED_NULL_POINTER,
                            "null key array in map struct");
      return false;
    }
    if (!Array_Data<Key>::Validate(DecodePointerRaw(&object->keys.offset),
                                   validation_context,
                                   validate_params->key_validate_params)) {
      return false;
    }

    if (!ValidateEncodedPointer(&object->values.offset)) {
      ReportValidationError(validation_context,
                            VALIDATION_ERROR_ILLEGAL_POINTER);
      return false;
    }
    if (!object->values.offset) {
      ReportValidationError(validation_context,
                            VALIDATION_ERROR_UNEXPECTED_NULL_POINTER,
                            "null value array in map struct");
      return false;
    }
    if (!Array_Data<Value>::Validate(
            DecodePointerRaw(&object->values.offset),
            validation_context, validate_params->element_validate_params)) {
      return false;
    }

    const ArrayHeader* key_header =
        static_cast<const ArrayHeader*>(DecodePointerRaw(&object->keys.offset));
    const ArrayHeader* value_header = static_cast<const ArrayHeader*>(
        DecodePointerRaw(&object->values.offset));
    if (key_header->num_elements != value_header->num_elements) {
      ReportValidationError(validation_context,
                            VALIDATION_ERROR_DIFFERENT_SIZED_ARRAYS_IN_MAP);
      return false;
    }

    return true;
  }

  StructHeader header_;

  Pointer<Array_Data<Key>> keys;
  Pointer<Array_Data<Value>> values;

  void EncodePointers() {
    Encode(&keys);
    Encode(&values);
  }

  void DecodePointers() {
    Decode(&keys);
    Decode(&values);
  }

 private:
  Map_Data() {
    header_.num_bytes = sizeof(*this);
    header_.version = 0;
  }
  ~Map_Data() = delete;
};
static_assert(sizeof(Map_Data<char, char>) == 24, "Bad sizeof(Map_Data)");

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_MAP_DATA_INTERNAL_H_
