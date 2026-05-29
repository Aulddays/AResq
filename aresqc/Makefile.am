bin_PROGRAMS = aresqc

aresqc_SOURCES = \
	aresqc.cpp \
	stdafx.h \
	stdafx.cpp \
	targetver.h

aresqc_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libaresq \
	-I$(top_srcdir)/libsqlite3

aresqc_CXXFLAGS = \
	-std=c++11 \
	-Wall -Wno-parentheses \
	-ffunction-sections \
	-fdata-sections \
	$(AM_CXXFLAGS)

aresqc_LDADD = \
	$(top_builddir)/libaresq/libaresq.a \
	$(top_builddir)/libsqlite3/libsqlite3.a \
	$(top_builddir)/libsmb2/libsmb2.a \
	$(top_builddir)/libconfig/libconfig.a \
	$(WINDOWS_SOCKET_LIBS) \
	-lshlwapi

aresqc_LDFLAGS = -mconsole -static-libgcc -static-libstdc++ -Wl,--gc-sections

aresqc_DEPENDENCIES = \
	$(top_builddir)/libaresq/libaresq.a \
	$(top_builddir)/libsqlite3/libsqlite3.a \
	$(top_builddir)/libconfig/libconfig.a
