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
#include "srt.h"
#include "uriparser.hpp"
#include "boost/asio.hpp"
#include <random>
#include "LogManager.h"
#define G711_PACKET_INTERVAL 20

static auto s_log = log4cxx::Logger::getLogger("plugin.srt");

static std::regex placeholderPattern("\\{([a-zA-Z0-9_]+)\\}");
SRTFilter::SRTFilter() :
	m_bufferQueueA(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL),
	m_bufferQueueB(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL),
	m_rng(std::random_device()()) {

	m_srtUrl = string("srt://127.0.0.1:6000?" + SRTCONFIG.m_srtQuery);

	auto tracer = LOG.GetTracer("SRTFilter");

	m_span = tracer->StartSpan("start");

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

std::shared_ptr<opentelemetry::trace::Scope> SRTFilter::Scope() {
	return std::make_shared<trace_api::Scope>(m_span);
}

void SRTFilter::AudioChunkIn(AudioChunkRef & inputAudioChunk) {
	auto scope = trace_api::Scope(m_span);
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
		PushToSRT(inputDetails, leftChunk, rightChunk);
	}
}

void SRTFilter::PushToSRT(AudioChunkDetails& channelDetails, char * firstChannelBuffer, char * secondChannelBuffer) {
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

		context::Context empty;
		auto ctx = trace_api::SetSpan(empty, m_span);

		SrtTextMapCarrier carrier;
		auto prop = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
		prop->Inject(carrier, ctx);

		const uuid streamingUuid = random_generator()();
		auto liveStreamingId = boost::lexical_cast<std::string>(streamingUuid);
		for(auto carrierHeader : carrier.headers_) {
			LOG4CXX_DEBUG(s_log, carrierHeader.first);
			LOG4CXX_DEBUG(s_log, carrierHeader.second);
		}
		std::string url = GetURL(liveStreamingId, carrier.headers_);
		logMsg.Format("SRTFilter:: Start[%s] Streaming URL %s", m_orkRefId, url.c_str());
		LOG4CXX_DEBUG(s_log, logMsg);

		UriParser u(url);

		srt_setloglevel(srt_logging::LogLevel::debug);

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


		m_span->AddEvent("connect");
		std::vector<int> indices(SRTCONFIG.m_srtAddresses.size());
		std::iota(indices.begin(), indices.end(), 0);
		std::shuffle(indices.begin(), indices.end(), m_rng);
		auto connected = false;
		  for (const auto i : indices) {
			auto address = SRTCONFIG.m_srtAddresses[i];
			boost::asio::ip::address_v4 v4Address = address.to_v4();
			sockaddr_in addr_in;
			LOG4CXX_DEBUG(s_log, v4Address.to_string());
			memset(&addr_in, 0, sizeof(addr_in));
			addr_in.sin_family = AF_INET;
			addr_in.sin_port = htons(std::stoi(u.port()));
			inet_pton4(v4Address.to_string().c_str(), &addr_in.sin_addr);

			sockaddr* addr = (struct sockaddr*)&addr_in;

			if (SRT_ERROR == srt_connect(m_srtsock, addr, sizeof(addr_in))) {
				int rej = srt_getrejectreason(m_srtsock);
				logMsg.Format("[%s] %s:%s", m_orkRefId, srt_getlasterror_str(), srt_rejectreason_str(rej));
				LOG4CXX_INFO(s_log, logMsg);
				m_span->AddEvent("connect-failed", {
					{"address", v4Address.to_string()},
					{"reason", srt_getlasterror_str()},
					{"reject", srt_rejectreason_str(rej)},
				});
			} else {
				m_span->AddEvent("connected", {
					{"address", v4Address.to_string()},
				});
				connected = true;
				break;
			}
		}
		if (connected) {
			m_status = true;
		} else {
			logMsg.Format("[%s] error srt_connect", m_orkRefId);
			LOG4CXX_ERROR(s_log, logMsg);
			m_status = false;
			m_span->AddEvent("srtfilter-failed");
			return;
		}
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
			m_span->AddEvent("stats", {
				{"msTimeStamp", perf.msTimeStamp},
				{"pktSentTotal", perf.pktSentTotal},
				{"pktSentUniqueTotal", perf.pktSentUniqueTotal},
				{"pktSndLossTotal", perf.pktSndLossTotal},
				{"pktRetransTotal", perf.pktRetransTotal},
				{"pktRecvACKTotal", perf.pktRecvACKTotal},
				{"pktRecvNAKTotal", perf.pktRecvNAKTotal},
				{"usSndDurationTotal", perf.usSndDurationTotal},
				{"pktSndDropTotal", perf.pktSndDropTotal},
				{"pktSndFilterExtraTotal", perf.pktSndFilterExtraTotal},
				{"byteSentTotal", perf.byteSentTotal},
				{"byteSentUniqueTotal", perf.byteSentUniqueTotal},
				{"byteRetransTotal", perf.byteRetransTotal},
				{"byteSndDropTotal", perf.byteSndDropTotal},
			});
		} else {
			logMsg.Format("[%s] error srt_bstats", m_orkRefId);
			LOG4CXX_ERROR(s_log, logMsg);
			m_span->AddEvent("stats-failed");
		}
		if (SRT_ERROR == srt_close(m_srtsock)) {
			logMsg.Format("[%s] %s", m_orkRefId, srt_getlasterror_str());
			LOG4CXX_INFO(s_log, logMsg);
			m_span->AddEvent("close-failed");
		}
		m_span->End();
	}
}

void SRTFilter::CaptureEventOut(CaptureEventRef & event) {
	//LOG4CXX_INFO(s_log, "LiveStream CaptureEventOut " + toString(event.get()));
}

void SRTFilter::SetSessionInfo(CStdString & trackingId) {
	LOG4CXX_INFO(s_log, "SRTFilter SetSessionInfo " + trackingId);
}

std::string SRTFilter::GetURL(std::string liveStreamingId, std::map<std::string, std::string> &headers) {
	std::string result = m_srtUrl;
	std::string traceparent;
	std::map<std::string, std::string> values = {
		{"{streamid}", liveStreamingId},
		{"{orkuid}", m_orkUid},
		{"{nativecallid}", m_callId},
		{"{localparty}", m_localParty},
		{"{remoteparty}", m_remoteParty},
		{"{remoteip}", m_remoteIp},
		{"{localip}", m_localIp},
		{"{direction}", m_direction},
	};
	{
		auto header = headers.find("traceparent");
		std::string value;
		if (header != headers.end()) {
			value = header->second;
		} else {
			value = "0";
		}
		values.insert({"{traceparent}", value});
	}
	{
		auto header = headers.find("tracestate");
		std::string value;
		if (header != headers.end()) {
			value = header->second;
		} else {
			value = "0";
		}
		values.insert({"{tracestate}", value});
	}
	std::regex_token_iterator<std::string::iterator> rbegin(result.begin(), result.end(), placeholderPattern);
	std::regex_token_iterator<std::string::iterator> rend;
	std::for_each(rbegin, rend, [&result, &values](const std::string& target) {
		auto kv = values.find(target);
		if (kv != values.end()) {
			std::size_t pos = result.find(kv->first);
			if (pos != std::string::npos) {
				result.replace(pos, target.length(), kv->second);
			}
		}
	});
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
