/*
 * Oreka -- A media capture and retrieval platform
 *
 * SRTFilter Plugin
 * Author Takahiro Mishiro
 *
 */

#ifndef __SRT_H__
#define __SRT_H__ 1

#include "LogManager.h"
#include "Filter.h"
#include <log4cxx/logger.h>
// #include "Utils.h"
#include <deque>
#include "AudioCapture.h"
#include <iostream>
#include <string>
#include <cstring>
#include "ConfigManager.h"
#include "../LiveStream/RingBuffer.h"
#include "srt.h"
#include "uriparser.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>

using namespace boost::uuids;

class DLL_IMPORT_EXPORT_ORKBASE SRTFilter : public Filter {
    public:
        SRTFilter();
        ~SRTFilter();

        FilterRef __CDECL__ Instanciate();
        void __CDECL__ AudioChunkIn(AudioChunkRef &chunk);
        void __CDECL__ AudioChunkOut(AudioChunkRef &chunk);
        AudioEncodingEnum __CDECL__ GetInputAudioEncoding();
        AudioEncodingEnum __CDECL__ GetOutputAudioEncoding();
        CStdString __CDECL__ GetName();
        bool __CDECL__ SupportsInputRtpPayloadType(int rtpm_payloadType);
        void __CDECL__ CaptureEventIn(CaptureEventRef &event);
        void __CDECL__ CaptureEventOut(CaptureEventRef &event);
        void __CDECL__ SetSessionInfo(CStdString &trackingId);

    private:
        AudioChunkRef m_outputAudioChunk;
        bool m_initialized;
        CStdString m_callId;
        CStdString m_orkRefId;
        CStdString m_orkUid;
        CStdString m_localParty;
        CStdString m_localIp;
        CStdString m_remoteParty;
        CStdString m_remoteIp;
        CStdString m_direction;
        SRTSOCKET m_srtsock;
        bool m_status = false;
        bool m_isFirstPacket = true;
        int m_currentBufferChannel;
        u_int32_t m_timestamp = 0;
        RingBuffer<AudioChunkRef> m_bufferQueueA;
        RingBuffer<AudioChunkRef> m_bufferQueueB;
        bool m_useBufferA = true;
        char * m_silentChannelBuffer = NULL;
        void PushToRTMP(AudioChunkDetails& channelDetails, char* firstChannelBuffer, char* secondChannelBuffer);
        std::string GetURL(std::string liveStreamingId);
};

#endif
