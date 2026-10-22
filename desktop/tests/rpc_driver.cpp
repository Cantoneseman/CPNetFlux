#include "rpc_client.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    RpcClient rpc(app.arguments().at(1));
    const bool uncertain=app.arguments().value(2)=="uncertain";
    QTimer flow;flow.setInterval(20);int step=0;
    QObject::connect(&flow,&QTimer::timeout,&app,[&]{
        if(!uncertain||!rpc.contractReady()||rpc.stale()||rpc.busy())return;
        if(step==0&&!rpc.profiles().isEmpty()){rpc.create("upload","/tmp","dest",false,"v3","peer",{});step=1;}
        else if(step==1&&rpc.validation().value("valid").toBool()){rpc.confirmCreate();step=2;}
        else if(step==2&&rpc.creationUncertain()){rpc.confirmCreate();step=3;}
    });flow.start();
    if(!uncertain)QTimer::singleShot(350,&rpc,&RpcClient::refresh);
    QTimer::singleShot(2000,&app,[&]{
        QJsonObject result{{"count",rpc.snapshotCount()},{"ready",rpc.contractReady()},
            {"stale",rpc.stale()},{"message",rpc.message()},{"uncertain",rpc.creationUncertain()},{"step",step}};
        puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());app.quit();
    });
    return app.exec();
}
