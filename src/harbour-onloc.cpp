#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <sailfishapp.h>
#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QScopedPointer>
#include <QLocale>
#include <QTranslator>

#include "controller/appcontroller.h"
#include "map/mapcanvas.h"
#include <QtQml>

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    // Source strings are English; load a matching translation (if any) for
    // the device's locale.
    QTranslator translator;
    const QString translationsDir = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    if (translator.load(QLocale(), QStringLiteral("harbour-onloc"), QStringLiteral("-"),
                         translationsDir)) {
        app->installTranslator(&translator);
    }

    // Sailjail whitelists (and persists) ~/.config/<OrganizationName>/
    // <ApplicationName> based on the .desktop file's [X-Sailjail] values;
    // AppController's settings file lives under
    // QStandardPaths::AppConfigLocation, which is built from exactly these
    // two names. Keep them in lockstep with harbour-onloc.desktop.
    app->setOrganizationName(QStringLiteral("io.github.jarnose"));
    app->setApplicationName(QStringLiteral("onloc"));

    // The base map is a C++ item (see src/map/mapcanvas.h for why).
    qmlRegisterType<MapCanvas>("io.github.jarnose.onloc", 1, 0, "MapCanvas");

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    AppController controller;
    view->rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}
