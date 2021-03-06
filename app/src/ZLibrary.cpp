/*
 * Copyright (C) 2015-2018 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Jolla Ltd nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "BooksDefs.h"
#include "BooksStorage.h"
#include "BooksPaintContext.h"
#include "BooksDialogManager.h"
#include "BooksImageProvider.h"
#include "BooksMediaPlugin.h"
#include "BooksPolicyPlugin.h"
#include "BooksSettings.h"

#include "HarbourDebug.h"

#include "ZLibrary.h"
#include "ZLApplication.h"
#include "ZLLanguageUtil.h"
#include "ZLLogger.h"

#include "filesystem/ZLQtFSManager.h"
#include "time/ZLQtTime.h"
#include "image/ZLQtImageManager.h"
#include "iconv/IConvEncodingConverter.h"
#include "formats/xhtml/XHTMLReader.h"
#include "library/Tag.h"

#include <sailfishapp.h>
#include <MGConfItem>

#include <QGuiApplication>
#include <QStandardPaths>
#include <QQuickView>
#include <QQmlContext>
#include <QScreen>

#include <execinfo.h>
#include <dlfcn.h>

bool ZLibrary::ourLocaleIsInitialized = false;
std::string ZLibrary::ourLanguage;
std::string ZLibrary::ourCountry;
std::string ZLibrary::ourZLibraryDirectory;

std::string ZLibrary::ourApplicationName;
std::string ZLibrary::ourImageDirectory;
std::string ZLibrary::ourApplicationImageDirectory;
std::string ZLibrary::ourApplicationDirectory;
std::string ZLibrary::ourApplicationWritableDirectory;
std::string ZLibrary::ourDefaultFilesPathPrefix;

const std::string ZLibrary::FileNameDelimiter("/");
const std::string ZLibrary::PathDelimiter(":");
const std::string ZLibrary::EndOfLine("\n");
const std::string ZLibrary::BaseDirectory;

int booksPPI = 240;

void ZLibrary::initLocale()
{
    const char* locale = setlocale(LC_MESSAGES, "");
    HDEBUG(locale);
    if (locale) {
        std::string sLocale = locale;
        const int dotIndex = sLocale.find('.');
        if (dotIndex != -1) sLocale = sLocale.substr(0, dotIndex);
        const int dashIndex = std::min(sLocale.find('_'), sLocale.find('-'));
        if (dashIndex == -1) {
            ourLanguage = sLocale;
        } else {
            ourLanguage = sLocale.substr(0, dashIndex);
            ourCountry = sLocale.substr(dashIndex + 1);
        }
    }
}

ZLPaintContext* ZLibrary::createContext()
{
    HDEBUG("creating context");
    return new BooksPaintContext();
}

bool ZLibrary::init(int& aArgc, char** &aArgv)
{
    HDEBUG("initializing");

    ZLibrary::parseArguments(aArgc, aArgv);

    std::string rootDir("/");
    Dl_info info;
    void* addr = NULL;
    if (backtrace(&addr, 1) && dladdr(addr, &info)) {
        // Step two levels up. For an application deployed from QtCreator
        // it's going to be /opt/sdk/<app-name> directory, for a normally
        // installed app - the root directory.
        HDEBUG("app path" << info.dli_fname);
        char* slash = (char*)strrchr(info.dli_fname, '/');
        if (slash) {
            slash[0] = 0;
            HDEBUG("app dir" << info.dli_fname);
            slash = (char*)strrchr(info.dli_fname, '/');
            if (slash) {
                slash[0] = 0;
                slash = (char*)strrchr(info.dli_fname, '/');
                if (slash) {
                    slash[1] = 0;
                    HDEBUG("root dir" << info.dli_fname);
                    rootDir = info.dli_fname;
                }
            }
        }
    }

    ((std::string&)BaseDirectory) = rootDir;
    ourApplicationName = BOOKS_APP_NAME;
    ourZLibraryDirectory = BaseDirectory + BOOKS_DATA_DIR;
    ourImageDirectory = BaseDirectory + BOOKS_ICONS_DIR;
    ourDefaultFilesPathPrefix  = ourZLibraryDirectory + "/";
    ourApplicationDirectory = ourZLibraryDirectory;
    ourApplicationImageDirectory = ourImageDirectory;
    ourApplicationWritableDirectory =
        (QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
         QLatin1String("/" BOOKS_APP_NAME)).toStdString();

    HDEBUG("zlibrary dir" << ourZLibraryDirectory.c_str());
    HDEBUG("image dir" << ourImageDirectory.c_str());
    HDEBUG("writable dir" << ourApplicationWritableDirectory.c_str());

    // Determine screen DPI.
    MGConfItem dpiConfig("/lipstick/screen/primary/physicalDotsPerInch");
    QVariant dpiValue(dpiConfig.value());
    HDEBUG("physicalDotsPerInch" << dpiValue);
    if (dpiValue.isValid() && dpiValue.toInt() > 0) {
        booksPPI = dpiValue.toInt();
    } else {
        MGConfItem ratioConfig("/desktop/sailfish/silica/theme_pixel_ratio");
        QVariant ratioValue(ratioConfig.value());
        HDEBUG("theme_pixel_ratio" << ratioValue);
        if (ratioValue.isValid()) {
            const qreal ratio = ratioValue.toReal();
            if (ratio > 0.0) {
                booksPPI = (int)(ratio*booksPPI);
                booksPPI -= booksPPI % 10;
            }
        }
    }
    HDEBUG("screen" << booksPPI << "dpi");

    ZLQtTimeManager::createInstance();
    ZLQtFSManager::createInstance();
    BooksDialogManager::createInstance();
    ZLQtImageManager::createInstance();
    ZLEncodingCollection::Instance().registerProvider(new IConvEncodingConverterProvider());
    ZLApplication::Instance();
    ZLFile::initCache();

    // Due to the weird inter-dependency between BooksSettings and
    // BooksStorageManager, BooksSettings has to be created first.
    // Doing it the other way around will result in two instances of
    // BooksStorageManager being created :)
    QSharedPointer<BooksSettings> settings = BooksSettings::sharedInstance();
    BooksStorageManager::instance();
    return true;
}

void ZLibrary::run(ZLApplication* aApp)
{
    if (ZLLanguageUtil::isRTLLanguage(ZLibrary::Language())) {
        qApp->setLayoutDirection(Qt::RightToLeft);
    }

    QString qml(QString::fromStdString(BaseDirectory + BOOKS_QML_FILE));
    HDEBUG("qml file" << qPrintable(qml));

    QQuickView* view = SailfishApp::createView();
    QQmlContext* root = view->rootContext();
    QQmlEngine* engine = root->engine();
    QSharedPointer<BooksSettings> settings = BooksSettings::sharedInstance();
    BooksPolicyPlugin::registerTypes(engine, BOOKS_QML_PLUGIN,
        BOOKS_QML_PLUGIN_V1, BOOKS_QML_PLUGIN_V2);
    BooksMediaPlugin::registerTypes(engine, BOOKS_QML_PLUGIN,
        BOOKS_QML_PLUGIN_V1, BOOKS_QML_PLUGIN_V2);
    engine->addImageProvider(BooksImageProvider::PROVIDER_ID,
        new BooksImageProvider(root));

    root->setContextProperty("PointsPerInch", booksPPI);
    root->setContextProperty("MaximumHintCount", 1);
    root->setContextProperty("BooksSettingsMenu",
        QVariant::fromValue(BOOKS_SETTINGS_MENU));
    root->setContextProperty("Settings", settings.data());

    view->setTitle(qtTrId("harbour-books-app-name"));
    view->setSource(QUrl::fromLocalFile(qml));
    view->show();
    HDEBUG("started");
    qApp->exec();
    HDEBUG("exiting...");
}

void ZLibrary::parseArguments(int &argc, char **&argv)
{
    for (int i=1; i<argc; i++) {
        if ((i+1)<argc && !strcmp(argv[i], "-d")) {
            ZLLogger::Instance().registerClass(argv[i+1]);
            memcpy(argv+i, argv+i+2, sizeof(char*)*(argc-i-2));
            argc -= 2;
            i -= i;
        }
    }
}

void ZLibrary::shutdown()
{
    ZLApplication::deleteInstance();
    ZLImageManager::deleteInstance();
    ZLDialogManager::deleteInstance();
    ZLFSManager::deleteInstance();
    ZLTimeManager::deleteInstance();
    BooksStorageManager::deleteInstance();
    XHTMLReader::clearTagTable();
    Tag::clearTags();
}

void ZLibrary::initApplication(const std::string& aName)
{
    HDEBUG(aName.c_str());
}

std::string ZLibrary::Language()
{
    if (ourLanguage.empty()) {
        if (!ourLocaleIsInitialized) {
            initLocale();
            ourLocaleIsInitialized = true;
        }
    }
    if (ourLanguage.empty()) {
        ourLanguage = "en";
    }
    return ourLanguage;
}

std::string ZLibrary::Country()
{
    if (ourCountry.empty() && !ourLocaleIsInitialized) {
        initLocale();
        ourLocaleIsInitialized = true;
    }
    return ourCountry;
}
