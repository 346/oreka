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

void InitOtel(std::string service) {

	resource::ResourceAttributes attributes = {
		{"service.name", "orkaudio"},
		{"version", static_cast<uint32_t>(1)},
		{"tenant", service}
	};
	otlp::OtlpFileExporterOptions trace_exp_options;
	otlp::OtlpFileLogRecordExporterOptions log_exp_options;
	otlp::OtlpFileMetricExporterOptions metric_exp_options;
	otlp::OtlpFileClientFileSystemOptions trace_file_options;
	otlp::OtlpFileClientFileSystemOptions log_file_options;
	otlp::OtlpFileClientFileSystemOptions metric_file_options;
	trace_file_options.file_pattern = "/var/log/orkaudio/trace-%N.jsonl";
	trace_file_options.alias_pattern = "/var/log/orkaudio/trace-latest.jsonl";
	log_file_options.file_pattern = "/var/log/orkaudio/logs-%N.jsonl";
	log_file_options.alias_pattern = "/var/log/orkaudio/logs-latest.jsonl";
	metric_file_options.file_pattern = "/var/log/orkaudio/metrics-%N.jsonl";
	metric_file_options.alias_pattern = "/var/log/orkaudio/metrics-latest.jsonl";

	trace_exp_options.backend_options = trace_file_options;
	log_exp_options.backend_options = log_file_options;
	metric_exp_options.backend_options = metric_file_options;
	auto resource = resource::Resource::Create(attributes);
	// logs
	{
		using ProviderPtr = std::shared_ptr<logs_api::LoggerProvider>;
		auto exporter     = otlp::OtlpFileLogRecordExporterFactory::Create(log_exp_options);
		// auto exporter     = log_exp::OStreamLogRecordExporterFactory::Create();
		auto processor    = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
		auto provider     = logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource);
		logs_api::Provider::SetLoggerProvider(ProviderPtr(provider.release()));
	}
	// traces
	{
		using ProviderPtr = std::shared_ptr<trace_api::TracerProvider>;
		auto exporter     = otlp::OtlpFileExporterFactory::Create(trace_exp_options);
		// auto exporter     = trace_exp::OStreamSpanExporterFactory::Create();
		auto processor    = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
		auto provider     = trace_sdk::TracerProviderFactory::Create(std::move(processor), resource);
		trace_api::Provider::SetTracerProvider(ProviderPtr(provider.release()));
	}
	// metrics
	// {
	// 	using ProviderPtr = std::shared_ptr<metrics_api::MeterProvider>;
	// 	auto exporter     = otlp::OtlpFileExporterFactory::Create(metric_exp_options);
	// 	auto reader 	 = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter));
	// 	auto context = metrics_sdk::MeterContextFactory::Create();
	// 	context->AddMetricReader(std::move(reader));
	// 	auto provider     = metrics_sdk::MeterProviderFactory::Create(std::move(context), resource);
	// 	metrics_api::Provider::SetMeterProvider(ProviderPtr(provider.release()));
	// }

	opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
		opentelemetry::nostd::shared_ptr<opentelemetry::context::propagation::TextMapPropagator>(
			new opentelemetry::trace::propagation::HttpTraceContext()));
}
void CleanupOtel()
{
	// {
	// 	std::shared_ptr<metrics_api::MeterProvider> none;
	// 	metrics_api::Provider::SetMeterProvider(none);
	// }
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

	const char* serviceChar = std::getenv("OTEL_SERVICE");
	if (serviceChar == nullptr) {
		serviceChar = "orkaudio";
	}
	std::string service(serviceChar);

	InitOtel(service);

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
}
nostd::shared_ptr<trace_api::Tracer> OrkLogManager::GetTracer(std::string name)
{
  auto provider = trace_api::Provider::GetTracerProvider();
  return provider->GetTracer(name, OPENTELEMETRY_SDK_VERSION);
}

void OrkLogManager::Shutdown()
{
	CleanupOtel();
	log4cxx::LogManager::shutdown();
}
