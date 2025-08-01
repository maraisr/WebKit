/*
 * Copyright (C) 2023 Igalia S.L
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
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "AudioEncoderGStreamer.h"

#if ENABLE(WEB_CODECS) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include "GStreamerRegistryScanner.h"
#include "PlatformRawAudioDataGStreamer.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/WorkQueue.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GStreamerAudioEncoder);

GST_DEBUG_CATEGORY(webkit_audio_encoder_debug);
#define GST_CAT_DEFAULT webkit_audio_encoder_debug

static WorkQueue& gstEncoderWorkQueue()
{
    static std::once_flag onceKey;
    static LazyNeverDestroyed<Ref<WorkQueue>> queue;
    std::call_once(onceKey, [] {
        queue.construct(WorkQueue::create("GStreamer AudioEncoder queue"_s));
    });
    return queue.get();
}

class GStreamerInternalAudioEncoder : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<GStreamerInternalAudioEncoder, WTF::DestructionThread::Main> {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(GStreamerInternalAudioEncoder);
    WTF_MAKE_NONCOPYABLE(GStreamerInternalAudioEncoder);

public:
    static Ref<GStreamerInternalAudioEncoder> create(AudioEncoder::DescriptionCallback&& descriptionCallback, AudioEncoder::OutputCallback&& outputCallback, GRefPtr<GstElement>&& element) { return adoptRef(*new GStreamerInternalAudioEncoder(WTFMove(descriptionCallback), WTFMove(outputCallback), WTFMove(element))); }
    ~GStreamerInternalAudioEncoder();

    String initialize(const String& codecName, const AudioEncoder::Config&);
    bool encode(AudioEncoder::RawFrame&&);
    void flush();
    void close() { m_isClosed = true; }

    const RefPtr<GStreamerElementHarness> harness() const { return m_harness; }
    bool isClosed() const { return m_isClosed; }

private:
    GStreamerInternalAudioEncoder(AudioEncoder::DescriptionCallback&&, AudioEncoder::OutputCallback&&, GRefPtr<GstElement>&&);

    AudioEncoder::DescriptionCallback m_descriptionCallback;
    AudioEncoder::OutputCallback m_outputCallback;
    int64_t m_timestamp { 0 };
    std::optional<uint64_t> m_duration;
    bool m_isClosed { false };
    RefPtr<GStreamerElementHarness> m_harness;
    GRefPtr<GstElement> m_encoder;
    GRefPtr<GstElement> m_outputCapsFilter;
    GRefPtr<GstCaps> m_outputCaps;
    GRefPtr<GstElement> m_inputCapsFilter;
    GRefPtr<GstCaps> m_inputCaps;
};

Ref<AudioEncoder::CreatePromise> GStreamerAudioEncoder::create(const String& codecName, const AudioEncoder::Config& config, DescriptionCallback&& descriptionCallback, OutputCallback&& outputCallback)
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_encoder_debug, "webkitaudioencoder", 0, "WebKit WebCodecs Audio Encoder");
    });

    GRefPtr<GstElement> element;
    if (codecName.startsWith("pcm-"_s)) {
        auto components = codecName.split('-');
        if (components.size() != 2)
            return CreatePromise::createAndReject(makeString("Invalid LPCM codec string: "_s, codecName));
        element = gst_element_factory_make("identity", nullptr);
    } else {
        auto& scanner = GStreamerRegistryScanner::singleton();
        auto lookupResult = scanner.isCodecSupported(GStreamerRegistryScanner::Configuration::Encoding, codecName);
        if (!lookupResult)
            return CreatePromise::createAndReject(makeString("No GStreamer encoder found for codec "_s, codecName));
        element = gst_element_factory_create(lookupResult.factory.get(), nullptr);
    }
    auto internalEncoder = GStreamerInternalAudioEncoder::create(WTFMove(descriptionCallback), WTFMove(outputCallback), WTFMove(element));
    auto error = internalEncoder->initialize(codecName, config);
    if (!error.isEmpty()) {
        GST_WARNING("Error creating encoder: %s", error.ascii().data());
        return CreatePromise::createAndReject(makeString("GStreamer encoding initialization failed with error: "_s, codecName));
    }
    auto encoder = adoptRef(*new GStreamerAudioEncoder(WTFMove(internalEncoder)));
    return CreatePromise::createAndResolve(WTFMove(encoder));
}

GStreamerAudioEncoder::GStreamerAudioEncoder(Ref<GStreamerInternalAudioEncoder>&& internalEncoder)
    : m_internalEncoder(WTFMove(internalEncoder))
{
}

GStreamerAudioEncoder::~GStreamerAudioEncoder()
{
    GST_DEBUG_OBJECT(m_internalEncoder->harness()->element(), "Destroying");
    close();
}

Ref<AudioEncoder::EncodePromise> GStreamerAudioEncoder::encode(RawFrame&& frame)
{
    return invokeAsync(gstEncoderWorkQueue(), [frame = WTFMove(frame), encoder = m_internalEncoder]() mutable {
        if (!encoder->encode(WTFMove(frame)))
            return EncodePromise::createAndReject("Encoding failed"_s);

        encoder->harness()->processOutputSamples();
        return EncodePromise::createAndResolve();
    });
}

Ref<GenericPromise> GStreamerAudioEncoder::flush()
{
    return invokeAsync(gstEncoderWorkQueue(), [encoder = m_internalEncoder] {
        encoder->flush();
        return GenericPromise::createAndResolve();
    });
}

void GStreamerAudioEncoder::reset()
{
    GST_DEBUG_OBJECT(m_internalEncoder->harness()->element(), "Resetting");
    m_internalEncoder->close();
}

void GStreamerAudioEncoder::close()
{
    GST_DEBUG_OBJECT(m_internalEncoder->harness()->element(), "Closing");
    m_internalEncoder->close();
}

GStreamerInternalAudioEncoder::GStreamerInternalAudioEncoder(AudioEncoder::DescriptionCallback&& descriptionCallback, AudioEncoder::OutputCallback&& outputCallback, GRefPtr<GstElement>&& encoderElement)
    : m_descriptionCallback(WTFMove(descriptionCallback))
    , m_outputCallback(WTFMove(outputCallback))
    , m_encoder(WTFMove(encoderElement))
{
    static Atomic<uint64_t> counter = 0;
    auto binName = makeString("audio-encoder-"_s, unsafeSpan(GST_OBJECT_NAME(m_encoder.get())), '-', counter.exchangeAdd(1));

    GRefPtr<GstElement> harnessedElement = gst_bin_new(binName.ascii().data());
    auto audioconvert = gst_element_factory_make("audioconvert", nullptr);
    auto audioresample = gst_element_factory_make("audioresample", nullptr);
    m_inputCapsFilter = gst_element_factory_make("capsfilter", nullptr);
    m_outputCapsFilter = gst_element_factory_make("capsfilter", nullptr);
    gst_bin_add_many(GST_BIN_CAST(harnessedElement.get()), audioconvert, audioresample, m_inputCapsFilter.get(), m_encoder.get(), m_outputCapsFilter.get(), nullptr);
    gst_element_link_many(audioconvert, audioresample, m_inputCapsFilter.get(), m_encoder.get(), m_outputCapsFilter.get(), nullptr);
    auto sinkPad = adoptGRef(gst_element_get_static_pad(audioconvert, "sink"));
    gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("sink", sinkPad.get()));
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_outputCapsFilter.get(), "src"));
    gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("src", srcPad.get()));

    auto pad = adoptGRef(gst_element_get_static_pad(m_encoder.get(), "src"));
    g_signal_connect_data(pad.get(), "notify::caps", G_CALLBACK(+[](GObject* pad, GParamSpec*, gpointer userData) {
        auto weakEncoder = static_cast<ThreadSafeWeakPtr<GStreamerInternalAudioEncoder>*>(userData);
        auto encoder = weakEncoder->get();
        if (!encoder)
            return;

        GRefPtr<GstCaps> caps;
        g_object_get(pad, "caps", &caps.outPtr(), nullptr);
        if (!caps)
            return;

        auto structure = gst_caps_get_structure(caps.get(), 0);
        GstBuffer* header = nullptr;
        if (auto streamHeader = gst_structure_get_value(structure, "streamheader")) {
            RELEASE_ASSERT(GST_VALUE_HOLDS_ARRAY(streamHeader));
            auto firstValue = gst_value_array_get_value(streamHeader, 0);
            RELEASE_ASSERT(GST_VALUE_HOLDS_BUFFER(firstValue));
            header = gst_value_get_buffer(firstValue);
        } else if (auto codecData = gst_structure_get_value(structure, "codec_data")) {
            RELEASE_ASSERT(GST_VALUE_HOLDS_BUFFER(codecData));
            header = gst_value_get_buffer(codecData);
        }

        AudioEncoder::ActiveConfiguration configuration;
        if (header) {
            GstMappedBuffer buffer(header, GST_MAP_READ);
            configuration.description = buffer.createVector();
        }
        configuration.numberOfChannels = gstStructureGet<int>(structure, "channels"_s);
        configuration.sampleRate = gstStructureGet<int>(structure, "rate"_s);
        encoder->m_descriptionCallback(WTFMove(configuration));
    }), new ThreadSafeWeakPtr { *this }, [](void* data, GClosure*) {
        delete static_cast<ThreadSafeWeakPtr<GStreamerInternalAudioEncoder>*>(data);
    }, static_cast<GConnectFlags>(0));

    m_harness = GStreamerElementHarness::create(WTFMove(harnessedElement), [weakThis = ThreadSafeWeakPtr { *this }, this](auto&, GRefPtr<GstSample>&& outputSample) {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;
        if (m_isClosed)
            return;

        auto caps = gst_sample_get_caps(outputSample.get());
        auto outputBuffer = gst_sample_get_buffer(outputSample.get());
        auto structure = gst_caps_get_structure(caps, 0);
        if (gst_structure_has_name(structure, "audio/x-opus") && gst_buffer_get_size(outputBuffer) < 2) {
            GST_INFO_OBJECT(m_encoder.get(), "DTX opus packet detected, ignoring it");
            return;
        }

        static std::once_flag onceFlag;
        std::call_once(onceFlag, [this] {
            m_harness->dumpGraph("audio-encoder"_s);
        });

        bool isKeyFrame = !GST_BUFFER_FLAG_IS_SET(outputBuffer, GST_BUFFER_FLAG_DELTA_UNIT);
        GST_TRACE_OBJECT(m_harness->element(), "Notifying encoded%s frame", isKeyFrame ? " key" : "");
        GstMappedBuffer mappedBuffer(outputBuffer, GST_MAP_READ);
        AudioEncoder::EncodedFrame encodedFrame { mappedBuffer.createVector(), isKeyFrame, m_timestamp, m_duration };
        m_outputCallback({ WTFMove(encodedFrame) });
    });
}

GStreamerInternalAudioEncoder::~GStreamerInternalAudioEncoder()
{
    if (!m_harness)
        return;

    auto pad = adoptGRef(gst_element_get_static_pad(m_harness->element(), "src"));
    g_signal_handlers_disconnect_by_data(pad.get(), this);
}

String GStreamerInternalAudioEncoder::initialize(const String& codecName, const AudioEncoder::Config& config)
{
    GST_DEBUG_OBJECT(m_harness->element(), "Initializing encoder for codec %s", codecName.ascii().data());

    GUniquePtr<char> name(gst_element_get_name(m_encoder.get()));
    auto nameView = StringView::fromLatin1(name.get());
    if (codecName.startsWith("mp4a"_s)) {
        const char* streamFormat = config.isAacADTS.value_or(false) ? "adts" : "raw";
        m_outputCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 4, "stream-format", G_TYPE_STRING, streamFormat, nullptr));
        if (gstObjectHasProperty(m_encoder.get(), "bitrate"_s) && config.bitRate && config.bitRate < std::numeric_limits<int>::max())
            g_object_set(m_encoder.get(), "bitrate", static_cast<int>(config.bitRate), nullptr);
    } else if (codecName == "mp3"_s) {
        if (gstObjectHasProperty(m_encoder.get(), "cbr"_s)) {
            switch (config.bitRateMode) {
            case BitrateMode::Constant:
                g_object_set(m_encoder.get(), "cbr", TRUE, nullptr);
                break;
            case BitrateMode::Variable:
                g_object_set(m_encoder.get(), "cbr", FALSE, nullptr);
                break;
            };
        }
        m_outputCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, nullptr));
    } else if (codecName == "opus"_s && nameView.startsWith("opusenc"_s)) {
        if (config.bitRate && config.bitRate < std::numeric_limits<int>::max()) {
            if (config.bitRate >= 4000 && config.bitRate <= 650000)
                g_object_set(m_encoder.get(), "bitrate", static_cast<int>(config.bitRate), nullptr);
            else
                return makeString("Opus bitrate out of range: "_s, config.bitRate, "not in [4000, 650000]"_s);
        }

        if (config.numberOfChannels > 255)
            return makeString("Too many audio channels requested from Opus config, the maximum allowed is 255."_s);

        switch (config.bitRateMode) {
        case BitrateMode::Constant:
            gst_util_set_object_arg(G_OBJECT(m_encoder.get()), "bitrate-type", "cbr");
            break;
        case BitrateMode::Variable:
            gst_util_set_object_arg(G_OBJECT(m_encoder.get()), "bitrate-type", "vbr");
            break;
        };

        if (auto parameters = config.opusConfig) {
            g_object_set(m_encoder.get(), "packet-loss-percentage", parameters->packetlossperc, "inband-fec", parameters->useinbandfec, "dtx", parameters->usedtx, nullptr);

            if (parameters->complexity)
                g_object_set(m_encoder.get(), "complexity", static_cast<int>(*parameters->complexity), nullptr);

            // The frame-size property is expressed in milli-seconds, the value in parameters is
            // expressed in micro-seconds.
            auto frameSize = makeString(parameters->frameDuration / 1000);
            gst_util_set_object_arg(G_OBJECT(m_encoder.get()), "frame-size", frameSize.ascii().data());
        }
        int channelMappingFamily = config.numberOfChannels <= 2 ? 0 : 1;
        m_outputCaps = adoptGRef(gst_caps_new_simple("audio/x-opus", "channel-mapping-family", G_TYPE_INT, channelMappingFamily, nullptr));
    } else if (codecName == "alaw"_s)
        m_outputCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-alaw"));
    else if (codecName == "ulaw"_s)
        m_outputCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-mulaw"));
    else if (codecName == "flac"_s) {
        m_outputCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-flac"));
        if (auto parameters = config.flacConfig) {
            if (nameView.startsWith("flacenc"_s))
                g_object_set(m_encoder.get(), "blocksize", static_cast<unsigned>(parameters->blockSize), "quality", parameters->compressLevel, nullptr);
        }
    } else if (codecName == "vorbis"_s) {
        m_outputCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-vorbis"));
        if (config.bitRate && config.bitRate <= 25000)
            g_object_set(m_encoder.get(), "bitrate", static_cast<int>(config.bitRate), nullptr);
    } else if (codecName.startsWith("pcm-"_s)) {
        auto components = codecName.split('-');
        auto pcmFormat = components[1].convertToASCIILowercase();
        GstAudioFormat gstPcmFormat = GST_AUDIO_FORMAT_UNKNOWN;
        if (pcmFormat == "u8"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_U8;
        else if (pcmFormat == "s16"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_S16;
        else if (pcmFormat == "s24"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_S24;
        else if (pcmFormat == "s32"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_S32;
        else if (pcmFormat == "f32"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_F32;
        else
            return makeString("Invalid LPCM codec format: "_s, pcmFormat);

        m_outputCaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, gst_audio_format_to_string(gstPcmFormat),
            "layout", G_TYPE_STRING, "interleaved", nullptr));
    } else
        return makeString("Unsupported audio codec: "_s, codecName);

    // Do not force sample rate, some tests in
    // imported/w3c/web-platform-tests/webcodecs/audio-encoder.https.any.html make use of values
    // that would not be accepted by the Opus encoder. So we instead let caps negotiation figure out
    // the most suitable value.
    m_inputCaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, config.numberOfChannels, nullptr));

    g_object_set(m_inputCapsFilter.get(), "caps", m_inputCaps.get(), nullptr);
    g_object_set(m_outputCapsFilter.get(), "caps", m_outputCaps.get(), nullptr);
    return emptyString();
}

bool GStreamerInternalAudioEncoder::encode(AudioEncoder::RawFrame&& rawFrame)
{
    m_timestamp = rawFrame.timestamp;
    m_duration = rawFrame.duration;

    auto gstAudioFrame = downcast<PlatformRawAudioDataGStreamer>(rawFrame.frame.get());
    return m_harness->pushSample(GRefPtr(gstAudioFrame->sample()));
}

void GStreamerInternalAudioEncoder::flush()
{
    m_harness->flush();
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_CODECS) && USE(GSTREAMER)
