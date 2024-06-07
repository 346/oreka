/*
 * Oreka -- A media capture and retrieval platform
 *
 * SRTFilter Plugin
 * Author Takahiro Mishiro
 *
 */
#pragma warning(disable: 4786) // disables truncated symbols in browse-info warning

#define _WINSOCKAPI_ // prevents the inclusion of winsock.h

#include "SRTFilter.h"
#include "SRTConfig.h"
#include <srt.h>
#include "uriparser.hpp"


#define G711_PACKET_INTERVAL 20

static log4cxx::LoggerPtr s_log = log4cxx::Logger::getLogger("plugin.srt");

static std::regex placeholderPattern("\\{([a-zA-Z0-9_]+)\\}");

SRTFilter::SRTFilter() : m_bufferQueueA(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL), m_bufferQueueB(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL) {
}

SRTFilter::~SRTFilter() {
        bool m_shouldStreamAllCalls;
    if (m_silentChannelBuffer != NULL){
        free(m_silentChannelBuffer);
    }

    // LOG4CXX_DEBUG(s_log, "LiveStream Instance Destroying");
}

FilterRef SRTFilter::Instanciate() {
    FilterRef Filter(new SRTFilter());
    return Filter;
}

void SRTFilter::AudioChunkIn(AudioChunkRef & inputAudioChunk) {
    m_outputAudioChunk = inputAudioChunk;

    if (inputAudioChunk.get() == NULL) {
        return;
    }

    if (inputAudioChunk->GetNumSamples() == 0) {
        return;
    }

    AudioChunkDetails inputDetails = * inputAudioChunk->GetDetails();
    char * newBuffer = (char * ) inputAudioChunk->m_pBuffer;

    if (m_isFirstPacket) {
        m_currentBufferChannel = inputDetails.m_channel;
        m_isFirstPacket = false;
        CStdString logMsg;
        logMsg.Format("SRTFilter:: HeadChannel: %d", m_currentBufferChannel);
        LOG4CXX_DEBUG(s_log, logMsg);
    }

    if (m_silentChannelBuffer == NULL){
        m_silentChannelBuffer = (char *)malloc(inputDetails.m_numBytes);
        if (!m_silentChannelBuffer) {
            CStdString logMsg;
            logMsg.Format("SRTFilter::AudioChunkIn [%s] SilentChannelBuffer Memory allocation failed.", m_orkRefId);
            LOG4CXX_ERROR(s_log, logMsg);
            return;
        }
        std::fill_n(m_silentChannelBuffer, inputDetails.m_numBytes, 255);
    }

    if (m_status) {
        char *bufferedChunk;
        char *newChunk;
        char *leftChunk;
        char *rightChunk;
        RingBuffer<AudioChunkRef> *currentBufferQueue;
        RingBuffer<AudioChunkRef> *standbyBufferQueue;
        if (m_useBufferA) {
            currentBufferQueue = &m_bufferQueueA;
            standbyBufferQueue = &m_bufferQueueB;
        } else {
            currentBufferQueue = &m_bufferQueueB;
            standbyBufferQueue = &m_bufferQueueA;
        }
        boost::optional<AudioChunkRef> queuedChunk;
        if (inputDetails.m_channel == m_currentBufferChannel) {
            if (queuedChunk = currentBufferQueue->put(inputAudioChunk)){
                newChunk = m_silentChannelBuffer;
                bufferedChunk = (char *)(*queuedChunk)->m_pBuffer;
            } else {
                return;
            }
        } else {
            if (queuedChunk = currentBufferQueue->get()){
                newChunk = newBuffer;
                bufferedChunk = (char *)(*queuedChunk)->m_pBuffer;
            } else {
                m_useBufferA = !m_useBufferA;
                m_currentBufferChannel = inputDetails.m_channel;

                if (queuedChunk = standbyBufferQueue->put(inputAudioChunk)) {
                    LOG4CXX_ERROR(s_log, "Standby buffer has chunk");
                    m_status = false;
                }
                return;
            }
        }
        if (m_currentBufferChannel == 1) {
            leftChunk = bufferedChunk;
            rightChunk = newChunk;
        } else {
            leftChunk = newChunk;
            rightChunk = bufferedChunk;
        }
        PushToRTMP(inputDetails, leftChunk, rightChunk);
    }
}

void SRTFilter::PushToRTMP(AudioChunkDetails& channelDetails, char * firstChannelBuffer, char * secondChannelBuffer) {
    CStdString logMsg;
    int size = channelDetails.m_numBytes * 2;
    m_timestamp += G711_PACKET_INTERVAL;
    char *outputBuffer = (char *)malloc(size);
    if (!outputBuffer) {
        logMsg.Format("SRTFilter::Send [%s] Memory allocation failed.", m_orkRefId);
        LOG4CXX_ERROR(s_log, logMsg);
        m_status = false;
        return;
    }

    for (int i = 0; i < channelDetails.m_numBytes; ++i)
    {
        outputBuffer[i * 2] = firstChannelBuffer[i];
        outputBuffer[i * 2 + 1] = secondChannelBuffer[i];
    }

    if (srt_sendmsg(m_srtsock, outputBuffer, size, -1, false) == SRT_ERROR)
    {
        logMsg.Format("SRTFilter::Send [%s] error:%s", m_orkRefId, srt_getlasterror_str());
        LOG4CXX_ERROR(s_log, logMsg);
        m_status = false;
        return;
    }
}

void SRTFilter::AudioChunkOut(AudioChunkRef & chunk) {
    chunk = m_outputAudioChunk;
}

AudioEncodingEnum SRTFilter::GetInputAudioEncoding() {
    return UnknownAudio;
}

AudioEncodingEnum SRTFilter::GetOutputAudioEncoding() {
    return UnknownAudio;
}

CStdString SRTFilter::GetName() {
    return "SRTFilter";
}

bool SRTFilter::SupportsInputRtpPayloadType(int rtpPayloadType) {
    //so that BatchProcessing doesn't pick this filter.
    return rtpPayloadType == pt_Unknown;
}

void SRTFilter::CaptureEventIn(CaptureEventRef & event) {
    //Start RTP Stream Open
    CStdString logMsg;
    auto key = event->EventTypeToString(event->m_type);

    if (event->m_type == CaptureEvent::EventTypeEnum::EtStart) {
        m_orkRefId = event->m_value;
    }

    logMsg.Format("SRTFilter:: CaptureEventIn[%s] Key: %s, Value: %s", m_orkRefId, key, event->m_value);
    LOG4CXX_DEBUG(s_log, logMsg);

    if (event->m_type == CaptureEvent::EventTypeEnum::EtCallId) {
        m_callId = event->m_value;
    }

    if (event->m_type == CaptureEvent::EventTypeEnum::EtOrkUid) {
        m_orkUid = event->m_value;
    }
    if (event->m_type == CaptureEvent::EventTypeEnum::EtDirection) {
        m_direction = event->m_value;
    }
    if (event->m_type == CaptureEvent::EventTypeEnum::EtLocalParty) {
        m_localParty = event->m_value;
    }
    if (event->m_type == CaptureEvent::EventTypeEnum::EtRemoteParty) {
        m_remoteParty = event->m_value;
    }
    if (event->m_type == CaptureEvent::EventTypeEnum::EtLocalIp) {
        m_localIp = event->m_value;
    }
    if (event->m_type == CaptureEvent::EventTypeEnum::EtRemoteIp) {
        m_remoteIp = event->m_value;
    }

    if (event->m_type == CaptureEvent::EventTypeEnum::EtCallId) {
        if (m_callId.empty()) {
            logMsg.Format("SRTFilter:: Start[%s] Failed for Empty Call ID", m_orkRefId);
            LOG4CXX_ERROR(s_log, logMsg);
            return;
        }
        if (m_orkUid.empty()) {
            logMsg.Format("SRTFilter:: Start[%s] Failed for Empty Ork UID", m_orkUid);
            LOG4CXX_ERROR(s_log, logMsg);
            return;
        }

        const uuid streamingUuid = random_generator()();
        auto liveStreamingId = boost::lexical_cast<std::string>(streamingUuid);
        std::string url = GetURL(liveStreamingId);
        logMsg.Format("SRTFilter:: Start[%s] Streaming URL %s", m_orkRefId, url.c_str());
        LOG4CXX_INFO(s_log, logMsg);

        UriParser u(url);

        srt_setloglevel(srt_logging::LogLevel::debug);

        struct addrinfo hints, *peer;

        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        m_srtsock = srt_create_socket();
        auto params = u.parameters();
        for (const auto& param : params) {
            const auto& key = param.first;
            const auto& value = param.second;
            LOG4CXX_DEBUG(s_log, key);
            LOG4CXX_DEBUG(s_log, value);
            if (key == "streamid") {
                if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_STREAMID, value.c_str(), value.length())) {
                    m_status = false;
                    logMsg.Format("[%s] streamid error %s", m_orkRefId, srt_getlasterror_str());
                    LOG4CXX_ERROR(s_log, logMsg);
                    return;
                }
            }

            if (key == "passphrase") {
                if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_PASSPHRASE, value.c_str(), value.length())) {
                    m_status = false;
                    logMsg.Format("[%s] passphrase error %s", m_orkRefId, srt_getlasterror_str());
                    LOG4CXX_ERROR(s_log, logMsg);
                    return;
                }
            }
        }

        if (0 != getaddrinfo(u.host().c_str(), u.port().c_str(), &hints, &peer))
        {
            logMsg.Format("[%s] incorrect server/peer address. %s:%s", m_orkRefId, u.host().c_str(), u.port().c_str());
            LOG4CXX_INFO(s_log, logMsg);
            m_status = false;
            return;
        }

        // Connect to the server, implicit bind.
        if (SRT_ERROR == srt_connect(m_srtsock, peer->ai_addr, peer->ai_addrlen))
        {
            int rej = srt_getrejectreason(m_srtsock);
            logMsg.Format("[%s] %s:%s", m_orkRefId, srt_getlasterror_str(), srt_rejectreason_str(rej));
            LOG4CXX_INFO(s_log, logMsg);
            m_status = false;
            return;
        }

        freeaddrinfo(peer);

        m_status = true;
    }

    if (event->m_type == CaptureEvent::EventTypeEnum::EtStop) {
        m_status = false;
        SRT_TRACEBSTATS perf;
        if (SRT_SUCCESS == srt_bstats(m_srtsock, &perf, true)) {
            logMsg.Format(
                "[%s] msTimeStamp: %lld, pktSentTotal: %lld, pktSentUniqueTotal: %lld, pktSndLossTotal: %d, pktRetransTotal: %d, pktRecvACKTotal: %d, pktRecvNAKTotal: %d, usSndDurationTotal: %lld, pktSndDropTotal: %d, pktSndFilterExtraTotal: %d, byteSentTotal: %llu, byteSentUniqueTotal: %llu, byteRetransTotal: %llu, byteSndDropTotal: %llu",
                m_orkRefId,
                perf.msTimeStamp,
                perf.pktSentTotal,
                perf.pktSentUniqueTotal,
                perf.pktSndLossTotal,
                perf.pktRetransTotal,
                perf.pktRecvACKTotal,
                perf.pktRecvNAKTotal,
                perf.usSndDurationTotal,
                perf.pktSndDropTotal,
                perf.pktSndFilterExtraTotal,
                perf.byteSentTotal,
                perf.byteSentUniqueTotal,
                perf.byteRetransTotal,
                perf.byteSndDropTotal);
            LOG4CXX_INFO(s_log, logMsg);
        } else {
            logMsg.Format("[%s] error srt_bstats", m_orkRefId);
            LOG4CXX_ERROR(s_log, logMsg);
        }
        if (SRT_ERROR == srt_close(m_srtsock)) {
            logMsg.Format("[%s] %s", m_orkRefId, srt_getlasterror_str());
            LOG4CXX_INFO(s_log, logMsg);
        }
    }
}

void SRTFilter::CaptureEventOut(CaptureEventRef & event) {
    //LOG4CXX_INFO(s_log, "LiveStream CaptureEventOut " + toString(event.get()));
}

void SRTFilter::SetSessionInfo(CStdString & trackingId) {
    LOG4CXX_INFO(s_log, "SRTFilter SetSessionInfo " + trackingId);
}

std::string SRTFilter::GetURL(std::string liveStreamingId) {
    std::string result = SRTCONFIG.m_srtServerEndpoint;
    std::map<std::string, std::string> values = {
        {"streamid", liveStreamingId},
        {"orkuid", m_orkUid},
        {"nativecallid", m_callId},
        {"localparty", m_localParty},
        {"remoteparty", m_remoteParty},
        {"remoteip", m_remoteIp},
        {"localip", m_localIp},
        {"direction", m_direction}
    };

    std::smatch matches;
    while (std::regex_search(result, matches, placeholderPattern)) {
        auto it = values.find(matches[1].str());
        if (it != values.end()) {
            result.replace(matches.position(0), matches.length(0), it->second);
        }
    }

    return result;
}

// =================================================================

extern "C"
{
    DLL_EXPORT void __CDECL__ OrkInitialize()
    {
        LOG4CXX_INFO(s_log, "SRTFilter starting");

        //SRTConfig
        ConfigManager::Instance()->AddConfigureFunction(SRTConfig::Configure);

        FilterRef filter(new SRTFilter());
        FilterRegistry::instance()->RegisterFilter(filter);

        LOG4CXX_INFO(s_log, "SRTFilter initialized");
    }

}
