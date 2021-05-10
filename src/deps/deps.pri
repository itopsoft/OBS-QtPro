INCLUDEPATH += $$PWD

INCLUDEPATH += $$PWD/json11 \
               $$PWD/w32-pthreads \
               $$PWD/lzma/liblzma/api \
               $$PWD/blake2/src \
               $$PWD/libff \
               $$PWD/obs-scripting

HEADERS += \
    $$PWD/blake2/src/blake2-impl.h \
    $$PWD/blake2/src/blake2.h \
    $$PWD/json11/json11.hpp \
    $$PWD/libff/libff/ff-util.h \
    $$PWD/obs-scripting/cstrcache.h \
    $$PWD/obs-scripting/obs-scripting-lua.h \
    $$PWD/obs-scripting/obs-scripting-python-import.h \
    $$PWD/obs-scripting/obs-scripting-python.h \
    $$PWD/obs-scripting/obs-scripting.h \
    $$PWD/w32-pthreads/pthread.h

SOURCES += \
    $$PWD/blake2/src/blake2b-ref.c \
    $$PWD/json11/json11.cpp \
    $$PWD/libff/libff/ff-util.c \
    $$PWD/obs-scripting/cstrcache.cpp \
    $$PWD/obs-scripting/obs-scripting-logging.c \
    $$PWD/obs-scripting/obs-scripting-lua-frontend.c \
    $$PWD/obs-scripting/obs-scripting-lua-source.c \
    $$PWD/obs-scripting/obs-scripting-lua.c \
    $$PWD/obs-scripting/obs-scripting-python-frontend.c \
    $$PWD/obs-scripting/obs-scripting-python-import.c \
    $$PWD/obs-scripting/obs-scripting-python.c \
    $$PWD/obs-scripting/obs-scripting.c \
    $$PWD/w32-pthreads/pthread.c



