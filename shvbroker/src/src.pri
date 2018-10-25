
HEADERS += \
    $$PWD/appclioptions.h \
    $$PWD/brokerapp.h \
    $$PWD/brokernode.h \
    $$PWD/shvclientnode.h \
    $$PWD/subscriptionsnode.h \
    $$PWD/fstabnode.h

SOURCES += \
    $$PWD/main.cpp\
    $$PWD/appclioptions.cpp \
    $$PWD/brokerapp.cpp \
    $$PWD/brokernode.cpp \
    $$PWD/shvclientnode.cpp \
    $$PWD/subscriptionsnode.cpp \
    $$PWD/fstabnode.cpp

include ($$PWD/sql/sql.pri)
include ($$PWD/rpc/rpc.pri)




