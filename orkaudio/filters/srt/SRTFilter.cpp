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
SRTFilter::SRTFilter(SimpleThreadPool &pool) :
	pool(pool),
	m_bufferQueueA(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL),
	m_bufferQueueB(SRTCONFIG.m_queueFlushThresholdMillis/G711_PACKET_INTERVAL),
	m_connected(false),
	m_connecting(false),
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
		logMsg.Format("SRTFilter::Send [%s] Memory allocation failed.", m_orkRefId);
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
	auto chunk = std::make_shared<SrtChunk>(SrtChunk{outputBuffer, size});
	if (!m_pushQueue.push(chunk)) {
		m_stats.FailedQueue++;
	}
}

bool SRTFilter::DequeueAndProcess() {
	std::shared_ptr<SrtChunk> chunk;
	if (m_pushQueue.pop(chunk)) {
		PushToSRT(chunk->buffer, chunk->size);
		free(chunk->buffer);
		return true;
	} else {
		return false;
	}
}

void SRTFilter::PushToSRT(char* outputBuffer, int size) {
	auto ret = srt_sendmsg(m_srtsock, outputBuffer, size, -1, false);
	if (ret == SRT_ERROR || ret != size) {
		CStdString logMsg;
		logMsg.Format("SRTFilter::Send [%s] error:%s", m_orkRefId, srt_getlasterror_str());
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
			m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
			return;
		}
		if (m_orkUid.empty()) {
			logMsg.Format("SRTFilter:: Start[%s] Failed for Empty Ork UID", m_orkUid);
			LOG4CXX_ERROR(s_log, logMsg);
			m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
			return;
		}

		m_connecting.store(true);

		boost::asio::io_context& ctx(pool.GetContext());
		boost::asio::spawn(ctx, [&](auto yield) {
			auto scope = Scope();
			Connect(yield);
			m_connecting.store(false);

			auto timer = std::make_shared<boost::asio::steady_timer>(ctx);
			auto delay = 15;
			while(true) {
				if (m_closeReceived.load()) {
					return;
				}
				DequeueAndProcess();
				timer->expires_after(std::chrono::milliseconds(delay));
				timer->async_wait(yield);
			}
		});
	}

	if (event->m_type == CaptureEvent::EventTypeEnum::EtStop) {
		m_closeReceived.store(true);
		boost::asio::io_context& ctx(pool.GetContext());
		boost::asio::spawn(ctx, [&](auto yield) {
			Close(yield);
		});
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

void SRTFilter::Connect(boost::asio::yield_context yield) {
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
	m_span->AddEvent("connect");
	for (const auto i : m_shuffledHostIndexes) {
		if (m_closeReceived.load()) {
			break;
		}
		auto address = SRTCONFIG.m_srtAddresses[i];
		logMsg.Format("SRTFilter:: Start[%s] LiveStreamingId %s", m_orkRefId, liveStreamingId);
		std::string url = GetURL(address, liveStreamingId, carrier.headers_);
		logMsg.Format("SRTFilter:: Start[%s] Streaming URL %s", m_orkRefId, url.c_str());
		LOG4CXX_DEBUG(s_log, logMsg);
		UriParser u(url);

		if (!SetupSRTSocket(u)) {
			continue;
		}
		if (!TryConnect(yield, u)) {
			continue;
		}
		m_connected.store(true);
		break;
	}
	if (!m_connected.load()) {
		CStdString logMsg;
		logMsg.Format("[%s] error srt_connect", m_orkRefId);
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		m_span->AddEvent("srtfilter-failed");
	}
}
bool SRTFilter::TryConnect(boost::asio::yield_context yield, UriParser u) {
	CStdString logMsg;
	sockaddr_in addr_in;
	memset(&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons(std::stoi(u.port()));
	inet_pton4(u.host().c_str(), &addr_in.sin_addr);

	int epollid = srt_epoll_create();
	if (epollid == -1) {
		logMsg.Format("[%s] %s", m_orkRefId, srt_getlasterror_str());
		return false;
	}
	int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
	if (SRT_ERROR == srt_epoll_add_usock(epollid, m_srtsock, &modes)) {
		logMsg.Format("[%s] %s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_INFO(s_log, logMsg);
		return false;
	}
	sockaddr* addr = (struct sockaddr*)&addr_in;

	if (SRT_ERROR == srt_connect(m_srtsock, addr, sizeof(addr_in))) {
		int rej = srt_getrejectreason(m_srtsock);
		logMsg.Format("[%s] %s:%s", m_orkRefId, srt_getlasterror_str(), srt_rejectreason_str(rej));
		LOG4CXX_INFO(s_log, logMsg);
		m_span->AddEvent("connect-failed", {
			{"address", u.hostport()},
			{"reason", srt_getlasterror_str()},
			{"reject", srt_rejectreason_str(rej)},
		});
		return false;
	}
	auto timer = std::make_shared<boost::asio::steady_timer>(boost::asio::get_associated_executor(yield));
	while(true) {
		int rlen = 1;
		int wlen = 1;
		SRTSOCKET rready;
		SRTSOCKET wready;
		if (srt_epoll_wait(epollid, &rready, &rlen, &wready, &wlen, 0, 0, 0, 0, 0) != -1) {
			SRT_SOCKSTATUS state = srt_getsockstate(m_srtsock);
			if (state == SRTS_CONNECTED) {
				logMsg.Format("[%s] connected", m_orkRefId);
				LOG4CXX_DEBUG(s_log, logMsg);
				m_span->AddEvent("connected", {
					{"address", u.hostport()},
				});
				return true;
			}
			if (state == SRTS_BROKEN) {
				logMsg.Format("[%s] error srt_epoll_wait: state broken, socket %d", m_orkRefId, m_srtsock);
				LOG4CXX_INFO(s_log, logMsg);
				return false;
			}
		}
		logMsg.Format("[%s] waiting for connection", m_orkRefId);
		LOG4CXX_DEBUG(s_log, logMsg);
		timer->expires_after(std::chrono::milliseconds(30));
		timer->async_wait(yield);
	}
	LOG4CXX_DEBUG(s_log, "return");
	return true;
}

bool SRTFilter::SetupSRTSocket(UriParser u) {
	CStdString logMsg;
	m_srtsock = srt_create_socket();
	const bool no = false;
	if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_SNDSYN, &no, sizeof(no))) {
		logMsg.Format("[%s] sndsyn error %s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		return false;
	}
	if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_RCVSYN, &no, sizeof(no))) {
		logMsg.Format("[%s] rcvsyn error %s", m_orkRefId, srt_getlasterror_str());
		LOG4CXX_ERROR(s_log, logMsg);
		m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
		return false;
	}
	const int32_t timeout = 2000;
	if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_CONNTIMEO, &timeout, sizeof(timeout))) {
		logMsg.Format("[%s] conn timeout error %s", m_orkRefId, srt_getlasterror_str());
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
				logMsg.Format("[%s] streamid error %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
				return false;
			}
		}

		if (key == "passphrase") {
			if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_PASSPHRASE, value.c_str(), value.length())) {
				logMsg.Format("[%s] passphrase error %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_ERROR(s_log, logMsg);
				m_span->SetStatus(trace_api::StatusCode::kError, logMsg);
				return false;
			}
		}
		if (key == "snddropdelay") {
			const int32_t snddropdelay = std::stoi(value);
			if (SRT_ERROR == srt_setsockflag(m_srtsock, SRTO_SNDDROPDELAY, &snddropdelay, sizeof(snddropdelay))) {
				logMsg.Format("[%s] snddropdelay error %s", m_orkRefId, srt_getlasterror_str());
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
	logMsg.Format("[%s] Close", m_orkRefId);
	LOG4CXX_DEBUG(s_log, logMsg);
	while(true) {
		if (m_connecting.load()) {
			m_stats.CloseWaitSecond++;
			timer->expires_after(std::chrono::seconds(1));
			timer->async_wait(yield);
			logMsg.Format("[%s] waiting for connecting", m_orkRefId);
			LOG4CXX_DEBUG(s_log, logMsg);
		} else {
			break;
		}
	}
	if (!m_connected.load()) {
		return;
	}
	while(true) {
		if (DequeueAndProcess()) {
			logMsg.Format("[%s] waiting for dequeue", m_orkRefId);
			LOG4CXX_DEBUG(s_log, logMsg);
			timer->expires_after(std::chrono::milliseconds(20));
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
			logMsg.Format("[%s] %s", m_orkRefId, srt_getlasterror_str());
			LOG4CXX_INFO(s_log, logMsg);
			m_span->AddEvent("getsndbuffer-failed");
			break;
		}

		bool streakSameBytes = (bytes == lastBytes && bytes == lastlastBytes && bytes == lastlastlastBytes);
		if (bytes == 0 || streakSameBytes) {
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
			timer->expires_after(std::chrono::seconds(1));
			timer->async_wait(yield);
			if (SRT_ERROR == srt_close(m_srtsock)) {
				logMsg.Format("[%s] %s", m_orkRefId, srt_getlasterror_str());
				LOG4CXX_INFO(s_log, logMsg);
				m_span->AddEvent("close-failed");
			}
			break;
		} else {
			logMsg.Format("[%s] Remaining bytes: %zu", m_orkRefId, bytes);
			LOG4CXX_DEBUG(s_log, logMsg);
			m_stats.CloseWaitSecond++;
			timer->expires_after(std::chrono::seconds(1));
			timer->async_wait(yield);
			LOG4CXX_DEBUG(s_log, "waiting for close");
		}
		lastlastlastBytes = lastlastBytes;
		lastlastBytes = lastBytes;
		lastBytes = bytes;
	}


	m_span->AddEvent("close", {
		{"CloseWaitSecond", m_stats.CloseWaitSecond},
		{"ReceivedRightPacket", m_stats.ReceivedRightPacket},
		{"ReceivedLeftPacket", m_stats.ReceivedLeftPacket},
		{"ReceivedPacket", m_stats.ReceivedPacket},
		{"OverflowPacket", m_stats.OverflowPacket},
		{"SentPacket", m_stats.SentPacket},
		{"FailedQueue", m_stats.FailedQueue},
	});
	logMsg.Format("[%s] CloseWaitSecond: %d, ReceivedRightPacket: %d, ReceivedLeftPacket: %d, ReceivedPacket: %d, OverflowPacket: %d, SentPacket: %d, FailedQueue: %d",
		m_orkRefId,
		m_stats.CloseWaitSecond,
		m_stats.ReceivedRightPacket,
		m_stats.ReceivedLeftPacket,
		m_stats.ReceivedPacket,
		m_stats.OverflowPacket,
		m_stats.SentPacket,
		m_stats.FailedQueue);
	LOG4CXX_INFO(s_log, logMsg);

	m_span->End();
}
// =================================================================

extern "C"
{
	SimpleThreadPool _pool;
	DLL_EXPORT void __CDECL__ OrkInitialize()
	{
		LOG4CXX_INFO(s_log, "SRTFilter starting");

		//SRTConfig
		ConfigManager::Instance()->AddConfigureFunction(SRTConfig::Configure);
		LOG4CXX_INFO(s_log, "SRTFilter registered");

		_pool.Run(SRTCONFIG.m_threadCount);

		FilterRef filter(new SRTFilter(_pool));
		FilterRegistry::instance()->RegisterFilter(filter);

		LOG4CXX_INFO(s_log, "SRTFilter initialized");
	}

}
