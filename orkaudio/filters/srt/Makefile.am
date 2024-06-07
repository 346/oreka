METASOURCES = AUTO
plugindir= $(prefix)/lib/orkaudio/plugins
plugin_LTLIBRARIES = srt.la
srt_la_SOURCES = SRTConfig.cpp SRTFilter.cpp uriparser.cpp
srt_la_LDFLAGS = -module
AM_CPPFLAGS = -D_REENTRANT
srt_la_LIBADD = -llog4cxx -L/usr/lib -lapr-1 -lpthread -ldl -lm -lcrypto -lstdc++ -L/usr/local/lib -lsrt
INCLUDES = -I@top_srcdir@ -I../../../orkbasecxx -I../../../orkaudio/audiocaptureplugins/common -I../../../orkaudio/audiocaptureplugins/voip  -I/usr/include/apr-1 -I/usr/include/apr-1.0 -I/usr/local/include/srt

all-local: srt.la
	cp .libs/srt.so.0.0.0 ../../plugins/srt.so
