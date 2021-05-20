INCLUDEPATH += $$PWD

INCLUDEPATH += $$PWD/json11 \
               $$PWD/w32-pthreads \
               $$PWD/libff

HEADERS += \
    $$PWD/json11/json11.hpp \
    $$PWD/libff/libff/ff-util.h \
    $$PWD/w32-pthreads/pthread.h

SOURCES += \
    $$PWD/json11/json11.cpp \
    $$PWD/libff/libff/ff-util.c \
    $$PWD/w32-pthreads/pthread.c



