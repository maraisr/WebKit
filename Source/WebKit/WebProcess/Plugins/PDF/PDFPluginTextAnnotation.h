/*
 * Copyright (C) 2012-2023 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(PDF_PLUGIN)

#include "PDFPluginAnnotation.h"

#include <wtf/text/WTFString.h>

namespace WebCore {
class Element;
}

namespace WebKit {

class PDFPluginTextAnnotation : public PDFPluginAnnotation {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(PDFPluginTextAnnotation);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(PDFPluginTextAnnotation);
public:
    static Ref<PDFPluginTextAnnotation> create(PDFAnnotation *, PDFPluginBase*);
    virtual ~PDFPluginTextAnnotation();

    void updateGeometry() override;
    void commit() override;

    void setValue(const String&);

protected:
    PDFPluginTextAnnotation(PDFAnnotation *annotation, PDFPluginBase* plugin)
        : PDFPluginAnnotation(annotation, plugin)
    {
    }

    Ref<WebCore::Element> createAnnotationElement() override;
    String value() const;

private:
    bool handleEvent(WebCore::Event&) override;
};

} // namespace WebKit

#endif // ENABLE(PDF_PLUGIN)
