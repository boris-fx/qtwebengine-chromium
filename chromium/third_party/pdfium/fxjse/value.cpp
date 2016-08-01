// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fxjse/include/cfxjse_value.h"

#include <math.h>

#include "fxjse/context.h"
#include "fxjse/include/cfxjse_class.h"

namespace {

double FXJSE_ftod(FX_FLOAT fNumber) {
  static_assert(sizeof(FX_FLOAT) == 4, "FX_FLOAT of incorrect size");

  uint32_t nFloatBits = (uint32_t&)fNumber;
  uint8_t nExponent = (uint8_t)(nFloatBits >> 23);
  if (nExponent == 0 || nExponent == 255)
    return fNumber;

  int8_t nErrExp = nExponent - 150;
  if (nErrExp >= 0)
    return fNumber;

  double dwError = pow(2.0, nErrExp), dwErrorHalf = dwError / 2;
  double dNumber = fNumber, dNumberAbs = fabs(fNumber);
  double dNumberAbsMin = dNumberAbs - dwErrorHalf,
         dNumberAbsMax = dNumberAbs + dwErrorHalf;
  int32_t iErrPos = 0;
  if (floor(dNumberAbsMin) == floor(dNumberAbsMax)) {
    dNumberAbsMin = fmod(dNumberAbsMin, 1.0);
    dNumberAbsMax = fmod(dNumberAbsMax, 1.0);
    int32_t iErrPosMin = 1, iErrPosMax = 38;
    do {
      int32_t iMid = (iErrPosMin + iErrPosMax) / 2;
      double dPow = pow(10.0, iMid);
      if (floor(dNumberAbsMin * dPow) == floor(dNumberAbsMax * dPow)) {
        iErrPosMin = iMid + 1;
      } else {
        iErrPosMax = iMid;
      }
    } while (iErrPosMin < iErrPosMax);
    iErrPos = iErrPosMax;
  }
  double dPow = pow(10.0, iErrPos);
  return fNumber < 0 ? ceil(dNumber * dPow - 0.5) / dPow
                     : floor(dNumber * dPow + 0.5) / dPow;
}

}  // namespace

void FXJSE_ThrowMessage(const CFX_ByteStringC& utf8Message) {
  v8::Isolate* pIsolate = v8::Isolate::GetCurrent();
  ASSERT(pIsolate);

  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(pIsolate);
  v8::Local<v8::String> hMessage = v8::String::NewFromUtf8(
      pIsolate, utf8Message.c_str(), v8::String::kNormalString,
      utf8Message.GetLength());
  v8::Local<v8::Value> hError = v8::Exception::Error(hMessage);
  pIsolate->ThrowException(hError);
}

CFXJSE_Value::CFXJSE_Value(v8::Isolate* pIsolate) : m_pIsolate(pIsolate) {}

CFXJSE_Value::~CFXJSE_Value() {}

CFXJSE_HostObject* CFXJSE_Value::ToHostObject(CFXJSE_Class* lpClass) const {
  ASSERT(!m_hValue.IsEmpty());

  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> pValue = v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  ASSERT(!pValue.IsEmpty());

  if (!pValue->IsObject())
    return nullptr;

  return FXJSE_RetrieveObjectBinding(pValue.As<v8::Object>(), lpClass);
}

void CFXJSE_Value::SetObject(CFXJSE_HostObject* lpObject,
                             CFXJSE_Class* pClass) {
  if (!pClass) {
    ASSERT(!lpObject);
    SetJSObject();
    return;
  }
  SetHostObject(lpObject, pClass);
}

void CFXJSE_Value::SetHostObject(CFXJSE_HostObject* lpObject,
                                 CFXJSE_Class* lpClass) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  ASSERT(lpClass);
  v8::Local<v8::FunctionTemplate> hClass =
      v8::Local<v8::FunctionTemplate>::New(m_pIsolate, lpClass->m_hTemplate);
  v8::Local<v8::Object> hObject = hClass->InstanceTemplate()->NewInstance();
  FXJSE_UpdateObjectBinding(hObject, lpObject);
  m_hValue.Reset(m_pIsolate, hObject);
}

void CFXJSE_Value::SetArray(uint32_t uValueCount, CFXJSE_Value** rgValues) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Array> hArrayObject = v8::Array::New(m_pIsolate, uValueCount);
  if (rgValues) {
    for (uint32_t i = 0; i < uValueCount; i++) {
      if (rgValues[i]) {
        hArrayObject->Set(i, v8::Local<v8::Value>::New(
                                 m_pIsolate, rgValues[i]->DirectGetValue()));
      }
    }
  }
  m_hValue.Reset(m_pIsolate, hArrayObject);
}

void CFXJSE_Value::SetDate(double dDouble) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hDate = v8::Date::New(m_pIsolate, dDouble);
  m_hValue.Reset(m_pIsolate, hDate);
}

void CFXJSE_Value::SetFloat(FX_FLOAT fFloat) {
  CFXJSE_ScopeUtil_IsolateHandle scope(m_pIsolate);
  v8::Local<v8::Value> pValue = v8::Number::New(m_pIsolate, FXJSE_ftod(fFloat));
  m_hValue.Reset(m_pIsolate, pValue);
}

FX_BOOL CFXJSE_Value::SetObjectProperty(const CFX_ByteStringC& szPropName,
                                        CFXJSE_Value* lpPropValue) {
  ASSERT(lpPropValue);
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  v8::Local<v8::Value> hPropValue =
      v8::Local<v8::Value>::New(m_pIsolate, lpPropValue->DirectGetValue());
  return (FX_BOOL)hObject.As<v8::Object>()->Set(
      v8::String::NewFromUtf8(m_pIsolate, szPropName.c_str(),
                              v8::String::kNormalString,
                              szPropName.GetLength()),
      hPropValue);
}

FX_BOOL CFXJSE_Value::GetObjectProperty(const CFX_ByteStringC& szPropName,
                                        CFXJSE_Value* lpPropValue) {
  ASSERT(lpPropValue);
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  v8::Local<v8::Value> hPropValue =
      hObject.As<v8::Object>()->Get(v8::String::NewFromUtf8(
          m_pIsolate, szPropName.c_str(), v8::String::kNormalString,
          szPropName.GetLength()));
  lpPropValue->ForceSetValue(hPropValue);
  return TRUE;
}

FX_BOOL CFXJSE_Value::SetObjectProperty(uint32_t uPropIdx,
                                        CFXJSE_Value* lpPropValue) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  v8::Local<v8::Value> hPropValue =
      v8::Local<v8::Value>::New(m_pIsolate, lpPropValue->DirectGetValue());
  return (FX_BOOL)hObject.As<v8::Object>()->Set(uPropIdx, hPropValue);
}

FX_BOOL CFXJSE_Value::GetObjectPropertyByIdx(uint32_t uPropIdx,
                                             CFXJSE_Value* lpPropValue) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  v8::Local<v8::Value> hPropValue = hObject.As<v8::Object>()->Get(uPropIdx);
  lpPropValue->ForceSetValue(hPropValue);
  return TRUE;
}

FX_BOOL CFXJSE_Value::DeleteObjectProperty(const CFX_ByteStringC& szPropName) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  hObject.As<v8::Object>()->Delete(v8::String::NewFromUtf8(
      m_pIsolate, szPropName.c_str(), v8::String::kNormalString,
      szPropName.GetLength()));
  return TRUE;
}

FX_BOOL CFXJSE_Value::HasObjectOwnProperty(const CFX_ByteStringC& szPropName,
                                           FX_BOOL bUseTypeGetter) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  v8::Local<v8::String> hKey = v8::String::NewFromUtf8(
      m_pIsolate, szPropName.c_str(), v8::String::kNormalString,
      szPropName.GetLength());
  return hObject.As<v8::Object>()->HasRealNamedProperty(hKey) ||
         (bUseTypeGetter &&
          hObject.As<v8::Object>()
              ->HasOwnProperty(m_pIsolate->GetCurrentContext(), hKey)
              .FromMaybe(false));
}

FX_BOOL CFXJSE_Value::SetObjectOwnProperty(const CFX_ByteStringC& szPropName,
                                           CFXJSE_Value* lpPropValue) {
  ASSERT(lpPropValue);
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hObject =
      v8::Local<v8::Value>::New(m_pIsolate, m_hValue);
  if (!hObject->IsObject())
    return FALSE;

  v8::Local<v8::Value> pValue =
      v8::Local<v8::Value>::New(m_pIsolate, lpPropValue->m_hValue);
  return hObject.As<v8::Object>()
      ->DefineOwnProperty(
          m_pIsolate->GetCurrentContext(),
          v8::String::NewFromUtf8(m_pIsolate, szPropName.c_str(),
                                  v8::String::kNormalString,
                                  szPropName.GetLength()),
          pValue)
      .FromMaybe(false);
}

FX_BOOL CFXJSE_Value::SetFunctionBind(CFXJSE_Value* lpOldFunction,
                                      CFXJSE_Value* lpNewThis) {
  ASSERT(lpOldFunction && lpNewThis);

  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> rgArgs[2];
  v8::Local<v8::Value> hOldFunction =
      v8::Local<v8::Value>::New(m_pIsolate, lpOldFunction->DirectGetValue());
  if (hOldFunction.IsEmpty() || !hOldFunction->IsFunction())
    return FALSE;

  rgArgs[0] = hOldFunction;
  v8::Local<v8::Value> hNewThis =
      v8::Local<v8::Value>::New(m_pIsolate, lpNewThis->DirectGetValue());
  if (hNewThis.IsEmpty())
    return FALSE;

  rgArgs[1] = hNewThis;
  v8::Local<v8::String> hBinderFuncSource =
      v8::String::NewFromUtf8(m_pIsolate,
                              "(function (oldfunction, newthis) { return "
                              "oldfunction.bind(newthis); })");
  v8::Local<v8::Function> hBinderFunc =
      v8::Script::Compile(hBinderFuncSource)->Run().As<v8::Function>();
  v8::Local<v8::Value> hBoundFunction =
      hBinderFunc->Call(m_pIsolate->GetCurrentContext()->Global(), 2, rgArgs);
  if (hBoundFunction.IsEmpty() || !hBoundFunction->IsFunction())
    return FALSE;

  m_hValue.Reset(m_pIsolate, hBoundFunction);
  return TRUE;
}

#define FXJSE_INVALID_PTR ((void*)(intptr_t)-1)
FX_BOOL CFXJSE_Value::Call(CFXJSE_Value* lpReceiver,
                           CFXJSE_Value* lpRetValue,
                           uint32_t nArgCount,
                           CFXJSE_Value** lpArgs) {
  CFXJSE_ScopeUtil_IsolateHandleRootContext scope(m_pIsolate);
  v8::Local<v8::Value> hFunctionValue =
      v8::Local<v8::Value>::New(m_pIsolate, DirectGetValue());
  v8::Local<v8::Object> hFunctionObject =
      !hFunctionValue.IsEmpty() && hFunctionValue->IsObject()
          ? hFunctionValue.As<v8::Object>()
          : v8::Local<v8::Object>();

  v8::TryCatch trycatch(m_pIsolate);
  if (hFunctionObject.IsEmpty() || !hFunctionObject->IsCallable()) {
    if (lpRetValue)
      lpRetValue->ForceSetValue(FXJSE_CreateReturnValue(m_pIsolate, trycatch));
    return FALSE;
  }

  v8::Local<v8::Value> hReturnValue;
  v8::Local<v8::Value>* lpLocalArgs = NULL;
  if (nArgCount) {
    lpLocalArgs = FX_Alloc(v8::Local<v8::Value>, nArgCount);
    for (uint32_t i = 0; i < nArgCount; i++) {
      new (lpLocalArgs + i) v8::Local<v8::Value>;
      CFXJSE_Value* lpArg = lpArgs[i];
      if (lpArg) {
        lpLocalArgs[i] =
            v8::Local<v8::Value>::New(m_pIsolate, lpArg->DirectGetValue());
      }
      if (lpLocalArgs[i].IsEmpty()) {
        lpLocalArgs[i] = v8::Undefined(m_pIsolate);
      }
    }
  }

  FX_BOOL bRetValue = TRUE;
  if (lpReceiver == FXJSE_INVALID_PTR) {
    v8::MaybeLocal<v8::Value> maybe_retvalue =
        hFunctionObject->CallAsConstructor(m_pIsolate->GetCurrentContext(),
                                           nArgCount, lpLocalArgs);
    hReturnValue = maybe_retvalue.FromMaybe(v8::Local<v8::Value>());
  } else {
    v8::Local<v8::Value> hReceiver;
    if (lpReceiver) {
      hReceiver =
          v8::Local<v8::Value>::New(m_pIsolate, lpReceiver->DirectGetValue());
    }
    if (hReceiver.IsEmpty() || !hReceiver->IsObject())
      hReceiver = v8::Object::New(m_pIsolate);

    v8::MaybeLocal<v8::Value> maybe_retvalue = hFunctionObject->CallAsFunction(
        m_pIsolate->GetCurrentContext(), hReceiver, nArgCount, lpLocalArgs);
    hReturnValue = maybe_retvalue.FromMaybe(v8::Local<v8::Value>());
  }

  if (trycatch.HasCaught()) {
    hReturnValue = FXJSE_CreateReturnValue(m_pIsolate, trycatch);
    bRetValue = FALSE;
  }

  if (lpRetValue)
    lpRetValue->ForceSetValue(hReturnValue);

  if (lpLocalArgs) {
    for (uint32_t i = 0; i < nArgCount; i++)
      lpLocalArgs[i].~Local();
    FX_Free(lpLocalArgs);
  }
  return bRetValue;
}
