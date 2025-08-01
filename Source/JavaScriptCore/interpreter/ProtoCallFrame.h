/*
 * Copyright (C) 2013-2024 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "CodeBlock.h"
#include "Register.h"
#include "StackAlignment.h"
#include <wtf/ForbidHeapAllocation.h>

#if ENABLE(WEBASSEMBLY)
#include "JSWebAssemblyInstance.h"
#endif

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

struct JS_EXPORT_PRIVATE ProtoCallFrame {
    WTF_FORBID_HEAP_ALLOCATION;
public:
    // CodeBlock, Callee, ArgumentCount, and |this|.
    static constexpr unsigned numberOfRegisters { 4 };

    Register codeBlockValue;
    Register calleeValue;
    Register argCountAndCodeOriginValue;
    Register thisArg;
    uint32_t paddedArgCount;
    EncodedJSValue* args;
    JSGlobalObject* globalObject;

    inline void init(CodeBlock*, JSGlobalObject*, JSObject*, JSValue, int, EncodedJSValue* otherArgs = nullptr);

    inline CodeBlock* codeBlock() const;
    inline void setCodeBlock(CodeBlock*);

    inline JSObject* callee() const;
    inline void setCallee(JSObject*);
    void setGlobalObject(JSGlobalObject* object)
    {
        globalObject = object;
    }

    int argumentCountIncludingThis() const { return argCountAndCodeOriginValue.payload(); }
    int argumentCount() const { return argumentCountIncludingThis() - 1; }
    void setArgumentCountIncludingThis(int count) { argCountAndCodeOriginValue.payload() = count; }
    void setPaddedArgCount(uint32_t argCount) { paddedArgCount = argCount; }

    void clearCurrentVPC() { argCountAndCodeOriginValue.tag() = 0; }
    
    JSValue thisValue() const { return thisArg.Register::jsValue(); }
    void setThisValue(JSValue value) { thisArg = value; }

#if ENABLE(WEBASSEMBLY)
    void setWasmInstance(JSWebAssemblyInstance* instance)
    {
        codeBlockValue = instance;
    }
#endif

    JSValue argument(size_t argumentIndex)
    {
        ASSERT(static_cast<int>(argumentIndex) < argumentCount());
        return JSValue::decode(args[argumentIndex]);
    }
    void setArgument(size_t argumentIndex, JSValue value)
    {
        ASSERT(static_cast<int>(argumentIndex) < argumentCount());
        args[argumentIndex] = JSValue::encode(value);
    }
};

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
