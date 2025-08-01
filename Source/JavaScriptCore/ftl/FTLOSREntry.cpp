/*
 * Copyright (C) 2013-2022 Apple Inc. All rights reserved.
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

#include "config.h"
#include "FTLOSREntry.h"

#include "CallFrame.h"
#include "CodeBlock.h"
#include "DFGJITCode.h"
#include "FTLForOSREntryJITCode.h"
#include "JSCJSValueInlines.h"
#include "OperandsInlines.h"
#include "VMInlines.h"

#if ENABLE(FTL_JIT)

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC { namespace FTL {

SUPPRESS_ASAN
void* prepareOSREntry(
    VM& vm, CallFrame* callFrame, CodeBlock* dfgCodeBlock, CodeBlock* entryCodeBlock,
    BytecodeIndex bytecodeIndex, unsigned streamIndex)
{
    CodeBlock* baseline = dfgCodeBlock->baselineVersion();
    ExecutableBase* executable = dfgCodeBlock->ownerExecutable();
    DFG::JITCode* dfgCode = dfgCodeBlock->jitCode()->dfg();
    ForOSREntryJITCode* entryCode = entryCodeBlock->jitCode()->ftlForOSREntry();

    if (!entryCode->dfgCommon()->isStillValid()) {
        dfgCode->clearOSREntryBlockAndResetThresholds(dfgCodeBlock);
        return nullptr;
    }

    dataLogLnIf(Options::verboseOSR(),
        "FTL OSR from ", *dfgCodeBlock, " to ", *entryCodeBlock, " at ",
        bytecodeIndex);
    
    if (bytecodeIndex)
        jsCast<ScriptExecutable*>(executable)->setDidTryToEnterInLoop(true);

    if (bytecodeIndex != entryCode->bytecodeIndex()) {
        dataLogLnIf(Options::verboseOSR(), "    OSR failed because we don't have an entrypoint for ", bytecodeIndex, "; ours is for ", entryCode->bytecodeIndex());
        return nullptr;
    }
    
    Operands<std::optional<JSValue>> values;
    dfgCode->reconstruct(callFrame, dfgCodeBlock, CodeOrigin(bytecodeIndex), streamIndex, values);
    
    dataLogLnIf(Options::verboseOSR(), "    Values at entry: ", values);
    
    std::optional<JSValue> reconstructedThis;
    for (int argument = values.numberOfArguments(); argument--;) {
        JSValue valueOnStack = callFrame->r(virtualRegisterForArgumentIncludingThis(argument)).asanUnsafeJSValue();
        std::optional<JSValue> reconstructedValue = values.argument(argument);
        {
            JSValue valueToValidate = reconstructedValue ? *reconstructedValue : valueOnStack;
            auto flushFormat = entryCode->argumentFlushFormats()[argument];
            switch (flushFormat) {
            case DFG::FlushedInt32:
                if (!valueToValidate.isInt32())
                    return nullptr;
                break;
            case DFG::FlushedBoolean:
                if (!valueToValidate.isBoolean())
                    return nullptr;
                break;
            case DFG::FlushedCell:
                if (!valueToValidate.isCell())
                    return nullptr;
                break;
            case DFG::FlushedJSValue:
                break;
            default:
                dataLogLn("Unknown flush format for argument during FTL osr entry: ", flushFormat);
                RELEASE_ASSERT_NOT_REACHED();
                break;
            }
        }

        if (!argument) {
            // |this| argument can be unboxed. We should store boxed value instead for loop OSR entry since FTL assumes that all arguments are flushed JSValue.
            // To make this valid, we will modify the stack on the fly: replacing the value with boxed value.
            reconstructedThis = reconstructedValue;
            continue;
        }
        if (reconstructedValue && valueOnStack == reconstructedValue.value())
            continue;
        dataLogLn(
            "Mismatch between reconstructed values and the value on the stack for argument arg", argument, " for ", *entryCodeBlock, " at ", bytecodeIndex, ":\n",
            "    Value on stack: ", valueOnStack, "\n",
            "    Reconstructed value: ", reconstructedValue);
        RELEASE_ASSERT_NOT_REACHED();
    }
    
    RELEASE_ASSERT(values.numberOfLocals() == baseline->numCalleeLocals());
    
    EncodedJSValue* scratch = static_cast<EncodedJSValue*>(
        entryCode->entryBuffer()->dataBuffer());
    
    for (int local = values.numberOfLocals(); local--;) {
        std::optional<JSValue> value = values.local(local);
        if (value)
            scratch[local] = JSValue::encode(value.value());
        else
            scratch[local] = JSValue::encode(JSValue());
    }
    
    int stackFrameSize = entryCode->common.requiredRegisterCountForExecutionAndExit();
    if (!vm.ensureStackCapacityFor(&callFrame->registers()[virtualRegisterForLocal(stackFrameSize - 1).offset()])) [[unlikely]] {
        dataLogLnIf(Options::verboseOSR(), "    OSR failed because stack growth failed.");
        return nullptr;
    }
    
    callFrame->setCodeBlock(entryCodeBlock);
    
    void* result = entryCode->addressForCall(ArityCheckMode::ArityCheckNotRequired).taggedPtr();
    dataLogLnIf(Options::verboseOSR(), "    Entry will succeed, going to address ", RawPointer(result));

    // At this point, we're committed to triggering an OSR entry immediately after we return. Hence, it is safe to modify stack here.
    if (result) {
        if (reconstructedThis)
            callFrame->r(virtualRegisterForArgumentIncludingThis(0)) = JSValue::encode(reconstructedThis.value());
    }
    
    return result;
}

} } // namespace JSC::FTL

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(FTL_JIT)
