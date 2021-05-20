QT       += core gui xml svg

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

#CONFIG += c++11
CONFIG += c++17

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

#将输出文件直接放到源码目录下的bin目录下，将dll都放在了此目录中，用以解决运行后找不到dll的问
#DESTDIR=$$PWD/bin/
win32{
    contains(QT_ARCH, i386) {
        message("32-bit")
        DESTDIR = $${PWD}/bin/bin/32bit
    } else {
        message("64-bit")
        DESTDIR = $${PWD}/bin/win64
    }
}

INCLUDEPATH += $$PWD/src/UI \
               $$PWD/src/UI/obs-frontend-api

#这里必须引入obs-frontend-api.lib， 不能引入 obs-frontend-api.cpp，否则会奔溃。
LIBS += -L$$PWD/src/UI/obs-frontend-api/lib/x86/Debug -lobs-frontend-api


### lib ### Begin
    include($$PWD/lib/lib.pri)
### lib ### End

### lib ### Begin
    include($$PWD/src/deps/deps.pri)
### lib ### End

INCLUDEPATH += $$PWD/src \
               $$PWD/src/Config \
               $$PWD/src/Main \
               $$PWD/src/Source \
               $$PWD/src/Display \
               $$PWD/src/Setting

SOURCES += \
    src/Config/window-basic-auto-config-test.cpp \
    src/Config/window-basic-auto-config.cpp \
    src/Display/qt-display.cpp \
    src/Display/window-basic-preview.cpp \
    src/Setting/window-basic-settings-stream.cpp \
    src/Setting/window-basic-settings.cpp \
    src/main.cpp \
    src/Main/OBSBasic.cpp \
    src/Main/window-basic-main-browser.cpp \
    src/Main/window-basic-main-dropfiles.cpp \
    src/Main/window-basic-main-icons.cpp \
    src/Main/window-basic-main-outputs.cpp \
    src/Main/window-basic-main-profiles.cpp \
    src/Main/window-basic-main-scene-collections.cpp \
    src/Main/window-basic-main-screenshot.cpp \
    src/Main/window-basic-main-transitions.cpp \
    src/mainwindow.cpp \
    src/obs-app.cpp \
    src/Source/OBSBasicProperties.cpp \
    src/Source/OBSBasicSourceSelect.cpp \
    src/Source/properties-view.cpp

HEADERS += \
    src/Config/window-basic-auto-config.hpp \
    src/Display/display-helpers.hpp \
    src/Display/qt-display.hpp \
    src/Display/window-basic-preview.hpp \
    src/Main/OBSBasic.hpp \
    src/Main/window-basic-main-outputs.hpp \
    src/Main/window-main.hpp \
    src/Setting/window-basic-settings.hpp \
    src/mainwindow.h \
    src/obs-app.hpp \
    src/Source/OBSBasicProperties.hpp \
    src/Source/OBSBasicSourceSelect.hpp \
    src/Source/properties-view.hpp \
    src/Source/properties-view.moc.hpp

FORMS += \
    src/Config/AutoConfigFinishPage.ui \
    src/Config/AutoConfigStartPage.ui \
    src/Config/AutoConfigStreamPage.ui \
    src/Config/AutoConfigTestPage.ui \
    src/Config/AutoConfigVideoPage.ui \
    src/Setting/OBSBasicSettings.ui \
    src/mainwindow.ui \
    src/Main/OBSBasic.ui \
    src/Source/OBSBasicSourceSelect.ui


SOURCES += \
    src/UI/adv-audio-control.cpp \
    src/UI/api-interface.cpp \
    src/UI/audio-encoders.cpp \
    src/UI/auth-base.cpp \
#    src/UI/auth-oauth.cpp \
#    src/UI/auth-restream.cpp \
#    src/UI/auth-twitch.cpp \
    src/UI/combobox-ignorewheel.cpp \
    src/UI/context-bar-controls.cpp \
    src/UI/crash-report.cpp \
    src/UI/double-slider.cpp \
    src/UI/focus-list.cpp \
    src/UI/horizontal-scroll-area.cpp \
    src/UI/hotkey-edit.cpp \
    src/UI/importers/classic.cpp \
    src/UI/importers/importers.cpp \
    src/UI/importers/sl.cpp \
    src/UI/importers/studio.cpp \
    src/UI/importers/xsplit.cpp \
    src/UI/item-widget-helpers.cpp \
    src/UI/locked-checkbox.cpp \
    src/UI/log-viewer.cpp \
    src/UI/media-controls.cpp \
    src/UI/media-slider.cpp \
    src/UI/menu-button.cpp \
    src/UI/obs-proxy-style.cpp \
    src/UI/platform-windows.cpp \
#    src/UI/platform-x11.cpp \
    src/UI/qt-wrappers.cpp \
    src/UI/record-button.cpp \
    src/UI/remote-text.cpp \
    src/UI/scene-tree.cpp \
    src/UI/slider-absoluteset-style.cpp \
    src/UI/slider-ignorewheel.cpp \
    src/UI/source-label.cpp \
    src/UI/source-tree.cpp \
    src/UI/spinbox-ignorewheel.cpp \
    src/UI/ui-validation.cpp \
    src/UI/url-push-button.cpp \
    src/UI/vertical-scroll-area.cpp \
    src/UI/visibility-checkbox.cpp \
    src/UI/visibility-item-widget.cpp \
    src/UI/volume-control.cpp \
    src/UI/window-basic-adv-audio.cpp \
    src/UI/window-basic-filters.cpp \
    src/UI/window-basic-interaction.cpp \
    src/UI/window-basic-stats.cpp \
    src/UI/window-basic-status-bar.cpp \
    src/UI/window-basic-transform.cpp \
#    src/UI/window-dock-browser.cpp \
    src/UI/window-dock.cpp \
#    src/UI/window-extra-browsers.cpp \
    src/UI/window-importer.cpp \
    src/UI/window-log-reply.cpp \
    src/UI/window-namedialog.cpp \
    src/UI/window-projector.cpp

HEADERS += \
    src/UI/adv-audio-control.hpp \
    src/UI/audio-encoders.hpp \
    src/UI/auth-base.hpp \
#    src/UI/auth-oauth.hpp \
#    src/UI/auth-restream.hpp \
#    src/UI/auth-twitch.hpp \
    src/UI/balance-slider.hpp \
    src/UI/clickable-label.hpp \
    src/UI/combobox-ignorewheel.hpp \
    src/UI/context-bar-controls.hpp \
    src/UI/crash-report.hpp \
    src/UI/double-slider.hpp \
    src/UI/expand-checkbox.hpp \
    src/UI/focus-list.hpp \
    src/UI/horizontal-scroll-area.hpp \
    src/UI/hotkey-edit.hpp \
    src/UI/importers/importers.hpp \
    src/UI/item-widget-helpers.hpp \
    src/UI/locked-checkbox.hpp \
    src/UI/log-viewer.hpp \
    src/UI/media-controls.hpp \
    src/UI/media-slider.hpp \
    src/UI/menu-button.hpp \
    src/UI/mute-checkbox.hpp \
    src/UI/obs-proxy-style.hpp \
    src/UI/platform.hpp \
    src/UI/qt-wrappers.hpp \
    src/UI/record-button.hpp \
    src/UI/remote-text.hpp \
    src/UI/scene-tree.hpp \
    src/UI/screenshot-obj.hpp \
    src/UI/slider-absoluteset-style.hpp \
    src/UI/slider-ignorewheel.hpp \
    src/UI/source-label.hpp \
    src/UI/source-tree.hpp \
    src/UI/spinbox-ignorewheel.hpp \
    src/UI/ui-validation.hpp \
    src/UI/url-push-button.hpp \
    src/UI/vertical-scroll-area.hpp \
    src/UI/visibility-checkbox.hpp \
    src/UI/visibility-item-widget.hpp \
    src/UI/volume-control.hpp \
    src/UI/window-basic-adv-audio.hpp \
    src/UI/window-basic-filters.hpp \
    src/UI/window-basic-interaction.hpp \
    src/UI/window-basic-stats.hpp \
    src/UI/window-basic-status-bar.hpp \
    src/UI/window-basic-transform.hpp \
#    src/UI/window-dock-browser.hpp \
    src/UI/window-dock.hpp \
#    src/UI/window-extra-browsers.hpp \
    src/UI/window-importer.hpp \
    src/UI/window-log-reply.hpp \
    src/UI/window-namedialog.hpp \
    src/UI/window-projector.hpp

FORMS += \
    src/UI/forms/ColorSelect.ui \
    src/UI/forms/OBSBasicFilters.ui \
    src/UI/forms/OBSBasicInteraction.ui \
    src/UI/forms/OBSBasicTransform.ui \
    src/UI/forms/OBSExtraBrowsers.ui \
    src/UI/forms/OBSImporter.ui \
    src/UI/forms/OBSLogReply.ui \
    src/UI/forms/source-toolbar/browser-source-toolbar.ui \
    src/UI/forms/source-toolbar/color-source-toolbar.ui \
    src/UI/forms/source-toolbar/device-select-toolbar.ui \
    src/UI/forms/source-toolbar/game-capture-toolbar.ui \
    src/UI/forms/source-toolbar/image-source-toolbar.ui \
    src/UI/forms/source-toolbar/media-controls.ui \
    src/UI/forms/source-toolbar/text-source-toolbar.ui


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    src/UI/forms/obs.qrc

DEFINES += WIN32
DEFINES += _WINDOWS
DEFINES += DEBUG=1
DEFINES += _DEBUG=1
#DEFINES += DL_OPENGL="libobs-opengl.dll"
DEFINES += DL_D3D9=""
#DEFINES += DL_D3D11="libobs-d3d11.dll"
DEFINES += UNICODE
DEFINES += _UNICODE
DEFINES += _CRT_SECURE_NO_WARNINGS
DEFINES += _CRT_NONSTDC_NO_WARNINGS
DEFINES += HAVE_OBSCONFIG_H
DEFINES += QT_WIDGETS_LIB
DEFINES += QT_GUI_LIB
DEFINES += QT_CORE_LIB
DEFINES += QT_SVG_LIB
DEFINES += QT_XML_LIB
DEFINES += CMAKE_INTDIR="Debug"
