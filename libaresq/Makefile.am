noinst_LIBRARIES = libaresq.a

libaresq_a_SOURCES = \
	Aresq.h \
	Aresq.cpp \
	AresqIgnore.h \
	AresqIgnore.cpp \
	audbg.h \
	auto_buf.hpp \
	fsadapter.h \
	fsadapter.cpp \
	match.cpp \
	Monitor.h \
	Monitor.cpp \
	RevisionMgr.h \
	RevisionMgr.cpp \
	pe_log.h \
	pe_log.cpp \
	record.h \
	Register.h \
	Register.cpp \
	resguard.h \
	Root.h \
	Root.cpp \
	Remote.h \
	Remote.cpp \
	RemoteSmb.h \
	RemoteSmb.cpp \
	stdafx.h \
	stdafx.cpp \
	targetver.h \
	tasking.h \
	utfconv.h \
	utfconv.cpp \
	utils.h \
	ap_dirent.h

libaresq_a_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/libconfig \
	-I$(top_srcdir)/libsmb2/include \
	-I$(top_srcdir)/libsqlite3

libaresq_a_CXXFLAGS = \
	-std=c++11 \
	-Wall -Wno-parentheses -Wno-unused-variable -Wno-unused-function \
	-ffunction-sections \
	-fdata-sections \
	$(AM_CXXFLAGS)

libaresq_a_LIBADD = \
	$(top_builddir)/libsmb2/libsmb2.a \
	$(top_builddir)/libconfig/libconfig.a
