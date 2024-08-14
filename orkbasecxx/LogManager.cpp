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

#define _WINSOCKAPI_		// prevents the inclusion of winsock.h

#include "LogManager.h"
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/logmanager.h>
#include "Utils.h"
#include <fstream>


OrkLogManager OrkLogManager::m_orkLogManager;

OrkLogManager* OrkLogManager::Instance()
{
	return &m_orkLogManager;
}

namespace {

std::string getEnv(std::string envName, std::string defaultString) {
	const char* envchr = std::getenv(envName.c_str());
	if (envchr == nullptr) {
		return defaultString;
	}
	std::string envstring(envchr);
	return envstring;
}
int flushInterval = 5000000;

std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> createFileLogExporter() {
	otlp::OtlpFileLogRecordExporterOptions log_exp_options;
	otlp::OtlpFileClientFileSystemOptions log_file_options;
	log_file_options.file_pattern = "/var/log/orkaudio/logs-%Y%m%d-%N.jsonl";
	log_file_options.alias_pattern = "/var/log/orkaudio/logs-latest.jsonl";
	log_file_options.flush_interval = std::chrono::microseconds(flushInterval);
	log_exp_options.backend_options = log_file_options;
	auto exporter     = otlp::OtlpFileLogRecordExporterFactory::Create(log_exp_options);
	return exporter;
}

std::unique_ptr<trace_sdk::SpanExporter> createFileSpanExporter() {
	otlp::OtlpFileExporterOptions trace_exp_options;
	otlp::OtlpFileClientFileSystemOptions trace_file_options;
	trace_file_options.file_pattern = "/var/log/orkaudio/trace-%Y%m%d-%N.jsonl";
	trace_file_options.alias_pattern = "/var/log/orkaudio/trace-latest.jsonl";
	trace_file_options.flush_interval = std::chrono::microseconds(flushInterval);
	trace_exp_options.backend_options = trace_file_options;
	auto exporter     = otlp::OtlpFileExporterFactory::Create(trace_exp_options);
	return exporter;
}
std::unique_ptr<metrics_sdk::PushMetricExporter> createFileMetricExporter() {
	otlp::OtlpFileMetricExporterOptions metric_exp_options;
	otlp::OtlpFileClientFileSystemOptions metric_file_options;
	metric_file_options.file_pattern = "/var/log/orkaudio/metrics-%Y%m%d-%N.jsonl";
	metric_file_options.alias_pattern = "/var/log/orkaudio/metrics-latest.jsonl";
	metric_file_options.flush_interval = std::chrono::microseconds(flushInterval);
	metric_exp_options.backend_options = metric_file_options;
	auto exporter     = otlp::OtlpFileMetricExporterFactory::Create(metric_exp_options);
	return exporter;
}
void initOtelLog(resource::Resource resource) {
	std::unique_ptr<logs_sdk::LogRecordExporter> logsExporter;
	auto otlpLogsProto = getEnv("OTEL_EXPORTER_OTLP_LOGS_PROTOCOL", "file");
	auto logsExporterName = getEnv("OTEL_LOGS_EXPORTER", "otlp");

	if (logsExporterName == "console") {
		logsExporter = log_exp::OStreamLogRecordExporterFactory::Create();
	} else if (logsExporterName == "otlp") {
		if (otlpLogsProto == "grpc") {
			logsExporter = otlp::OtlpGrpcLogRecordExporterFactory::Create();
		} else if (otlpLogsProto == "http/protobuf") {
			logsExporter = otlp::OtlpHttpLogRecordExporterFactory::Create();
		} else if (otlpLogsProto == "file") {
			logsExporter = createFileLogExporter();
		} else {
			LOG4CXX_ERROR(LOG.rootLog, "Unknown OTLP logs protocol: " << otlpLogsProto);
		}
	} else {
		LOG4CXX_ERROR(LOG.rootLog, "Unknown logs exporter: " << logsExporterName);
	}
	if (logsExporter) {
		using ProviderPtr = std::shared_ptr<logs_api::LoggerProvider>;
		auto processor    = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(logsExporter));
		auto provider     = logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource);
		logs_api::Provider::SetLoggerProvider(ProviderPtr(provider.release()));
		LOG4CXX_INFO(LOG.rootLog, "Logs exporter configured: " << logsExporterName << " " << otlpLogsProto);
	} else {
		LOG4CXX_INFO(LOG.rootLog, "No logs exporter configured");
		return;
	}
}
void initOtelTrace(resource::Resource resource) {
	std::unique_ptr<trace_sdk::SpanExporter> spanExporter;
	auto otlpTracesProto = getEnv("OTEL_EXPORTER_OTLP_TRACES_PROTOCOL", "file");
	auto tracesExporterName = getEnv("OTEL_TRACES_EXPORTER", "otlp");
	if (tracesExporterName == "console") {
		spanExporter = trace_exp::OStreamSpanExporterFactory::Create();
	} else if (tracesExporterName == "otlp") {
		if (otlpTracesProto == "grpc") {
			spanExporter = otlp::OtlpGrpcExporterFactory::Create();
		} else if (otlpTracesProto == "http/protobuf") {
			spanExporter = otlp::OtlpHttpExporterFactory::Create();
		} else if (otlpTracesProto == "file") {
			spanExporter = createFileSpanExporter();
		} else {
			LOG4CXX_ERROR(LOG.rootLog, "Unknown OTLP traces protocol: " << otlpTracesProto);
		}
	} else {
		LOG4CXX_ERROR(LOG.rootLog, "Unknown traces exporter: " << tracesExporterName);
	}
	if (spanExporter) {
		using ProviderPtr = std::shared_ptr<trace_api::TracerProvider>;
		auto processor    = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(spanExporter));
		auto provider     = trace_sdk::TracerProviderFactory::Create(std::move(processor), resource);
		trace_api::Provider::SetTracerProvider(ProviderPtr(provider.release()));
		LOG4CXX_INFO(LOG.rootLog, "Traces exporter configured: " << tracesExporterName << " " << otlpTracesProto);
	} else {
		LOG4CXX_INFO(LOG.rootLog, "No trace exporter configured");
		return;
	}
	opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
		opentelemetry::nostd::shared_ptr<opentelemetry::context::propagation::TextMapPropagator>(
			new opentelemetry::trace::propagation::HttpTraceContext()));
}

void initOtelMetric(resource::Resource resource) {
	// metrics
	std::unique_ptr<metrics_sdk::PushMetricExporter> metricExporter;
	auto otlpMetricsProto = getEnv("OTEL_EXPORTER_OTLP_METRICS_PROTOCOL", "file");
	auto metricsExporterName = getEnv("OTEL_METRICS_EXPORTER", "otlp");
	if (metricsExporterName == "console") {
		metricExporter = metrics_exp::OStreamMetricExporterFactory::Create();
	} else if (metricsExporterName == "otlp") {
		if (otlpMetricsProto == "grpc") {
			metricExporter = otlp::OtlpGrpcMetricExporterFactory::Create();
		} else if (otlpMetricsProto == "http/protobuf") {
			metricExporter = otlp::OtlpHttpMetricExporterFactory::Create();
		} else if (otlpMetricsProto == "file") {
			metricExporter = createFileMetricExporter();
		} else {
			LOG4CXX_ERROR(LOG.rootLog, "Unknown OTLP metrics protocol: " << otlpMetricsProto);
		}
	} else {
		LOG4CXX_ERROR(LOG.rootLog, "Unknown metrics exporter: " << metricsExporterName);
	}
	if (metricExporter) {
		using ProviderPtr = std::shared_ptr<metrics_api::MeterProvider>;
		metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
		reader_options.export_interval_millis = std::chrono::milliseconds(10000);
		reader_options.export_timeout_millis  = std::chrono::milliseconds(500);
		auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(metricExporter), reader_options);
		auto registry = metrics_sdk::ViewRegistryFactory::Create();
		auto context = metrics_sdk::MeterContextFactory::Create(std::move(registry), resource);
		context->AddMetricReader(std::move(reader));
		auto provider = metrics_sdk::MeterProviderFactory::Create(std::move(context));
		metrics_api::Provider::SetMeterProvider(ProviderPtr(provider.release()));
		LOG4CXX_INFO(LOG.rootLog, "Metrics exporter configured: " << metricsExporterName << " " << otlpMetricsProto);
	} else {
		LOG4CXX_INFO(LOG.rootLog, "No metrics exporter configured");
		return;
	}

}
void InitOtel() {


	resource::ResourceAttributes attributes = {
		{"service.name", "orkaudio"},
		{"version", static_cast<uint32_t>(1)},
	};

	auto resource = resource::Resource::Create(attributes);

	initOtelLog(resource);
	initOtelTrace(resource);
	initOtelMetric(resource);
}
void CleanupOtel()
{
	{
		std::shared_ptr<metrics_api::MeterProvider> none;
		metrics_api::Provider::SetMeterProvider(none);
	}
	{
		std::shared_ptr<opentelemetry::trace::TracerProvider> none;
		trace_api::Provider::SetTracerProvider(none);
	}
	{
		// logger_provider->ForceFlush();
		// logger_provider.reset();
		nostd::shared_ptr<logs_api::LoggerProvider> none;
		opentelemetry::logs::Provider::SetLoggerProvider(none);
	}
}

}
void OrkLogManager::Initialize()
{
	OrkAprSubPool locPool;


	BasicConfigurator::resetConfiguration();
	BasicConfigurator::configure();
	apr_status_t ret;
	char* logCfgFilename = NULL;
	char* cfgEnvPath = "";
	int cfgAlloc = 0;

	ret = apr_env_get(&cfgEnvPath, "ORKAUDIO_CONFIG_PATH", AprLp);
	if(ret == APR_SUCCESS) {
		apr_dir_t* dir;
		ret = apr_dir_open(&dir, cfgEnvPath, AprLp);
		if(ret == APR_SUCCESS)
		{
			int len = 0;
			apr_dir_close(dir);
			len = strlen(cfgEnvPath)+1+strlen("logging.properties")+1;
			logCfgFilename = (char*)malloc(len);

					if(logCfgFilename) {
							cfgAlloc = 1;
							apr_snprintf(logCfgFilename, len, "%s/%s", cfgEnvPath, "logging.properties", AprLp);
					}
		}
	}

	if(!logCfgFilename) {
		std::fstream file;
		file.open("logging.properties", std::fstream::in);
		if(file.is_open()){
			logCfgFilename = (char*)"logging.properties";
			file.close();
		}
		else
		{
			// logging.properties could not be found in the current
			// directory, try to find it in system configuration directory
			logCfgFilename = (char*)"/etc/orkaudio/logging.properties";
		}
	}

	// If this one fails, the above default configuration stays valid
	PropertyConfigurator::configure(logCfgFilename);

	// XXX should we free this here?
	if(cfgAlloc) {
		free(logCfgFilename);
	}

	rootLog  = Logger::getLogger("root");
	topLog = Logger::getLogger("top");
	immediateProcessingLog = Logger::getLogger("immediateProcessing");
	batchProcessingLog = Logger::getLogger("batchProcessing");
	tapeFileNamingLog = Logger::getLogger("tapeFileNamingLog");
	portLog =  Logger::getLogger("port");
	fileLog = Logger::getLogger("file");
	configLog = Logger::getLogger("config");
	tapelistLog = Logger::getLogger("tapelist");
	tapeLog = Logger::getLogger("tape");
	clientLog = Logger::getLogger("orkclient");
	directionSelectorLog = Logger::getLogger("directionSelector");
	reporting = Logger::getLogger("reporting");
	ipfragmentation = Logger::getLogger("ipfragmentation");
	messaging = Logger::getLogger("messaging");

	InitOtel();
}

nostd::shared_ptr<trace_api::Tracer> OrkLogManager::GetTracer(nostd::string_view name)
{
	auto provider = trace_api::Provider::GetTracerProvider();
	return provider->GetTracer(name, OPENTELEMETRY_SDK_VERSION);
}
nostd::shared_ptr<metrics_api::Meter> OrkLogManager::GetMeter(nostd::string_view name)
{
	auto provider = metrics_api::Provider::GetMeterProvider();
	return provider->GetMeter(name, OPENTELEMETRY_SDK_VERSION);
}
void OrkLogManager::Shutdown()
{
	CleanupOtel();
	log4cxx::LogManager::shutdown();
}
