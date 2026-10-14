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
    parser.addOption({"window-width","启动窗口宽度","pixels","1180"});
    parser.addOption({"window-height","启动窗口高度","pixels","780"});
    parser.addOption({"layout-report","输出详情页滚动视口诊断"});
    parser.addOption({"import-bundle","启动后导入 CPNetFlux 节点包","path"});
    parser.process(app);
    RpcClient rpc(parser.value("socket"));
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("rpc",&rpc);
    engine.rootContext()->setContextProperty("initialPage",parser.value("page").toInt());
    engine.load(QUrl("qrc:/ui/Main.qml"));
    if(engine.rootObjects().isEmpty()) return 2;
    if(auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        window->resize(parser.value("window-width").toInt(),parser.value("window-height").toInt());
    }
    if(parser.isSet("import-bundle")) QTimer::singleShot(200, &app, [&] { rpc.importProvisioningBundle(parser.value("import-bundle")); });
    if(parser.isSet("smoke-ms")) QTimer::singleShot(parser.value("smoke-ms").toInt(), &app, [&] {
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        bool ok=window && rpc.snapshotCount() >= 0;
        if(parser.isSet("screenshot") && window) window->grabWindow().save(parser.value("screenshot"));
        qInfo("desktop smoke: connected=%d snapshots=%d",rpc.connected(),rpc.snapshotCount());
        if(parser.isSet("layout-report")) {
            auto detail=engine.rootObjects().first()->findChild<QObject*>("detailScroll");
            if(detail) qInfo("detail scroll: viewport=%f content=%f",detail->property("availableHeight").toReal(),detail->property("contentHeight").toReal());
        }
        app.exit(ok?0:3);
    });
    return app.exec();
}
