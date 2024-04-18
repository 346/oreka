/*
 * Oreka -- A media capture and retrieval platform
 *
 * LiveStreamFilter Plugin
 * Author Kinshuk Bairagi
 *
 */

#ifndef __LIVESTREAM_H__
#define __LIVESTREAM_H__ 1

#include "LogManager.h"
#include "Filter.h"
#include <log4cxx/logger.h>
#include "Utils.h"
#include "srs_librtmp.h"
#include <deque>
#include "LiveStreamSession.h"
#include "AudioCapture.h"
#include <iostream>
#include <string>
#include <cstring>
#include "ConfigManager.h"
#include "LiveStreamServer.h"
#include "RingBuffer.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>

using namespace boost::uuids;

class DLL_IMPORT_EXPORT_ORKBASE LiveStreamFilter : public Filter {
    public:
        LiveStreamFilter();
        ~LiveStreamFilter();

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
        bool status = false;
        bool isFirstPacket = true;
        int currentBufferChannel;
        srs_rtmp_t rtmp = NULL;
        u_int32_t timestamp = 0;
        RingBuffer<AudioChunkRef> bufferQueueA;
        RingBuffer<AudioChunkRef> bufferQueueB;
        bool useBufferA = true;
        bool shouldStreamAllCalls;
        char * silentChannelBuffer = NULL;
        void PushToRTMP(AudioChunkDetails& channelDetails, char* firstChannelBuffer, char* secondChannelBuffer);
};

#endif
