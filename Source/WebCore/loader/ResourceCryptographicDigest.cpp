/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#include "config.h"
#include "ResourceCryptographicDigest.h"

#include "SharedBuffer.h"
#include <pal/crypto/CryptoDigest.h>
#include <wtf/text/Base64.h>
#include <wtf/text/ParsingUtilities.h>
#include <wtf/text/StringParsingBuffer.h>

namespace WebCore {

template<typename CharacterType> static std::optional<ResourceCryptographicDigest::Algorithm> parseHashAlgorithmAdvancingPosition(StringParsingBuffer<CharacterType>& buffer)
{
    // FIXME: This would be much cleaner with a lookup table of pairs of label / algorithm enum values, but I can't
    // figure out how to keep the labels compiletime strings for skipExactlyIgnoringASCIICase.

    if (skipExactlyIgnoringASCIICase(buffer, "sha256"_s))
        return ResourceCryptographicDigest::Algorithm::SHA256;
    if (skipExactlyIgnoringASCIICase(buffer, "sha384"_s))
        return ResourceCryptographicDigest::Algorithm::SHA384;
    if (skipExactlyIgnoringASCIICase(buffer, "sha512"_s))
        return ResourceCryptographicDigest::Algorithm::SHA512;

    return std::nullopt;
}

template<typename CharacterType> static std::optional<ResourceCryptographicDigest> parseCryptographicDigestImpl(StringParsingBuffer<CharacterType>& buffer)
{
    if (buffer.atEnd())
        return std::nullopt;

    auto algorithm = parseHashAlgorithmAdvancingPosition(buffer);
    if (!algorithm)
        return std::nullopt;

    if (!skipExactly(buffer, '-'))
        return std::nullopt;

    auto beginHashValue = buffer.span();
    skipWhile<isBase64OrBase64URLCharacter>(buffer);
    skipExactly(buffer, '=');
    skipExactly(buffer, '=');

    if (buffer.position() == beginHashValue.data())
        return std::nullopt;

    StringView hashValue(beginHashValue.first(buffer.position() - beginHashValue.data()));

    if (auto digest = base64Decode(hashValue))
        return ResourceCryptographicDigest { *algorithm, WTFMove(*digest) };

    if (auto digest = base64URLDecode(hashValue))
        return ResourceCryptographicDigest { *algorithm, WTFMove(*digest) };

    return std::nullopt;
}

std::optional<ResourceCryptographicDigest> parseCryptographicDigest(StringParsingBuffer<char16_t>& buffer)
{
    return parseCryptographicDigestImpl(buffer);
}

std::optional<ResourceCryptographicDigest> parseCryptographicDigest(StringParsingBuffer<LChar>& buffer)
{
    return parseCryptographicDigestImpl(buffer);
}

template<typename CharacterType> static std::optional<EncodedResourceCryptographicDigest> parseEncodedCryptographicDigestImpl(StringParsingBuffer<CharacterType>& buffer)
{
    if (buffer.atEnd())
        return std::nullopt;

    auto algorithm = parseHashAlgorithmAdvancingPosition(buffer);
    if (!algorithm)
        return std::nullopt;

    if (!skipExactly(buffer, '-'))
        return std::nullopt;

    auto beginHashValue = buffer.span();
    skipWhile<isBase64OrBase64URLCharacter>(buffer);
    skipExactly(buffer, '=');
    skipExactly(buffer, '=');

    if (buffer.position() == beginHashValue.data())
        return std::nullopt;

    return EncodedResourceCryptographicDigest { *algorithm, beginHashValue.first(buffer.position() - beginHashValue.data()) };
}

std::optional<EncodedResourceCryptographicDigest> parseEncodedCryptographicDigest(StringParsingBuffer<char16_t>& buffer)
{
    return parseEncodedCryptographicDigestImpl(buffer);
}

std::optional<EncodedResourceCryptographicDigest> parseEncodedCryptographicDigest(StringParsingBuffer<LChar>& buffer)
{
    return parseEncodedCryptographicDigestImpl(buffer);
}

std::optional<ResourceCryptographicDigest> decodeEncodedResourceCryptographicDigest(const EncodedResourceCryptographicDigest& encodedDigest)
{
    if (auto digest = base64Decode(encodedDigest.digest))
        return ResourceCryptographicDigest { encodedDigest.algorithm, WTFMove(*digest) };

    if (auto digest = base64URLDecode(encodedDigest.digest))
        return ResourceCryptographicDigest { encodedDigest.algorithm, WTFMove(*digest) };

    return std::nullopt;
}

static PAL::CryptoDigest::Algorithm toCryptoDigestAlgorithm(ResourceCryptographicDigest::Algorithm algorithm)
{
    switch (algorithm) {
    case ResourceCryptographicDigest::Algorithm::SHA256:
        return PAL::CryptoDigest::Algorithm::SHA_256;
    case ResourceCryptographicDigest::Algorithm::SHA384:
        return PAL::CryptoDigest::Algorithm::SHA_384;
    case ResourceCryptographicDigest::Algorithm::SHA512:
        return PAL::CryptoDigest::Algorithm::SHA_512;
    }
    ASSERT_NOT_REACHED();
    return PAL::CryptoDigest::Algorithm::SHA_512;
}

ResourceCryptographicDigest cryptographicDigestForBytes(ResourceCryptographicDigest::Algorithm algorithm, std::span<const uint8_t> bytes)
{
    auto cryptoDigest = PAL::CryptoDigest::create(toCryptoDigestAlgorithm(algorithm));
    cryptoDigest->addBytes(bytes);
    return { algorithm, cryptoDigest->computeHash() };
}

ResourceCryptographicDigest cryptographicDigestForSharedBuffer(ResourceCryptographicDigest::Algorithm algorithm, const FragmentedSharedBuffer* buffer)
{
    auto cryptoDigest = PAL::CryptoDigest::create(toCryptoDigestAlgorithm(algorithm));
    if (buffer) {
        buffer->forEachSegment([&](auto segment) {
            cryptoDigest->addBytes(segment);
        });
    }
    return { algorithm, cryptoDigest->computeHash() };
}

}
