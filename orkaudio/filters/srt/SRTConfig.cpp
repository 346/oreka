
#include "SRTConfig.h"
#include <log4cxx/logger.h>

static log4cxx::LoggerPtr s_log = log4cxx::Logger::getLogger("plugin.srt");

SRTConfig g_SRTConfigObjectRef;

SRTConfig::SRTConfig() {
    m_queueFlushThresholdMillis = DEFAULT_LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD;
    m_serviceName = LIVE_STREAMING_SERVICE_NAME_DEFAULT;
}

void SRTConfig::Reset() {
    m_srtServerEndpoint = "";
    m_queueFlushThresholdMillis = DEFAULT_LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD;
    m_serviceName = LIVE_STREAMING_SERVICE_NAME_DEFAULT;
}

void SRTConfig::Define(Serializer* s) {
    s->StringValue(SRT_SERVER_ENDPOINT, m_srtServerEndpoint);
    s->IntValue(LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD, m_queueFlushThresholdMillis);
    s->StringValue(LIVE_STREAMING_SERVICE_NAME_PARAM, m_serviceName);
    LOG4CXX_INFO(s_log, "SRTFilter Endpoint " + m_srtServerEndpoint);
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

