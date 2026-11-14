#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTextStream>

#include "backend.h"

static QString socketPath() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
           + QStringLiteral("/hyprtab.sock");
}

// Sends a command to the running GUI instance and prints its response.
static int sendCommand(const QStringList &args) {
    QLocalSocket sock;
    sock.connectToServer(socketPath());
    if (!sock.waitForConnected(1000)) {
        QTextStream(stderr) << "hyprtab: GUI is not running (cannot connect to "
                            << socketPath() << ")\n";
        return 1;
    }
    sock.write(args.join(QLatin1Char(' ')).toUtf8());
    sock.flush();
    sock.waitForReadyRead(3000);
    const QByteArray resp = sock.readAll();
    QTextStream out(stdout);
    out << resp;
    if (!resp.isEmpty() && !resp.endsWith('\n'))
        out << "\n";
    out.flush();
    return 0;
}

int main(int argc, char *argv[]) {
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("hyprtab")); // Wayland app_id = "hyprtab"
    app.setOrganizationName(QStringLiteral("hyprtab"));

    // With arguments the binary acts as a CLI client of the GUI process.
    if (app.arguments().size() > 1)
        return sendCommand(app.arguments().mid(1));

    // Single instance: if a GUI is already listening, don't steal its socket.
    {
        QLocalSocket probe;
        probe.connectToServer(socketPath());
        if (probe.waitForConnected(500)) {
            QTextStream(stderr) << "hyprtab: GUI is already running on " << socketPath() << "\n";
            return 0;
        }
    }

    Backend backend;

    QLocalServer server;
    QLocalServer::removeServer(socketPath()); // clear a stale socket file
    if (!server.listen(socketPath())) {
        QTextStream(stderr) << "hyprtab: cannot listen on " << socketPath() << ": "
                            << server.errorString() << "\n";
        return 2;
    }
    QTextStream(stderr) << "hyprtab: GUI listening on " << socketPath() << "\n";
    QObject::connect(&server, &QLocalServer::newConnection, &backend, [&] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            client->waitForReadyRead(500);
            const QString line = QString::fromUtf8(client->readAll()).trimmed();
            const QString resp = backend.handleCommand(line);
            client->write(resp.toUtf8());
            client->flush();
            client->disconnectFromServer();
            QObject::connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
        }
    });

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("hypricon"), new IconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("windowModel"), backend.model());

    const QUrl url(QStringLiteral("qrc:/qt/qml/Hyprtab/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url, &backend](QObject *obj, const QUrl &objUrl) {
                         if (!obj && objUrl == url)
                             QCoreApplication::exit(-1);
                         if (obj)
                             backend.setWindow(qobject_cast<QQuickWindow *>(obj));
                     }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
