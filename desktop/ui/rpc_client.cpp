#include "rpc_client.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <sys/socket.h>
#include <unistd.h>
static QString operation() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
RpcClient::RpcClient(QString path, QObject* parent):QObject(parent),path_(path) {
    retry_.setInterval(1000);
    connect(&retry_, &QTimer::timeout, this, [this] { if(socket_.state()==QLocalSocket::UnconnectedState) socket_.connectToServer(path_); });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        struct ucred peer{}; socklen_t size=sizeof(peer);
        if(getsockopt(socket_.socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &peer, &size)!=0 || peer.uid!=getuid()) {
            message_="agent身份校验失败"; socket_.abort(); emit changed(); return;
        }
        message_="已连接本地 agent；演示不传输真实文件";
        send("transfer.subscribe", {{"transfer_ids",QJsonArray{}},{"agent_epoch",epoch_.isEmpty()?QJsonValue():QJsonValue(epoch_)},{"after_sequence",epoch_.isEmpty()?QJsonValue():QJsonValue(sequence_)}});
        send("profile.list",{}); send("node.capabilities",{{"node_id","demo"}}); emit changed();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] { buffer_.clear(); pending_.clear(); message_="agent离线，显示上次快照；正在重连"; emit changed(); });
    connect(&socket_, &QLocalSocket::readyRead,this,&RpcClient::read);
    retry_.start(); socket_.connectToServer(path_);
}
RpcClient::~RpcClient() {
    retry_.stop();
    disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
}
void RpcClient::send(QString method, QJsonObject params) {
    if(!connected()) { message_="agent未连接"; emit changed(); return; }
    if(pending_.size()>64) { message_="请求过多，请稍后"; emit changed(); return; }
    const QString id=QString::number(++request_); pending_[id]=method;
    socket_.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+"\n");
}
void RpcClient::upsert(QJsonObject t) {
    for(auto& old:tasks_) if(old.toMap().value("transfer_id").toString()==t["transfer_id"].toString()) {
        if(old.toMap().value("revision").toLongLong()<=t["revision"].toInteger()) old=t.toVariantMap();
        emit changed(); return;
    }
    tasks_.prepend(t.toVariantMap()); emit changed();
}
void RpcClient::read() {
    buffer_+=socket_.readAll();
    if(buffer_.size()>1024*1024) { message_="agent消息超长"; socket_.abort(); emit changed(); return; }
    while(buffer_.contains('\n')) {
        const int at=buffer_.indexOf('\n'); const auto line=buffer_.left(at); buffer_.remove(0,at+1);
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(line,&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) { message_="agent响应JSON无效"; socket_.abort(); return; }
        const auto m=doc.object();
        if(m["method"].toString()=="transfer.event") {
            const auto p=m["params"].toObject(); const qint64 seq=p["sequence"].toInteger();
            if(p["agent_epoch"].toString()!=epoch_) { epoch_.clear(); refresh(); continue; }
            if(seq<=sequence_) continue;
            // All transfers subscribed, so sequence gaps require a new snapshot.
            if(seq!=sequence_+1) { epoch_.clear(); refresh(); continue; }
            sequence_=seq; if(p["type"].toString()=="transfer.state") upsert(p["payload"].toObject());
            continue;
        }
        const QString method=pending_.take(m["id"].toString());
        if(method.isEmpty()) continue;
        if(m.contains("error")) { message_=m["error"].toObject()["message"].toString(); emit changed(); continue; }
        const auto result=m["result"].toObject(); const QString kind=result["kind"].toString();
        if(kind=="subscription") {
            epoch_=result["agent_epoch"].toString(); sequence_=result["sequence"].toInteger();
            if(result["reset"].toBool()) { tasks_.clear(); for(const auto& t:result["snapshots"].toArray()) upsert(t.toObject()); }
        } else if(kind=="transfer" || kind=="operation") upsert(result["snapshot"].toObject());
        else if(kind=="profile_list") profiles_=result["profiles"].toArray().toVariantList();
        else if(kind=="profile") { send("profile.list",{}); message_="节点配置已保存；凭据仅保存引用，真实连接未接入"; }
        else if(kind=="capabilities") capabilities_=result.toVariantMap();
        else if(kind=="connection_test") { checks_=result["checks"].toObject().toVariantMap(); message_="真实服务器探测尚未接入"; }
        emit changed();
    }
}
void RpcClient::refresh() {
    epoch_.clear(); send("transfer.subscribe",{{"transfer_ids",QJsonArray{}},{"agent_epoch",QJsonValue()},{"after_sequence",QJsonValue()}}); send("profile.list",{});
}
void RpcClient::create(QString direction, QString local, QString remote, bool failure, QString mode) {
    if(!QFileInfo(local).isAbsolute()) { message_="本地目录必须是绝对路径"; emit changed(); return; }
    QJsonObject l{{"kind","local"},{"path",local},{"node_id",QJsonValue()},{"root_id",QJsonValue()}};
    QJsonObject r{{"kind","remote"},{"path",failure?"demo-fail":remote},{"node_id","demo"},{"root_id","demo-root"}};
    QJsonObject policy{{"requested_mode",mode},{"scheduler","auto"},{"channel_count","auto"},{"pending_window",2},{"queue_depth",256},{"range_policy","auto"},{"checksum_policy","required_crc32c"},{"resume_policy","allow"},{"control_tls","verify_ca"},{"data_tls","required"},{"fallback_policy","allow_compatible"},{"conflict_policy","fail"},{"experimental_raw_ack",false}};
    send("transfer.create",{{"operation_id",operation()},{"direction",direction},{"source",direction=="upload"?l:r},{"destination",direction=="upload"?r:l},{"policy",policy}});
}
void RpcClient::action(QString method,QVariantMap task) {
    QJsonObject p{{"transfer_id",task.value("transfer_id").toString()},{"operation_id",operation()},{"expected_revision",task.value("revision").toLongLong()}};
    if(method=="transfer.resume") p["checkpoint_id"]=task.value("recovery").toMap().value("checkpoint_id").toString();
    send(method,p);
}
void RpcClient::saveProfile(QVariantMap profile,bool update) { send(update?"profile.update":"profile.create",{{"operation_id",operation()},{"profile",QJsonObject::fromVariantMap(profile)}}); }
void RpcClient::testNode(QString id,QString root) { send("node.testConnection",{{"node_id",id},{"root_id",root},{"path",""},{"access","read"}}); }
void RpcClient::demo() { create("upload","/home/student/demo","演示数据",false,"auto"); }
