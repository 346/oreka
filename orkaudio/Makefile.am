# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
AUTOMAKE_OPTIONS = foreign 1.4


sbin_PROGRAMS = orkaudio
orkaudio_LDADD = @OPENTELEMETRY_EXPORTER_OTLP_FILE_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_FILE_LOG_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_FILE_METRIC_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_FILE_CLIENT_LIBS@ @PROTOBUF_LIBS@  @ABSL_LOG_LIBS@ @OPENTELEMETRY_PROTO_LIBS@ @OPENTELEMETRY_TRACE_LIBS@ @OPENTELEMETRY_LOGS_LIBS@ @OPENTELEMETRY_METRICS_LIBS@ @OPENTELEMETRY_COMMON_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_GRPC_CLIENT_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_GRPC_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_GRPC_LOG_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_GRPC_METRICS_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_HTTP_CLIENT_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_HTTP_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_HTTP_LOG_LIBS@ @OPENTELEMETRY_EXPORTER_OTLP_HTTP_METRIC_LIBS@ @OPENTELEMETRY_EXT_LIBS@
orkaudio_LDFLAGS = -lxerces-c -lorkbase -llog4cxx -L/usr/local/lib -Wl,--whole-archive,-lopentelemetry_log4cxx_appender,--no-whole-archive -lopentelemetry_exporter_ostream_logs -lopentelemetry_exporter_ostream_span -lopentelemetry_exporter_ostream_metrics -lsndfile -l@speex_lib@ -lapr-1 -lssl -lcrypto -lpthread -ldl
orkaudio_SOURCES = 	OrkAudio.cpp
AM_CPPFLAGS = -D_REENTRANT @OPENTELEMETRY_EXT_CFLAGS@ @OPENTELEMETRY_API_CFLAGS@ @OPENTELEMETRY_SDK_CFLAGS@
INCLUDES = -I@top_srcdir@ -I../orkbasecxx -I/opt/opus/include/opus -I../orkbasecxx/filters/gsm -I../orkbasecxx/filters/gsm/gsm610 -I../orkbasecxx/filters/ilbc -I../orkbasecxx/filters/ilbc/ilbc -I../orkbasecxx/filters/audiogain -I../orkbasecxx/filters/g722codec -I../orkbasecxx/filters/speex -I../orkbasecxx/filters/g726codecs -I../orkbasecxx/filters/g726codecs/g72x -I../orkbasecxx/filters/opuscodec -I/usr/include/apr-1 -I/usr/include/apr-1.0 -I/usr/local/include/opentelemetry -I /usr/local/include/opentelemetry/instrumentation/log4cxx -I /usr/local/include/absl -I /usr/local/include/opentelemetry/ext
SUBDIRS = audiocaptureplugins filters
#orkaudio_LDADD = $(top_builddir)/messages/libmessages.la

orkaudioconfdir = /etc/orkaudio
orkaudioconf_DATA = \
	logging-linux-template.properties \
	config-linux-template.xml
install-orkaudioconfDATA:
	mkdir -p /etc/orkaudio
	cp logging-linux-template.properties /etc/orkaudio/logging.properties
	cp config-linux-template.xml /etc/orkaudio/config.xml
	ldconfig
	mkdir -p /var/log/orkaudio


