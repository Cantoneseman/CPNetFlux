#include "rpc_client.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);
    if(argc!=3)return 2;
    QFile f(app.arguments()[2]);if(!f.open(QIODevice::ReadOnly))return 2;
    auto config=QJsonDocument::fromJson(f.readAll()).object();
    RpcClient rpc(app.arguments()[1]);
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("rpc",&rpc);
    engine.rootContext()->setContextProperty("initialPage",1);
    engine.load(QUrl("qrc:/ui/Main.qml"));if(engine.rootObjects().isEmpty())return 2;
    auto root=engine.rootObjects().first();int stage=0;bool cancelled=false,resumed=false;
    QElapsedTimer elapsed;elapsed.start();QTimer timer;timer.setInterval(5);
    auto finish=[&](int code,QVariantMap extra){
        extra["message"]=rpc.message();extra["code"]=rpc.lastErrorCode();
        extra["tasks"]=rpc.tasks();extra["evidence"]=rpc.evidence();
        extra["files"]=rpc.files();extra["attempts"]=rpc.attempts();extra["ranges"]=rpc.ranges();
        puts(QJsonDocument(QJsonObject::fromVariantMap(extra)).toJson(QJsonDocument::Compact).constData());
        app.exit(code);
    };
    auto click=[&](const char* name){
        auto button=root->findChild<QObject*>(name);
        return button&&button->property("enabled").toBool()&&QMetaObject::invokeMethod(button,"clicked");
    };
    QObject::connect(&timer,&QTimer::timeout,&app,[&]{
        if(elapsed.elapsed()>45000){finish(3,{{"failure","deadline"},{"stage",stage}});return;}
        if(!rpc.lastErrorCode().isEmpty()){finish(4,{{"failure","rpc_error"},{"stage",stage}});return;}
        if(stage==0&&rpc.contractReady()&&!rpc.stale()&&!rpc.profiles().isEmpty()){
            if(config["queued_task_id"].isString()){
                root->setProperty("selectedId",config["queued_task_id"].toString());root->setProperty("page",2);
                if(!click("resumeButton")){finish(5,{{"failure","queued_start_disabled"}});return;}stage=2;return;
            }
            root->findChild<QObject*>("localPath")->setProperty("text",config["local"].toString());
            root->findChild<QObject*>("remotePath")->setProperty("text",config["remote"].toString());
            root->findChild<QObject*>("direction")->setProperty("currentIndex",config["direction"]=="download"?1:0);
            auto policy=QJsonObject::fromVariantMap(rpc.contract().value("policy_defaults").toMap());
            const auto overrides=config["policy"].toObject();
            for(auto it=overrides.begin();it!=overrides.end();++it)policy[it.key()]=it.value();
            root->setProperty("draftPolicy",policy.toVariantMap());
            if(!click("validateButton")){finish(5,{{"failure","validate_disabled"}});return;}stage=1;
        }else if(stage==1&&rpc.validation().value("valid").toBool()){
            if(!click("confirmCreateButton")){finish(5,{{"failure","confirm_disabled"}});return;}
            // The second click must not produce another transfer.create.
            QMetaObject::invokeMethod(root->findChild<QObject*>("confirmCreateButton"),"clicked");
            stage=2;
        }else if(stage==2){
            QVariantMap task;
            for(const auto& row:rpc.tasks()){
                const auto path=row.toMap().value("destination").toMap().value("path").toString();
                if(path==(config["direction"]=="download"?config["local"].toString():config["remote"].toString())){task=row.toMap();break;}
            }
            if(task.isEmpty())return;
            root->setProperty("selectedId",task.value("transfer_id"));root->setProperty("page",2);
            auto state=task.value("state").toString();
            if(config["detach"].toBool() && state=="transferring"){
                finish(0,{{"detached_while_active",true},{"transfer_id",task.value("transfer_id")}});return;
            }
            if(config["cancel"].toBool()&&!cancelled){
                QFile checkpoint(config["checkpoint"].toString());
                if(checkpoint.open(QIODevice::ReadOnly)){
                    const auto lines=checkpoint.readAll().split('\n');
                    for(const auto& line:lines)if(line.startsWith("verified_chunks=")&&line.size()>16){
                        if(!click("cancelButton")){finish(5,{{"failure","cancel_disabled"}});return;}
                        cancelled=true;break;
                    }
                }
            }
            if(cancelled&&!resumed&&state=="cancelled"){
                if(task.value("recovery").toMap().value("eligibility")!="eligible"){
                    finish(6,{{"failure","no_checkpoint_eligibility"}});return;
                }
                if(!click("resumeButton")){finish(5,{{"failure","resume_disabled"}});return;}
                resumed=true;
            }else if(state=="completed"){
                if(config["cancel"].toBool()&&!resumed){finish(6,{{"failure","cancel_missed"}});return;}
                rpc.loadDetails(task.value("transfer_id").toString(),task.value("attempt").toMap().value("attempt_id").toString());stage=3;
            }else if(state=="failed"||state=="recoverable"){finish(6,{{"failure","transfer_failed"}});return;}
        }else if(stage==3&&rpc.evidence().value("observation")=="final_summary"&&!rpc.files().isEmpty()&&!rpc.attempts().isEmpty()){
            if(rpc.hasMoreFiles()){rpc.loadNextFiles();return;}
            for(const auto& file:rpc.files())if(file.toMap().value("range_count").toLongLong()>0){rpc.selectFile(file.toMap());stage=4;return;}
            finish(0,{{"cancelled",cancelled},{"resumed",resumed},{"range_observation","no_ranges_reported"}});
        }else if(stage==4&&!rpc.ranges().isEmpty()){
            finish(0,{{"cancelled",cancelled},{"resumed",resumed}});
        }
    });
    timer.start();return app.exec();
}
