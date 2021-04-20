/*
 * Oreka -- A media capture and retrieval platform
 *
 */
#pragma warning(disable : 4786) // disables truncated symbols in browse-info warning

#define _WINSOCKAPI_ // prevents the inclusion of winsock.h

#include "LiveStreamFilter.h"

static log4cxx::LoggerPtr s_log = log4cxx::Logger::getLogger("plugin.livestream");

LiveStreamFilter::LiveStreamFilter()
{
	LOG4CXX_INFO(s_log, "LiveStream New Instance Created");
}

LiveStreamFilter::~LiveStreamFilter()
{
	LOG4CXX_INFO(s_log, "LiveStream Instance Destroying");
}

FilterRef LiveStreamFilter::Instanciate()
{
	FilterRef Filter(new LiveStreamFilter());
	return Filter;
}

void LiveStreamFilter::AudioChunkIn(AudioChunkRef &inputAudioChunk)
{
	m_outputAudioChunk = inputAudioChunk;
	// int16_t pcmdata[BUFFER_SAMPLES];
	CStdString logMsg;

	if (inputAudioChunk.get() == NULL)
	{
		return;
	}

	if (inputAudioChunk->GetNumSamples() == 0)
	{
		return;
	}

	AudioChunkDetails outputDetails = *inputAudioChunk->GetDetails();
	char *inputBuffer = (char *)inputAudioChunk->m_pBuffer;
	int size = outputDetails.m_numBytes * 2;

	//logMsg.Format("LiveStreamFilter AudioChunkIn Size: %d, Encoding: %s , RTP payload type: %s",size ,toString(outputDetails.m_encoding) , RtpPayloadTypeEnumToString(outputDetails.m_rtpPayloadType));
	//LOG4CXX_INFO(s_log, logMsg);

	// @param sound_format Format of SoundData. The following values are defined:
	// 0 = Linear PCM, platform endian
	// 1 = ADPCM
	// 2 = MP3
	// 3 = Linear PCM, little endian
	// 4 = Nellymoser 16 kHz mono
	// 5 = Nellymoser 8 kHz mono
	// 6 = Nellymoser
	// 7 = G.711 A-law logarithmic PCM
	// 8 = G.711 mu-law logarithmic PCM
	// 9 = reserved
	// 10 = AAC
	// 11 = Speex
	// 14 = MP3 8 kHz
	// 15 = Device-specific sound
	// Formats 7, 8, 14, and 15 are reserved.
	// AAC is supported in Flash Player 9,0,115,0 and higher.
	// Speex is supported in Flash Player 10 and higher.

	char sound_format = 9;
	if (outputDetails.m_rtpPayloadType == pt_PCMU)
		sound_format = 8;
	else if (outputDetails.m_rtpPayloadType == pt_PCMA)
		sound_format = 7;

	// @param sound_rate Sampling rate. The following values are defined:
	// 0 = 5.5 kHz
	// 1 = 11 kHz
	// 2 = 22 kHz
	// 3 = 44 kHz
	char sound_rate = 3;

	// @param sound_size Size of each audio sample. This parameter only pertains to
	// uncompressed formats. Compressed formats always decode
	// to 16 bits internally.
	// 0 = 8-bit samples
	// 1 = 16-bit samples
	char sound_size = 1;

	// @param sound_type Mono or stereo sound
	// 0 = Mono sound
	// 1 = Stereo sound
	//char sound_type = outputDetails.m_channel == 0 ? 0 : 1;
	char sound_type = 1;

	timestamp += 160; //Timestamp increment = clock frequency/frame rate
					  //160 byte payload of G.711 has a packetization interval of 20 ms
					  //For 1 second, there will be 1000ms / 20ms = 50 frames
					  //Audio RTP packet timestamp incremental value = 8kHz / 50 = 8000Hz / 50 = 160

	if (isFirstPacket)
	{
		headChannel = outputDetails.m_channel;
		isFirstPacket = false;
	}

	if (outputDetails.m_channel == headChannel && status)
	{
		bufferQueue.push(inputBuffer);
	}

	if (rtmp != NULL && status)
	{
		if (outputDetails.m_channel != headChannel && bufferQueue.size() > 0)
		{
			char *outputBuffer = (char *)malloc(size);
			char *tempBuffer = bufferQueue.front();
			bufferQueue.pop();

			for (int i = 0; i < 160; ++i)
			{
				outputBuffer[i * 2] = tempBuffer[i];
				outputBuffer[i * 2 + 1] = inputBuffer[i];
			}

			if (srs_audio_write_raw_frame(rtmp, sound_format, sound_rate, sound_size, sound_type, outputBuffer, size, timestamp) != 0)
			{
				srs_human_trace("send audio raw data failed.");
				return;
			}
			CStdString logMsg;
			logMsg.Format("LiveStreamFilter::sent packet: type=%s, time=%d, size=%d, codec=%d, rate=%d, sample=%d, channel=%d nativecallId=%s",
						  srs_human_flv_tag_type2string(SRS_RTMP_TYPE_AUDIO), timestamp, size, sound_format, sound_rate, sound_size, sound_type, m_callId);
			LOG4CXX_DEBUG(s_log, logMsg);
		}
	}
}

void LiveStreamFilter::AudioChunkOut(AudioChunkRef &chunk)
{
	chunk = m_outputAudioChunk;
}

AudioEncodingEnum LiveStreamFilter::GetInputAudioEncoding()
{
	return UnknownAudio;
}

AudioEncodingEnum LiveStreamFilter::GetOutputAudioEncoding()
{
	return UnknownAudio;
}

CStdString LiveStreamFilter::GetName()
{
	return "LiveStreamFilter";
}

bool LiveStreamFilter::SupportsInputRtpPayloadType(int rtpPayloadType)
{
	//so that BatchProcessing doesn't pick this filter.
	return rtpPayloadType == pt_Unknown;
}

void LiveStreamFilter::CaptureEventIn(CaptureEventRef &event)
{
	//Start RTP Stream Open
	auto key = event->EventTypeToString(event->m_type);
	LOG4CXX_INFO(s_log, "LiveStream CaptureEventIn " + key + " : " + event->m_value);
	if (event->m_type == CaptureEvent::EventTypeEnum::EtCallId)
	{
		m_callId = event->m_value;
	}

	if (event->m_type == CaptureEvent::EventTypeEnum::EtKeyValue && event->m_key == "LiveStream" && event->m_value == "start")
	{
		std::string url = "rtmp://" + CONFIG.m_rtmpServerEndpoint + ":" + CONFIG.m_rtmpServerPort + "/live/" + m_callId;

		LOG4CXX_INFO(s_log, "LiveStream URL : " + url);
		//open rstp stream
		rtmp = srs_rtmp_create(url.c_str());
		if (srs_rtmp_handshake(rtmp) != 0)
		{
			srs_human_trace("simple handshake failed.");
			return;
		}
		srs_human_trace("simple handshake success");

		if (srs_rtmp_connect_app(rtmp) != 0)
		{
			srs_human_trace("connect vhost/app failed.");
			return;
		}
		srs_human_trace("connect vhost/app success");

		if (srs_rtmp_publish_stream(rtmp) != 0)
		{
			srs_human_trace("publish stream failed.");
			return;
		}
		srs_human_trace("publish stream success");

		status = true;
		LiveStreamSessionsSingleton::instance()->AddToStreamCallList(m_callId);
	}

	if (event->m_type == CaptureEvent::EventTypeEnum::EtStop)
	{
		//close rstp stream
		status = false;
		LiveStreamSessionsSingleton::instance()->RemoveFromStreamCallList(m_callId);
		if (rtmp != NULL)
		{
			srs_human_trace("stream detroying...");
			srs_rtmp_destroy(rtmp);
		}
	}

	if (event->m_type == CaptureEvent::EventTypeEnum::EtKeyValue && event->m_key == "LiveStream" && event->m_value == "stop")
	{
		LiveStreamSessionsSingleton::instance()->RemoveFromStreamCallList(m_callId);
		status = false;
	}
}

void LiveStreamFilter::CaptureEventOut(CaptureEventRef &event)
{
	//LOG4CXX_INFO(s_log, "LiveStream CaptureEventOut " + toString(event.get()));
}

void LiveStreamFilter::SetSessionInfo(CStdString &trackingId)
{
	LOG4CXX_INFO(s_log, "LiveStream SetSessionInfo " + trackingId);
}

// =================================================================

extern "C"
{
	DLL_EXPORT void __CDECL__ OrkInitialize()
	{
		LOG4CXX_INFO(s_log, "LiveStream  filter starting");
		FilterRef filter(new LiveStreamFilter());
		FilterRegistry::instance()->RegisterFilter(filter);
		LOG4CXX_INFO(s_log, "LiveStream  filter initialized");
		LiveStreamServer *liveStreamServer = new LiveStreamServer(CONFIG.m_liveStreamingServerPort);
		liveStreamServer->Run();
	}
}