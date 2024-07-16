
#include "SRTConfig.h"
#include "SRTFilter.h"
#include <log4cxx/logger.h>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <iostream>
#include <vector>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

static log4cxx::LoggerPtr s_log = log4cxx::Logger::getLogger("plugin.srt");

SRTConfig g_SRTConfigObjectRef;

SRTConfig::SRTConfig() {
    m_srtQuery = "";
    m_queueFlushThresholdMillis = DEFAULT_LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD;
    m_serviceName = LIVE_STREAMING_SERVICE_NAME_DEFAULT;
    m_threadCount = SRT_THREAD_COUNT_DEFAULT;
}

void SRTConfig::Reset() {
    m_srtQuery = "";
    m_queueFlushThresholdMillis = DEFAULT_LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD;
    m_serviceName = LIVE_STREAMING_SERVICE_NAME_DEFAULT;
    m_threadCount = SRT_THREAD_COUNT_DEFAULT;
}

void SRTConfig::Define(Serializer* s) {
    s->CsvValue(SRT_SERVER_HOSTS_NAME_PARAM, m_srtServerHosts);
    s->StringValue(SRT_QUERY_NAME_PARAM, m_srtQuery);
    s->IntValue(LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD, m_queueFlushThresholdMillis);
    s->StringValue(LIVE_STREAMING_SERVICE_NAME_PARAM, m_serviceName);
    s->IntValue(SRT_THREAD_COUNT_NAME_PARAM, m_threadCount);
    LOG4CXX_INFO(s_log, "SRTFilter Endpoints");
    for (const auto host : m_srtServerHosts) {
       LOG4CXX_INFO(s_log, boost::format("* %s") % host.c_str());
    }
    m_srtAddresses = SRTConfig::ResolveHostname(m_srtServerHosts);

    LOG4CXX_INFO(s_log, "Resolved addresses:");
    for (const auto& address : m_srtAddresses) {
      LOG4CXX_INFO(s_log, boost::format("* %s") % address.to_string());
    }
    LOG4CXX_INFO(s_log, boost::format("SRT Thread: %d") % m_threadCount);
}
void SRTConfig::Validate() {
}

CStdString SRTConfig::GetClassName() {
	return CStdString("SRTConfig");
}

ObjectRef SRTConfig::NewInstance() {
	return ObjectRef(new SRTConfig);
}

void SRTConfig::Configure(DOMNode* node) {
	if (node){
		try {
			g_SRTConfigObjectRef.DeSerializeDom(node);
			LOG4CXX_INFO(s_log, "SRTConfig Configured");
		} catch (CStdString& e) {
			LOG4CXX_ERROR(s_log, e + " - check your config.xml");
		}
	} else {
		LOG4CXX_ERROR(s_log, "Got empty DOM tree");
	}
}

// ip/hostのリストからIPアドレスのリストを取得する関数
boost::asio::io_context io;
vector<address> SRTConfig::ResolveHostname(const list<CStdString>& hostnames) {
  vector<address> addresses;
  for (const auto& hostname : hostnames) {
    try {
      udp::resolver resolver(io);
      // auto results = resolver.resolve(udp::v4(), string(hostname.c_str()));
			boost::system::error_code ec;
      auto results = resolver.resolve(string(hostname.c_str()), "6000", ec);
      if (ec) {
        LOG4CXX_ERROR(s_log, boost::format("Error resolving hostname: %s") % ec.message());
      } else {
        for (const auto& result : results) {
          if (result.endpoint().address().is_v4()) {
            addresses.push_back(result.endpoint().address());
          } else {
            LOG4CXX_INFO(s_log, boost::format("Skipped address: %s") % result.endpoint().address().to_string());
          }
        }
      }
    } catch (const system_error& e) {
       LOG4CXX_ERROR(s_log, boost::format("Error resolving hostname: %s") % e.what());
    }
  }
  return addresses;
}
