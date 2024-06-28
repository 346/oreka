/*
 * Oreka -- A media capture and retrieval platform
 *
 * Copyright (C) 2005, orecx LLC
 *
 * http://www.orecx.com
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 * Please refer to http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef __LOGMANAGER_H__
#define __LOGMANAGER_H__

#include <log4cxx/logger.h>
#include "dll.h"
#include "OrkBase.h"
#ifdef UNIT_TESTING
#include "StdString.h"
#endif
using namespace log4cxx;

#include <opentelemetry/instrumentation/log4cxx/appender.h>

#include <opentelemetry/sdk/version/version.h>

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/ostream/log_record_exporter_factory.h>

#include <opentelemetry/exporters/otlp/otlp_file_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_file_metric_exporter_factory.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/sdk/logs/exporter.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/processor.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>

#include <opentelemetry/exporters/otlp/otlp_file_exporter_factory.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"
#include "opentelemetry/sdk/metrics/meter_context.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/sdk/metrics/state/filtered_ordered_attribute_map.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/meter_selector.h"
#include "opentelemetry/sdk/metrics/view/meter_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/view.h"
#include "opentelemetry/sdk/metrics/view/view_factory.h"
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/sdk/resource/resource.h>

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace metrics_api = opentelemetry::metrics;
namespace otlp = opentelemetry::exporter::otlp;
namespace logs_api = opentelemetry::logs;
namespace logs_sdk = opentelemetry::sdk::logs;
namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace trace_exp = opentelemetry::exporter::trace;
namespace log_exp = opentelemetry::exporter::logs;
namespace resource = opentelemetry::sdk::resource;

namespace nostd = opentelemetry::nostd;


class DLL_IMPORT_EXPORT_ORKBASE OrkLogManager
{
public:
	static OrkLogManager* Instance();

	void Initialize();
	nostd::shared_ptr<trace_api::Tracer> GetTracer(std::string name);
	void Shutdown();

	LoggerPtr rootLog;
	LoggerPtr topLog;
	LoggerPtr immediateProcessingLog;
	LoggerPtr batchProcessingLog;
	LoggerPtr tapeFileNamingLog;
	LoggerPtr portLog;
	LoggerPtr fileLog;
	LoggerPtr configLog;
	LoggerPtr tapelistLog;
	LoggerPtr tapeLog;
	LoggerPtr clientLog;
	LoggerPtr directionSelectorLog;
	LoggerPtr reporting;
	LoggerPtr ipfragmentation;
	LoggerPtr messaging;

private:
	static OrkLogManager m_orkLogManager;
};

#define LOG (*OrkLogManager::Instance())

#define FLOG_DEBUG(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__); LOG4CXX_DEBUG(logger, logMsg);
#define FLOG_INFO(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__); LOG4CXX_INFO(logger, logMsg);
#define FLOG_WARN(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__); LOG4CXX_WARN(logger, logMsg);
#define FLOG_ERROR(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__); LOG4CXX_ERROR(logger, logMsg);


#ifdef UNIT_TESTING

void TEST_LOG_INFO(CStdString& logMsg);
void TEST_LOG_DEBUG(CStdString& logMsg);
void TEST_LOG_WARN(CStdString& logMsg);
void TEST_LOG_ERROR(CStdString& logMsg);

#undef FLOG_DEBUG
#undef FLOG_INFO
#undef FLOG_WARN
#undef FLOG_ERROR

#define FLOG_DEBUG(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__);TEST_LOG_DEBUG(logMsg);
#define FLOG_INFO(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__);TEST_LOG_INFO(logMsg);
#define FLOG_WARN(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__);TEST_LOG_WARN(logMsg);
#define FLOG_ERROR(logger,fmt, ...) logMsg.Format(fmt,__VA_ARGS__);TEST_LOG_ERROR(logMsg);

#endif

#endif

