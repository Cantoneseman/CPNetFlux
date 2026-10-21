#include "rpc_client.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QUrl>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QtGlobal>
#include <sys/socket.h>
#include <unistd.h>
static QString operation(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
RpcClient::RpcClient(QString path,QObject* parent):QObject(parent),path_(path){retry_.setInterval(1000);connect(&retry_,&QTimer::timeout,this,[this]{if(socket_.state()==QLocalSocket::UnconnectedState)socket_.connectToServer(path_);});connect(&socket_,&QLocalSocket::connected,this,[this]{struct ucred peer{};socklen_t size=sizeof(peer);if(getsockopt(socket_.socketDescriptor(),SOL_SOCKET,SO_PEERCRED,&peer,&size)!=0||peer.uid!=getuid()){message_="agent 身份校验失败";socket_.abort();emit changed();return;}startSession();});connect(&socket_,&QLocalSocket::disconnected,this,[this]{buffer_.clear();pending_.clear();apiCompatible_=false;stale_=true;message_="agent 离线，当前快照已标记 stale；正在重连";emit changed();});connect(&socket_,&QLocalSocket::readyRead,this,&RpcClient::read);retry_.start();socket_.connectToServer(path_);}
RpcClient::~RpcClient(){retry_.stop();disconnect(&socket_,nullptr,this,nullptr);socket_.abort();}
void RpcClient::startSession(){stale_=true;apiCompatible_=false;pending_.clear();message_="正在校验 desktop-agent API 2.1.0";send("agent.getInfo",{});}
void RpcClient::sendPostHandshake(QString m,QJsonObject p){if(!apiCompatible_){message_="agent API 未兼容，已阻止 "+m;emit changed();return;}send(m,p);}
void RpcClient::send(QString m,QJsonObject p){if(!connected()){message_="agent 未连接";emit changed();return;}if(pending_.size()>64){message_="请求过多，请稍后";emit changed();return;}QString id=QString::number(++request_);pending_[id]=m;socket_.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",m},{"params",p}}).toJson(QJsonDocument::Compact)+"\n");}
void RpcClient::upsert(QJsonObject t){QString id=t["transfer_id"].toString();for(auto& old:tasks_)if(old.toMap().value("transfer_id").toString()==id){if(old.toMap().value("revision").toLongLong()<=t["revision"].toInteger())old=t.toVariantMap();emit changed();return;}tasks_.prepend(t.toVariantMap());emit changed();}
bool RpcClient::acceptWatermark(QJsonObject r){QString transfer=r["transfer_id"].toString(),attempt=r["attempt_id"].toString();if(transfer!=detailTransfer_||(!attempt.isEmpty()&&attempt!=detailAttempt_)){detailStatus_="旧 attempt/generation 页面已丢弃";return false;}QString e=r["agent_epoch"].toString();qint64 rev=r["revision"].toInteger(),seq=r["snapshot_sequence"].toInteger();if(detailSnapshot_.isEmpty()){detailSnapshot_=r["snapshot_id"].toString();detailEpoch_=e;detailRevision_=rev;detailSequence_=seq;return true;}if(r["snapshot_id"].toString()!=detailSnapshot_||e!=detailEpoch_||rev!=detailRevision_||seq!=detailSequence_){detailStatus_="分页 watermark 不一致，请刷新详情";return false;}return true;}
void RpcClient::handlePage(QJsonObject r){if(!acceptWatermark(r)){emit changed();return;}QString k=r["kind"].toString();if(k=="attempt_list")attempts_+=r["items"].toArray().toVariantList();else if(k=="file_list"){files_+=r["items"].toArray().toVariantList();detailCursorFiles_=r["next_cursor"].toString();detailFile_=files_.isEmpty()?QString():files_.last().toMap().value("file_id").toString();}else if(k=="range_list"){ranges_+=r["items"].toArray().toVariantList();detailCursorRanges_=r["next_cursor"].toString();}detailStatus_=(detailCursorFiles_.isEmpty()&&detailCursorRanges_.isEmpty())?"分页完整":"分页部分加载";emit changed();}
void RpcClient::read(){buffer_+=socket_.readAll();if(buffer_.size()>1024*1024){message_="agent 消息超长";socket_.abort();emit changed();return;}while(buffer_.contains('\n')){int at=buffer_.indexOf('\n');QByteArray line=buffer_.left(at);buffer_.remove(0,at+1);QJsonParseError er;auto d=QJsonDocument::fromJson(line,&er);if(er.error!=QJsonParseError::NoError||!d.isObject()){message_="agent 响应 JSON 无效";socket_.abort();return;}auto m=d.object();if(m["method"].toString()=="transfer.event"){auto p=m["params"].toObject();qint64 seq=p["sequence"].toInteger();if(!apiCompatible_)continue;if(p["agent_epoch"].toString()!=epoch_){stale_=true;refresh();continue;}if(seq<=sequence_)continue;if(seq!=sequence_+1){stale_=true;refresh();continue;}sequence_=seq;if(p["type"].toString()=="transfer.state"){auto payload=p["payload"].toObject();if(detailAttempt_.isEmpty()||payload["attempt"].toObject()["attempt_id"].toString()==detailAttempt_)upsert(payload);}continue;}QString method=pending_.take(m["id"].toString());if(method.isEmpty())continue;if(m.contains("error")){auto e=m["error"].toObject();auto x=e["data"].toObject();message_=e["message"].toString();if(x["code"].toString()=="cursor_expired")detailStatus_="分页已过期，请刷新详情";emit changed();continue;}auto r=m["result"].toObject();QString kind=r["kind"].toString();if(method=="agent.getInfo"||kind=="agent_info"){agentInfo_=r.toVariantMap();apiCompatible_=r["api_version"].toString()=="2.1.0";if(!apiCompatible_){message_=QString("agent API 不兼容：需要 2.1.0，实际 ")+r["api_version"].toString();emit changed();continue;}epoch_=r["agent_epoch"].toString();stale_=true;message_=QString("agent API 2.1.0 已确认（")+(demoAdapter()?"开发诊断/合成数据":"真实 adapter")+"）";send("transfer.subscribe",{{"transfer_ids",QJsonArray{}},{"agent_epoch",epoch_},{"after_sequence",sequence_}});send("profile.list",{});if(demoAdapter())send("node.capabilities",{{"node_id","demo"}});}else if(kind=="subscription"){if(r["agent_epoch"].toString()!=epoch_){stale_=true;message_="agent epoch 已变化，请重新同步";refresh();continue;}sequence_=r["sequence"].toInteger();if(r["reset"].toBool()){tasks_.clear();for(const auto&t:r["snapshots"].toArray())upsert(t.toObject());}stale_=false;}else if(kind=="transfer"||kind=="operation")upsert(r["snapshot"].toObject());else if(kind=="profile_list"){profiles_=r["profiles"].toArray().toVariantList();if(!demoAdapter()&&!profiles_.isEmpty())send("node.capabilities",{{"node_id",profiles_.first().toMap().value("node_id").toString()}});}else if(kind=="profile"){sendPostHandshake("profile.list",{});message_="节点配置已保存；凭据仅保存引用，等待 agent 进行真实 TLS/认证检查";}else if(kind=="capabilities")capabilities_=r.toVariantMap();else if(kind=="network_inventory")networkInventory_=r.toVariantMap();else if(kind=="network_plan")networkPlan_=r.toVariantMap();else if(kind=="network_apply")networkApply_=r.toVariantMap();else if(kind=="network_verify")networkVerify_=r.toVariantMap();else if(kind=="connection_test"){checks_=r["checks"].toObject().toVariantMap();authenticationState_=r["authentication"].toString();}else if(kind=="attempt_list"||kind=="file_list"||kind=="range_list")handlePage(r);emit changed();}}
void RpcClient::refresh(){if(!connected())return;if(!apiCompatible_){startSession();return;}stale_=true;detailStatus_="正在刷新详情";epoch_.clear();sequence_=0;send("transfer.subscribe",{{"transfer_ids",QJsonArray{}},{"agent_epoch",QJsonValue()},{"after_sequence",QJsonValue()}});}
void RpcClient::loadDetails(QString t,QString a){if(!apiCompatible_){detailStatus_="agent API 未兼容";emit changed();return;}detailTransfer_=t;detailAttempt_=a;detailSnapshot_.clear();detailEpoch_.clear();detailRevision_=-1;detailSequence_=-1;detailCursorFiles_.clear();detailCursorRanges_.clear();attempts_.clear();files_.clear();ranges_.clear();detailFile_.clear();detailStatus_="正在加载详情分页";sendPostHandshake("attempt.list",{{"transfer_id",t},{"attempt_id",a},{"cursor",QJsonValue()},{"limit",64}});sendPostHandshake("file.list",{{"transfer_id",t},{"attempt_id",a},{"cursor",QJsonValue()},{"limit",64}});emit changed();}
void RpcClient::loadNextFiles(){if(!detailCursorFiles_.isEmpty())sendPostHandshake("file.list",{{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"cursor",detailCursorFiles_},{"limit",64}});}
void RpcClient::loadNextRanges(){if(detailFile_.isEmpty())return;QJsonValue cursor=detailCursorRanges_.isEmpty()?QJsonValue():QJsonValue(detailCursorRanges_);sendPostHandshake("range.list",{{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"file_id",detailFile_},{"generation",1},{"cursor",cursor},{"limit",64}});}
void RpcClient::create(QString dir,QString local,QString remote,bool failure,QString mode,QString profileId){if(!apiCompatible_){message_="agent API 未兼容，已阻止创建";emit changed();return;}QFileInfo info(local);if(!info.isAbsolute()||!info.exists()){message_="本地文件/目录必须存在且使用绝对路径";emit changed();return;}QString node="";QString root="";if(!demoAdapter()&&!profiles_.isEmpty()){QVariantMap selected;for(const auto& item:profiles_)if(item.toMap().value("node_id").toString()==profileId){selected=item.toMap();break;}if(selected.isEmpty())selected=profiles_.first().toMap();node=selected.value("node_id").toString();root=selected.value("default_root_id").toString();}if(node.isEmpty())node="demo";if(root.isEmpty())root="demo-root";QJsonObject l{{"kind","local"},{"path",local},{"node_id",QJsonValue()},{"root_id",QJsonValue()}},r{{"kind","remote"},{"path",failure?"demo-fail":remote},{"node_id",node},{"root_id",root}},policy{{"requested_mode",demoAdapter()?mode:"v2"},{"scheduler",demoAdapter()?"auto":"dynamic_ring"},{"channel_count",1},{"pending_window",2},{"queue_depth",256},{"range_policy",demoAdapter()?"auto":"file"},{"checksum_policy","required_crc32c"},{"resume_policy","allow"},{"control_tls","verify_ca"},{"data_tls","required"},{"fallback_policy","allow_compatible"},{"conflict_policy","fail"},{"experimental_raw_ack",false}};sendPostHandshake("transfer.create",{{"operation_id",operation()},{"direction",dir},{"source",dir=="upload"?l:r},{"destination",dir=="upload"?r:l},{"policy",policy}});}
void RpcClient::action(QString m,QVariantMap t){if(!apiCompatible_){message_="agent API 未兼容，已阻止操作";emit changed();return;}QJsonObject p{{"transfer_id",t.value("transfer_id").toString()},{"operation_id",operation()},{"expected_revision",t.value("revision").toLongLong()}};if(m=="transfer.resume")p["checkpoint_id"]=t.value("recovery").toMap().value("checkpoint_id").toString();sendPostHandshake(m,p);}
void RpcClient::inspectNetwork(QString node){ sendPostHandshake("node.network.inventory",{{"node_id",node}}); }
void RpcClient::planNetwork(QString node,QString revision,int links,QVariantList peers){
    QJsonArray peerArray; for(const auto& value:peers) peerArray.append(QJsonValue::fromVariant(value));
    sendPostHandshake("node.network.plan",{{"node_id",node},{"inventory_revision",revision},{"requested_links",links},{"peer_addresses",peerArray}});
}
void RpcClient::applyNetwork(QString node,QString plan,QString revision,bool confirm){
    sendPostHandshake("node.network.apply",{{"operation_id",operation()},{"node_id",node},{"plan_id",plan},{"inventory_revision",revision},{"confirm",confirm}});
}
void RpcClient::verifyNetwork(QString node,QString plan){ sendPostHandshake("node.network.verify",{{"node_id",node},{"plan_id",plan}}); }
void RpcClient::invalidateAuthentication(){checks_.clear();authenticationState_="unknown";emit changed();}
void RpcClient::saveProfile(QVariantMap p,bool update){invalidateAuthentication();sendPostHandshake(update?"profile.update":"profile.create",{{"operation_id",operation()},{"profile",QJsonObject::fromVariantMap(p)}});}
static bool writePrivateFile(const QString& path, const QByteArray& data, QString& error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { error = "无法写入私有凭据文件"; return false; }
    if (file.write(data) != data.size() || !file.commit()) { error = "无法提交私有凭据文件"; return false; }
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        error = "无法设置私有凭据文件权限"; return false;
    }
    return true;
}

void RpcClient::importProvisioningBundle(QString inputPath) {
    QString path = QUrl::fromUserInput(inputPath).isLocalFile() ? QUrl::fromUserInput(inputPath).toLocalFile() : inputPath;
    QFileInfo source(path);
    if (!source.isFile() || source.size() <= 0 || source.size() > 1024 * 1024 ||
        (source.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther))) {
        message_ = "节点包不存在、过大或权限过宽（需要普通文件且不可组/其他用户写入）"; emit changed(); return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { message_ = "节点包不可读"; emit changed(); return; }
    QJsonParseError parseError{}; QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { message_ = "节点包 JSON 无效"; emit changed(); return; }
    QJsonObject bundle = doc.object();
    if (bundle.value("format").toString() != "cpnetflux-node-provisioning" || bundle.value("version").toInt() != 1) {
        message_ = "节点包版本不受支持"; emit changed(); return;
    }
    QJsonObject node = bundle.value("node").toObject(), tls = bundle.value("tls").toObject(), credential = bundle.value("credential").toObject();
    QString nodeId = node.value("node_id").toString(), name = node.value("name").toString(), host = node.value("host").toString();
    QString serverName = node.value("server_name").toString(), rootId = node.value("default_root_id").toString();
    QJsonValue portValue = node.value("control_port"); bool portOk = portValue.isDouble() && portValue.toDouble() >= 0 && portValue.toDouble() == qFloor(portValue.toDouble()); int controlPort = portValue.toInt();
    static const QRegularExpression safeId("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$");
    if (!safeId.match(nodeId).hasMatch() || name.isEmpty() || name.size() > 128 || host.isEmpty() || host.contains(QRegularExpression("\\s")) ||
        serverName.isEmpty() || serverName.contains(QRegularExpression("[\\s/\\\\,]")) || !safeId.match(rootId).hasMatch() || !portOk || controlPort < 1024 || controlPort > 65535 ||
        tls.value("mode").toString() != "required" || tls.value("server_name").toString() != serverName ||
        !tls.value("ca_pem").toString().contains("BEGIN CERTIFICATE" ) || credential.value("type").toString() != "token") {
        message_ = "节点包字段或安全策略无效"; emit changed(); return;
    }
    QString token = credential.value("token").toString();
    if (token.isEmpty() || token.contains(QRegularExpression("[\\r\\n]"))) { message_ = "节点包 token 无效"; emit changed(); return; }
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty()) configHome = QDir::homePath() + "/.config";
    QString nodeDir = QDir(configHome).filePath("cpnetflux/nodes/" +
        QString::fromLatin1(QCryptographicHash::hash(nodeId.toUtf8(), QCryptographicHash::Sha256).toHex()));
    if (!QDir().mkpath(nodeDir) || !QFile::setPermissions(nodeDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        message_ = "无法创建当前用户的私有节点目录"; emit changed(); return;
    }
    QString caPath = QDir(nodeDir).filePath("ca.pem"), tokenPath = QDir(nodeDir).filePath("token"); QString error;
    if (!writePrivateFile(caPath, tls.value("ca_pem").toString().toUtf8(), error) || !writePrivateFile(tokenPath, token.toUtf8(), error)) {
        message_ = error; emit changed(); return;
    }
    QVariantMap profile{{"node_id",nodeId},{"name",name},{"host",host},{"control_port",controlPort},
                        {"server_name",serverName},{"ca_path",caPath},{"auth_mode","token"},
                        {"credential_ref","file:" + tokenPath},{"default_root_id",rootId}};
    bool update = false; for (const auto& item : profiles_) if (item.toMap().value("node_id").toString() == nodeId) { update = true; break; }
    saveProfile(profile, update); message_ = "节点包已导入；CA 与 token 已保存到当前用户私有目录，正在同步节点配置"; emit changed();
}

void RpcClient::testNode(QString id,QString root){checks_={{"dns_control_port",QVariantMap{{"state","pending"}}},{"tls_identity",QVariantMap{{"state","pending"}}},{"login",QVariantMap{{"state","pending"}}},{"capabilities",QVariantMap{{"state","pending"}}},{"directory_permission",QVariantMap{{"state","unsupported"}}},{"data_port",QVariantMap{{"state","unsupported"}}}};authenticationState_="authenticating";emit changed();sendPostHandshake("node.testConnection",{{"node_id",id},{"root_id",root},{"path",""},{"access","read"}});}
void RpcClient::demo(){create("upload","/home/student/demo","演示数据",false,"auto","");}
