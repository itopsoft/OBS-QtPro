INCLUDEPATH += $$PWD

####  Windows ### - Begin
win32{

    #DEFINES += __STDC_LIMIT_MACROS
#    DEFINES += NO_CRYPTO

    include($$PWD/win/libobs/libobs.pri)

    INCLUDEPATH += $$PWD/win/dependencies2017/win32/include \
                   $$PWD/win/dependencies2017/win32/include/luajit \
                   $$PWD/win/dependencies2017/win32/include/python

    LIBS += -L$$PWD/win/dependencies2017/win32/bin -lluajit  -llibcurl -lpython3 -lpython36


    QMAKE_LFLAGS_DEBUG      = /DEBUG /NODEFAULTLIB:libc.lib /NODEFAULTLIB:libcmt.lib
    QMAKE_LFLAGS_RELEASE    = /RELEASE /NODEFAULTLIB:libc.lib /NODEFAULTLIB:libcmt.lib

    INCLUDEPATH += $$PWD/win/ffmpeg/include

    contains(QT_ARCH, i386) {
        message("32-bit")

        LIBS += -L$$PWD/win/ffmpeg/lib/x86 -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc -lswresample -lswscale
    } else {
        message("64-bit")

        LIBS += -L$$PWD/win/ffmpeg/lib/x64 -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc -lswresample -lswscale
    }

#    LIBS += WS2_32.lib AdvAPI32.lib winmm.lib User32.lib GDI32.lib
    LIBS += -lWS2_32 -lUser32 -lole32 -lGDI32 -lAdvAPI32 -lwinmm -lStrmiids -loleaut32 -lcrypt32

}
####  Windows  ### - End


unix{
#    INCLUDEPATH += $$PWD/linux/ChlFaceSdk/include
#    LIBS += -L$$PWD/linux/ChlFaceSdk/lib -lTHFaceImage -lTHFeature -lTHFaceProperty -lTHFacialPos -lChlFaceSdk

    INCLUDEPATH += $$PWD/linux/ffmpeg/include \
                   $$PWD/linux/grpc/include \
                   $$PWD/linux/abseil-cpp

    contains(QT_ARCH, i386) {d
        message("32-bit, 请自行编译32位库!")
    } else {
        message("64-bit")

        LIBS += -L$$PWD/linux/ffmpeg/lib  -lavformat  -lavcodec -lavdevice -lavfilter -lavutil -lswresample -lswscale
        LIBS += -L$$PWD/linux/grpc/lib -lgrpc -lgrpc++ -lgpr -lgrpc++_reflection -lre2 -lupb -laddress_sorting\
                                       -labsl_base -labsl_city -labsl_cord -labsl_hash -labsl_time -labsl_int128 -labsl_status -labsl_strings -labsl_statusor -labsl_symbolize -labsl_time_zone -labsl_civil_time -labsl_stacktrace -labsl_log_severity -labsl_raw_hash_set -labsl_spinlock_wait -labsl_throw_delegate -labsl_malloc_internal -labsl_synchronization -labsl_strings_internal -labsl_demangle_internal -labsl_bad_variant_access -labsl_debugging_internal -labsl_exponential_biased -labsl_hashtablez_sampler -labsl_bad_optional_access -labsl_str_format_internal -labsl_graphcycles_internal -labsl_raw_logging_internal \
                                       -lcrypto -lssl
        LIBS += -L$$PWD/linux/grpc/lib64 -lprotobuf
        LIBS += -lpthread -ldl
    }


#QMAKE_POST_LINK 表示编译后执行内容
#QMAKE_PRE_LINK 表示编译前执行内容

#解压库文件
#QMAKE_PRE_LINK += "cd $$PWD/lib/linux && tar xvzf ffmpeg.tar.gz "
system("cd $$PWD/lib/linux && tar xvzf ffmpeg.tar.gz")
system("cd $$PWD/lib/linux && tar xvzf jrtplib.tar.gz")

}
