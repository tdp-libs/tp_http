TARGET = tp_http
TEMPLATE = lib

DEFINES += TP_HTTP_LIBRARY

SOURCES += src/Globals.cpp
HEADERS += inc/tp_http/Globals.h

SOURCES += src/Client.cpp
HEADERS += inc/tp_http/Client.h

SOURCES += src/Request.cpp
HEADERS += inc/tp_http/Request.h

SOURCES += src/ResolverResults.cpp
HEADERS += inc/tp_http/ResolverResults.h

SOURCES += src/AsyncTimer.cpp
HEADERS += inc/tp_http/AsyncTimer.h
