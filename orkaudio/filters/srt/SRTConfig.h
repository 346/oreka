#ifndef __SRTCONFIG_H__
#define __SRTCONFIG_H__

#include "Object.h"
#include "serializers/Serializer.h"
#include "StdString.h"
#include "Utils.h"

#define SRT_SERVER_ENDPOINT "SRTEndpoint"
#define LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD "LiveStreamingQueueFlushThreshold"
#define DEFAULT_LIVE_STREAMING_QUEUE_FLUSH_THRESHOLD 500
#define LIVE_STREAMING_SERVICE_NAME_PARAM "LiveStreamingServiceName"
#define LIVE_STREAMING_SERVICE_NAME_DEFAULT "orkaudio"

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

        CStdString m_srtServerEndpoint;
        int m_queueFlushThresholdMillis;
        CStdString m_serviceName;
};

extern SRTConfig g_SRTConfigObjectRef;

#define SRTCONFIG g_SRTConfigObjectRef

#endif
