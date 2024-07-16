/*
 * Oreka -- A media capture and retrieval platform
 *
 * SRTFilter Plugin
 * Author Takahiro Mishiro
 *
 */

#ifndef __SRT_H__
#define __SRT_H__ 1

#define BOOST_ASIO_DISABLE_BOOST_COROUTINE

#include "LogManager.h"
#include "Filter.h"
#include <log4cxx/logger.h>
#include "SimpleThreadPool.hpp"
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

#include <algorithm>
#include <random>
#include <ctime>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>


#include "opentelemetry/exporters/ostream/span_exporter_factory.h"
#include "opentelemetry/nostd/detail/decay.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/tracer_provider.h"

#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/ext/http/common/url_parser.h"
#include "opentelemetry/trace/semantic_conventions.h"

#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/sdk/trace/tracer_context_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace context   = opentelemetry::context;
namespace nostd     = opentelemetry::nostd;


using namespace boost::uuids;

struct SrtFilterStats {
	int CloseWaitSecond;
	int ReceivedRightPacket;
	int ReceivedLeftPacket;
	int ReceivedPacket;
	int OverflowPacket;
	SrtFilterStats() : CloseWaitSecond(0), ReceivedRightPacket(0), ReceivedLeftPacket(0), ReceivedPacket(0), OverflowPacket(0) {}
};

struct SrtChunk {
	char* buffer;
	int size;
};

class DLL_IMPORT_EXPORT_ORKBASE SRTFilter : public Filter {
	public:
		SRTFilter(SimpleThreadPool &pool);
		~SRTFilter();

		std::shared_ptr<trace_api::Scope> Scope();

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
		bool m_status = true;
		bool m_isFirstPacket = true;
		int m_currentBufferChannel;
		std::mt19937 m_rng;
		std::vector<int>m_shuffledHostIndexes;
		u_int32_t m_timestamp = 0;
		RingBuffer<AudioChunkRef> m_bufferQueueA;
		RingBuffer<AudioChunkRef> m_bufferQueueB;
		RingBuffer<SrtChunk> m_pushQueue;
		bool m_useBufferA = true;
		char * m_silentChannelBuffer = NULL;
		std::string m_srtUrl;
		void PushToSRT(char* buffer, int size);
		void AddQueue(AudioChunkDetails& channelDetails, char* firstChannelBuffer, char* secondChannelBuffer);
		bool DequeueAndProcess();
		void Connect();
		void Close();
		std::string GetURL(std::string liveStreamingId, std::map<std::string, std::string> &headers);
		nostd::shared_ptr<trace_api::Span> m_span;
		std::atomic<bool> m_connected;
		std::atomic<bool> m_connecting;
		std::atomic<bool> m_closeReceived;
		SrtFilterStats m_stats;
		SimpleThreadPool &pool;
};

class SrtTextMapCarrier : public opentelemetry::context::propagation::TextMapCarrier
{
public:
	SrtTextMapCarrier(std::map<std::string, std::string> &headers) : headers_(headers) {}
	SrtTextMapCarrier() = default;
	virtual opentelemetry::nostd::string_view Get(
		opentelemetry::nostd::string_view key) const noexcept override
	{
		std::string key_to_compare = key.data();
		// Header's first letter seems to be  automatically capitaliazed by our test http-server, so
		// compare accordingly.
		if (key == opentelemetry::trace::propagation::kTraceParent)
		{
			key_to_compare = "Traceparent";
		}
		else if (key == opentelemetry::trace::propagation::kTraceState)
		{
			key_to_compare = "Tracestate";
		}
		auto it = headers_.find(key_to_compare);
		if (it != headers_.end())
		{
			return it->second;
		}
		return "";
	}

  virtual void Set(opentelemetry::nostd::string_view key,
				   opentelemetry::nostd::string_view value) noexcept override
  {
	headers_.insert(std::pair<std::string, std::string>(std::string(key), std::string(value)));
  }

  std::map<std::string, std::string> headers_;
};
#endif
