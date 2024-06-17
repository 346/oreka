#ifndef __SRTCONFIG_H__
#define __SRTCONFIG_H__

#include "Object.h"
#include "serializers/Serializer.h"
#include "StdString.h"
#include "Utils.h"
#include "boost/asio.hpp"

#define SRT_SERVER_HOSTS_NAME_PARAM "SRTServerHosts"
#define SRT_QUERY_NAME_PARAM "SRTQuery"
#define LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD "LiveStreamingQueueFlushThreshold"
#define DEFAULT_LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD 500
#define LIVE_STREAMING_SERVICE_NAME_PARAM "LiveStreamingServiceName"
#define LIVE_STREAMING_SERVICE_NAME_DEFAULT "orkaudio"

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

class SRTConfig : public Object {
    public:
        SRTConfig();
        void Define(Serializer* s);
        void Validate();
        void Reset();
        ObjectRef NewInstance();
        CStdString GetClassName();
        inline ObjectRef Process() {return ObjectRef();};

        static void Configure(DOMNode* node);

        vector<address> ResolveHostname(const list<CStdString> &hostnames);

        list<CStdString> m_srtServerHosts;
        CStdString m_srtQuery;
        vector<address> m_srtAddresses;
        int m_queueFlushThresholdMillis;
        CStdString m_serviceName;
};

extern SRTConfig g_SRTConfigObjectRef;

#define SRTCONFIG g_SRTConfigObjectRef

#endif
