AM_CPPFLAGS = -I$(srcdir)/../statesync -I$(srcdir)/../terminal -I$(srcdir)/../network -I$(srcdir)/../crypto -I../protobufs -I$(srcdir)/../util $(TINFO_CFLAGS) $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS)
AM_LDFLAGS  = $(HARDEN_LDFLAGS)
LDADD = ../crypto/libmoshcrypto.a ../network/libmoshnetwork.a ../statesync/libmoshstatesync.a ../terminal/libmoshterminal.a ../util/libmoshutil.a ../protobufs/libmoshprotos.a -lm $(TINFO_LIBS) $(protobuf_LIBS) $(CRYPTO_LIBS)

mosh_server_LDADD = $(LDADD) $(LIBUTIL)
mosh_LDADD = $(LDADD) $(LIBUTIL) /usr/lib64/libssh2.so

bin_PROGRAMS =

if BUILD_CLIENT
  bin_PROGRAMS += mosh-client mosh
endif

if BUILD_SERVER
  bin_PROGRAMS += mosh-server
endif

if BUILD_IOS_CONTROLLER
  noinst_LIBRARIES = libmoshios.a
endif

mosh_client_SOURCES = mosh-client.cc stmclient.cc stmclient.h terminaloverlay.cc terminaloverlay.h
mosh_server_SOURCES = mosh-server.cc
mosh_SOURCES = mosh.cc stmclient.cc stmclient.h terminaloverlay.cc terminaloverlay.h
libmoshios_a_SOURCES = mosh-ios-controller.cc terminalbridge.cc terminaloverlay.cc terminaloverlay.h
