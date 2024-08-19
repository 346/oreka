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

const char* SRT_DUMMY = "0";

static std::regex placeholderPattern("\\{([a-zA-Z0-9_\\-]+)\\}");
SRTFilter::SRTFilter(SimpleThreadPool &pool) :
	pool(pool),
	m_bufferQueueA(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL),
	m_bufferQueueB(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL),
	m_closeReceived(false),
	m_rng(std::random_device()()) {
	std::vector<int> indices(SRTCONFIG.m_srtAddresses.size());
	std::iota(indices.begin(), indices.end(), 0);
	std::shuffle(indices.begin(), indices.end(), m_rng);
	m_shuffledHostIndexes = indices;

	auto tracer = LOG.GetTracer("SRTFilter");

	m_span = tracer->StartSpan("SRTFilter");

}

SRTFilter::~SRTFilter() {
	bool m_shouldStreamAllCalls;
	if (m_silentChannelBuffer != NULL){
		free(m_silentChannelBuffer);
	}
}

FilterRef SRTFilter::Instanciate() {
	FilterRef Filter(new SRTFilter(pool));
	return Filter;
}

std::shared_ptr<opentelemetry::trace::Scope> SRTFilter::Scope() {
	return std::make_shared<trace_api::Scope>(m_span);
}

void SRTFilter::AudioChunkIn(AudioChunkRef & inputAudioChunk) {
	if (m_status == false) {
		return;
	}
	auto scope = trace_api::Scope(m_span);
	m_outputAudioChunk = inputAudioChunk;

	if (inputAudioChunk.get() == NULL) {
		return;
	}

	if (inputAudioChunk->GetNumSamples() == 0) {
		return;
	}
	m_stats.ReceivedPacket++;

	AudioChunkDetails inputDetails = * inputAudioChunk->GetDetails();
	char * newBuffer = (char * ) inputAudioChunk->m_pBuffer;

	if (m_isFirstPacket) {
		m_currentBufferChannel = inputDetails.m_channel;
		m_isFirstPacket = false;
		CStdString logMsg;
		logMsg.Format("[%s] HeadChannel: %d", m_orkRefId, m_currentBufferChannel);
		LOG4CXX_DEBUG(s_log, logMsg);
	}

	if (m_silentChannelBuffer == NULL){
		m_silentChannelBuffer = (char *)malloc(inputDetails.m_numBytes);
		if (!m_silentChannelBuffer) {
			CStdString logMsg;
			logMsg.Format("[%s] SilentChannelBuffer Memory allocation failed.", m_orkRefId);
			LOG4CXX_ERROR(s_log, logMsg);
			return;
		}
		std::fill_n(m_silentChannelBuffer, inputDetails.m_numBytes, 255);
	}
	if (inputDetails.m_channel == 1) {
		m_stats.ReceivedRightPacket++;
	} else {
		m_stats.ReceivedLeftPacket++;
	}
	char *bufferedChunk;
	char *newChunk;
	char *leftChunk;
	char *rightChunk;
	AudioBuffer *currentBufferQueue;
	AudioBuffer *standbyBufferQueue;
	if (m_useBufferA) {
		currentBufferQueue = &m_bufferQueueA;
		standbyBufferQueue = &m_bufferQueueB;
	} else {
		currentBufferQueue = &m_bufferQueueB;
		standbyBufferQueue = &m_bufferQueueA;
	}
	AudioChunkRef queuedChunk;
	if (inputDetails.m_channel == m_currentBufferChannel) {
		if (currentBufferQueue->full()) {
			queuedChunk = currentBufferQueue->front();
			currentBufferQueue->pop_front();
			currentBufferQueue->push_back(inputAudioChunk);
			newChunk = m_silentChannelBuffer;
			bufferedChunk = (char *)queuedChunk->m_pBuffer;
		} else {
			currentBufferQueue->push_back(inputAudioChunk);
			return;
		}
	} else {
		if (currentBufferQueue->empty()) {
			m_useBufferA = !m_useBufferA;
			m_currentBufferChannel = inputDetails.m_channel;
			standbyBufferQueue->push_back(inputAudioChunk);
			return;
		} else {
			queuedChunk = currentBufferQueue->front();
			currentBufferQueue->pop_front();
			newChunk = newBuffer;
			bufferedChunk = (char *)queuedChunk->m_pBuffer;
		}
	}
	if (m_currentBufferChannel == 1) {
		leftChunk = bufferedChunk;
		rightChunk = newChunk;
	} else {
		leftChunk = newChunk;
		rightChunk = bufferedChunk;
	}
	AddQueue(inputDetails, leftChunk, rightChunk);
}

void SRTFilter::AddQueue(AudioChunkDetails& channelDetails, char * firstChannelBuffer, char * secondChannelBuffer) {
	int size = channelDetails.m_numBytes * 2;
	char *outputBuffer = (char *)malloc(size);
	if (!outputBuffer) {
		CStdString logMsg;
		logMsg.Format("[%s] Memory allocation failed.", m_orkRefId);
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		m_status = false;
		return;
	}

	for (int i = 0; i < channelDetails.m_numBytes; ++i)
	{
		outputBuffer[i * 2] = firstChannelBuffer[i];
		outputBuffer[i * 2 + 1] = secondChannelBuffer[i];
	}
	auto chunk = SrtChunk{outputBuffer, size};
	if (!m_pushQueue.push(chunk)) {
		m_stats.FailedQueue++;
	}
}

bool SRTFilter::DequeueAndProcess(bool sendFraction) {
	while(true) {
		SrtChunk chunk;
		if (m_pushQueue.pop(chunk)) {
			m_chunkList.push_back(chunk);
			if (m_chunkList.size() == SRT_CHUNK_COUNT) {
				break;
			}
		} else {
			break;
		}
	}
	if (m_chunkList.size() == 0) {
		return false;
	}
	if (m_chunkList.size() == SRT_CHUNK_COUNT || sendFraction) {
		size_t allChunkSize = 0;
    for (const auto& chunk : m_chunkList) {
        allChunkSize += chunk.size;
    }
		char* allChunk = (char*)malloc(allChunkSize);
		char* currentPos = allChunk;
		for (const auto& chunk : m_chunkList) {
			memcpy(currentPos, chunk.buffer, chunk.size);
			currentPos += chunk.size;
			free(chunk.buffer);
		}
		m_chunkList.clear();
		PushToSRT(allChunk, allChunkSize);
		free(allChunk);
		return true;
	} else {
		return false;
	}
}

void SRTFilter::PushToSRT(char* outputBuffer, int size) {
	auto ret = srt_sendmsg2(m_srtsock, outputBuffer, size, NULL);
	if (ret == SRT_ERROR || ret != size) {
		CStdString logMsg;
		logMsg.Format("[%s] error srt_sendmsg2:%s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		m_status = false;
		return;
	}
	m_stats.SentPacket++;
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

	logMsg.Format("[%s] CaptureEventIn %s:%s:%s", m_orkRefId, key, event->m_value, event->m_key);
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
	if (event->m_type == CaptureEvent::EventTypeEnum::EtKeyValue) {
		m_extractedHeaders["{" + event->m_key + "}"] = event->m_value;
	}

	if (event->m_type == CaptureEvent::EventTypeEnum::EtEndMetadata) {
		if (m_callId.empty()) {
			logMsg.Format("[%s] Failed for Empty Call ID", m_orkRefId);
			LOG4CXX_ERROR(s_log, logMsg);
			m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
			return;
		}
		if (m_orkUid.empty()) {
			logMsg.Format("[%s] Failed for Empty Ork UID", m_orkUid);
			LOG4CXX_ERROR(s_log, logMsg);
			m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
			return;
		}

		boost::asio::io_context& ctx(pool.GetContext());
		boost::asio::spawn(ctx, [&](auto yield) {
			if (!Connect(yield)) {
				auto scope = Scope();
				CStdString logMsg;
				logMsg.Format("[%s] failed to connect", m_orkRefId);
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
				m_span->AddEvent("srtfilter-failed");
				m_span->End();
				return;
			}

			auto timer = std::make_shared<boost::asio::steady_timer>(ctx);
			while(true) {
				if (m_closeReceived.load()) {
					break;
				}
				DequeueAndProcess(false);
				timer->expires_after(std::chrono::milliseconds(SRT_CHUNK_MS));
				timer->async_wait(yield);
			}
			Close(yield);
		});
	}

	if (event->m_type == CaptureEvent::EventTypeEnum::EtStop) {
		m_closeReceived.store(true);
	}
}

void SRTFilter::CaptureEventOut(CaptureEventRef & event) {
	//LOG4CXX_INFO(s_log, "LiveStream CaptureEventOut " + toString(event.get()));
}

void SRTFilter::SetSessionInfo(CStdString & trackingId) {
	LOG4CXX_INFO(s_log, "SRTFilter SetSessionInfo " + trackingId);
}

std::string SRTFilter::GetURL(boost::asio::ip::address address, std::string liveStreamingId, std::map<std::string, std::string> &headers) {
	std::string result = string("srt://");
	result += address.to_string();
	result += string(":6000?");
	result += SRTCONFIG.m_srtQuery;
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

	values.insert(m_extractedHeaders.begin(), m_extractedHeaders.end());
	std::regex_token_iterator<std::string::iterator> rbegin(result.begin(), result.end(), placeholderPattern);
	std::regex_token_iterator<std::string::iterator> rend;
	std::for_each(rbegin, rend, [&result, &values](const std::string& target) {
		std::string replaceValue = "";
		auto kv = values.find(target);
		if (kv != values.end()) {
			replaceValue = kv->second;
		}
		std::size_t pos = result.find(target);
		if (pos != std::string::npos) {
			result.replace(pos, target.length(), replaceValue);
		}
	});
	return result;
}

bool SRTFilter::Connect(boost::asio::yield_context yield) {
	CStdString logMsg;

	context::Context empty;
	auto spanctx = trace_api::SetSpan(empty, m_span);

	SrtTextMapCarrier carrier;
	auto prop = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
	prop->Inject(carrier, spanctx);

	const uuid streamingUuid = random_generator()();
	auto liveStreamingId = boost::lexical_cast<std::string>(streamingUuid);
	for(auto carrierHeader : carrier.headers_) {
		LOG4CXX_DEBUG(s_log, carrierHeader.first);
		LOG4CXX_DEBUG(s_log, carrierHeader.second);
	}
	{
		auto scope = Scope();
		logMsg.Format("[%s] start connecting", m_orkRefId);
		LOG4CXX_INFO(s_log, logMsg);
	}
	m_span->AddEvent("connecting");
	for (const auto i : m_shuffledHostIndexes) {
		if (m_closeReceived.load()) {
			break;
		}
		auto address = SRTCONFIG.m_srtAddresses[i];
		std::string url = GetURL(address, liveStreamingId, carrier.headers_);
		logMsg.Format("[%s] url: %s", m_orkRefId, url.c_str());
		LOG4CXX_DEBUG(s_log, logMsg);
		UriParser u(url);

		if (!SetupSRTSocket(u)) {
			continue;
		}
		if (!TryConnect(yield, u)) {
			continue;
		}
		return true;
	}
	return false;
}
bool SRTFilter::TryConnect(boost::asio::yield_context yield, UriParser u) {
	CStdString logMsg;
	sockaddr_in addr_in;
	memset(&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons(std::stoi(u.port()));
	inet_pton4(u.host().c_str(), &addr_in.sin_addr);

	sockaddr* addr = (struct sockaddr*)&addr_in;

	if (SRT_ERROR == srt_connect(m_srtsock, addr, sizeof(addr_in))) {
		auto scope = Scope();
		int rej = srt_getrejectreason(m_srtsock);
		logMsg.Format("[%s] error srt_connect: %s:%s", m_orkRefId, srt_getlasterror_str(), srt_rejectreason_str(rej));
		LOG4CXX_INFO(s_log, logMsg);
		m_span->AddEvent("connect-failed", {
			{"address", u.hostport()},
			{"reason", srt_getlasterror_str()},
			{"reject", srt_rejectreason_str(rej)},
		});
		return false;
	}
	auto timer = std::make_shared<boost::asio::steady_timer>(boost::asio::get_associated_executor(yield));
	int count = 0;
	bool brokenLast = false;
	while(true) {
		count++;
		timer->expires_after(std::chrono::milliseconds(30));
		timer->async_wait(yield);
		SRT_SOCKSTATUS state = srt_getsockstate(m_srtsock);
		if (state == SRTS_CONNECTED) {
			auto scope = Scope();
			logMsg.Format("[%s] connected", m_orkRefId);
			LOG4CXX_INFO(s_log, logMsg);
			m_span->AddEvent("connected", {
				{"address", u.hostport()},
			});
			return true;
		}
		if (state == SRTS_BROKEN) {
			auto scope = Scope();
			if (brokenLast) {
				logMsg.Format("[%s] error socket broken, socket: %d", m_orkRefId, m_srtsock);
				LOG4CXX_INFO(s_log, logMsg);
				if (srt_close(m_srtsock) == -1) {
					logMsg.Format("[%s] error srt_close: %s", m_orkRefId, srt_getlasterror_str());
					LOG4CXX_INFO(s_log, logMsg);
				}
				return false;
			} else {
				logMsg.Format("[%s] broken once, socket: %d", m_orkRefId, m_srtsock);
				LOG4CXX_INFO(s_log, logMsg);
				brokenLast = true;
			}
		} else {
			brokenLast = false;
		}
		if (count > 100) {
			auto scope = Scope();
			logMsg.Format("[%s] connection loop error. state: %d ", m_orkRefId, state);
			LOG4CXX_INFO(s_log, logMsg);
			return false;
		}
	}
	return true;
}

bool SRTFilter::SetupSRTSocket(UriParser u) {
	CStdString logMsg;
	m_srtsock = srt_create_socket();
	const bool no = false;
	if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_SNDSYN, &no, sizeof(no))) {
		auto scope = Scope();
		logMsg.Format("[%s] error srt_setsockflag SRTO_SNDSYN: %s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		return false;
	}
	if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_RCVSYN, &no, sizeof(no))) {
		auto scope = Scope();
		logMsg.Format("[%s] error srt_setsockflag SRTO_RCVSYN: %s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		return false;
	}
	const int32_t timeout = 2000;
	if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_CONNTIMEO, &timeout, sizeof(timeout))) {
		auto scope = Scope();
		logMsg.Format("[%s] error srt_setsockflag SRTO_CONNTIMEO: %s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		return false;
	}

	auto params = u.parameters();
	for (const auto& param : params) {
		const auto& key = param.first;
		const auto& value = param.second;
		LOG4CXX_DEBUG(s_log, key);
		LOG4CXX_DEBUG(s_log, value);

		if (key == "streamid") {
			if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_STREAMID, value.c_str(), value.length())) {
				auto scope = Scope();
				logMsg.Format("[%s] error srt_setsockflag SRTO_STREAMID: %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
				return false;
			}
		}

		if (key == "passphrase") {
			if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_PASSPHRASE, value.c_str(), value.length())) {
				auto scope = Scope();
				logMsg.Format("[%s] error srt_setsockflag SRTO_PASSPHRASE: %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
				return false;
			}
		}
		if (key == "snddropdelay") {
			const int32_t snddropdelay = std::stoi(value);
			if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_SNDDROPDELAY, &snddropdelay, sizeof(snddropdelay))) {
				auto scope = Scope();
				logMsg.Format("[%s] error srt_setsockflag SRTO_SNDDROPDELAY: %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
				return false;
			}
		}
	}
	return true;
}

void SRTFilter::Close(boost::asio::yield_context yield) {
	CStdString logMsg;
	auto timer = std::make_shared<boost::asio::steady_timer>(boost::asio::get_associated_executor(yield));
	{
		auto scope = Scope();
		logMsg.Format("[%s] closing", m_orkRefId);
		LOG4CXX_INFO(s_log, logMsg);
	}

	while(true) {
		if (DequeueAndProcess(true)) {
			logMsg.Format("[%s] waiting for dequeue", m_orkRefId);
			LOG4CXX_DEBUG(s_log, logMsg);

			timer->expires_after(std::chrono::milliseconds(SRT_CHUNK_MS));
			timer->async_wait(yield);
		} else {
			break;
		}
	}
	size_t lastBytes;
	size_t lastlastBytes;
	size_t lastlastlastBytes;
	while(true) {
		size_t bytes;
		size_t blocks;

		if (SRT_ERROR == srt_getsndbuffer(m_srtsock, &bytes, &blocks)) {
			auto scope = Scope();
			logMsg.Format("[%s] error srt_getsndbuffer: %s", m_orkRefId, srt_getlasterror_str());
			LOG4CXX_INFO(s_log, logMsg);
			m_span->AddEvent("getsndbuffer-failed");
			break;
		}

		bool streakSameBytes = (bytes == lastBytes && bytes == lastlastBytes && bytes == lastlastlastBytes);
		if (bytes == 0 || streakSameBytes) {
			SRT_TRACEBSTATS perf;
			if (SRT_SUCCESS == srt_bstats(m_srtsock, &perf, true)) {
				auto scope = Scope();
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
				auto scope = Scope();
				logMsg.Format("[%s] error srt_bstats", m_orkRefId);
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->AddEvent("stats-failed");
			}
			// receiver latency
			timer->expires_after(std::chrono::seconds(1));
			timer->async_wait(yield);
			if (SRT_ERROR == srt_close(m_srtsock)) {
				auto scope = Scope();
				logMsg.Format("[%s] error srt_close: %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_INFO(s_log, logMsg);
				m_span->AddEvent("close-failed");
			}
			break;
		} else {
			auto scope = Scope();
			logMsg.Format("[%s] remaining bytes: %zu", m_orkRefId, bytes);
			LOG4CXX_DEBUG(s_log, logMsg);
			m_stats.CloseWaitSecond++;
			if (srt_sendmsg2(m_srtsock, SRT_DUMMY, 1, NULL) == SRT_ERROR) {
				logMsg.Format("[%s] error closing srt_sendmsg2: %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_ERROR(s_log, logMsg);
			}
			timer->expires_after(std::chrono::seconds(1));
			timer->async_wait(yield);
			logMsg.Format("[%s] waiting for close", m_orkRefId);
			LOG4CXX_DEBUG(s_log, logMsg);
		}
		lastlastlastBytes = lastlastBytes;
		lastlastBytes = lastBytes;
		lastBytes = bytes;
	}

	auto scope = Scope();
	m_span->AddEvent("close", {
		{"CloseWaitSecond", m_stats.CloseWaitSecond},
		{"ReceivedRightPacket", m_stats.ReceivedRightPacket},
		{"ReceivedLeftPacket", m_stats.ReceivedLeftPacket},
		{"ReceivedPacket", m_stats.ReceivedPacket},
		{"SentPacket", m_stats.SentPacket},
		{"FailedQueue", m_stats.FailedQueue},
	});
	logMsg.Format("[%s] CloseWaitSecond: %d, ReceivedRightPacket: %d, ReceivedLeftPacket: %d, ReceivedPacket: %d, SentPacket: %d, FailedQueue: %d",
		m_orkRefId,
		m_stats.CloseWaitSecond,
		m_stats.ReceivedRightPacket,
		m_stats.ReceivedLeftPacket,
		m_stats.ReceivedPacket,
		m_stats.SentPacket,
		m_stats.FailedQueue);
	LOG4CXX_INFO(s_log, logMsg);
	m_span->SetStatus(trace_api::StatusCode::kOk, "closed");

	m_span->End();
}
// =================================================================
static void SrtLogHandler(void *opaque, int level, const char *file, int line, const char *area, const char *message)
{
	CStdString logMsg;
	logMsg.Format("LIBSRT %s:%d(%s) # %s", file, line, area, message);
	LOG4CXX_INFO(s_log, logMsg);
}
extern "C"
{
	SimpleThreadPool _pool;
	DLL_EXPORT void __CDECL__ OrkInitialize()
	{
		LOG4CXX_INFO(s_log, "SRTFilter starting");

		srt_setloglevel(srt_logging::LogLevel::debug);
		srt_setloghandler(nullptr, SrtLogHandler);

		//SRTConfig
		ConfigManager::Instance()->AddConfigureFunction(SRTConfig::Configure);
		LOG4CXX_INFO(s_log, "SRTFilter registered");

		_pool.Run(SRTCONFIG.m_threadCount);

		FilterRef filter(new SRTFilter(_pool));
		FilterRegistry::instance()->RegisterFilter(filter);

		LOG4CXX_INFO(s_log, "SRTFilter initialized");
	}

}

