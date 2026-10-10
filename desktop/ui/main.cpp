#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <QCommandLineParser>
#include "rpc_client.h"
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    app.setApplicationName("CPNetFlux Desktop");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"socket","agent Unix socket","path",qEnvironmentVariable("XDG_RUNTIME_DIR")+"/cpnetflux/agent.sock"});
    parser.addOption({"smoke-ms","offscreen运行后退出","milliseconds"});
    parser.addOption({"screenshot","保存窗口截图","path"});
    parser.addOption({"page","初始页面0..6","number","0"});
    parser.process(app);
    RpcClient rpc(parser.value("socket"));
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("rpc",&rpc);
    engine.rootContext()->setContextProperty("initialPage",parser.value("page").toInt());
    engine.load(QUrl("qrc:/ui/Main.qml"));
    if(engine.rootObjects().isEmpty()) return 2;
    if(parser.isSet("smoke-ms")) QTimer::singleShot(parser.value("smoke-ms").toInt(), &app, [&] {
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        bool ok=window && rpc.snapshotCount() >= 0;
        if(parser.isSet("screenshot") && window) window->grabWindow().save(parser.value("screenshot"));
        qInfo("desktop smoke: connected=%d snapshots=%d",rpc.connected(),rpc.snapshotCount());
        app.exit(ok?0:3);
    });
    return app.exec();
}
