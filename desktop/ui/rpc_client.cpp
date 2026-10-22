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

RpcClient::RpcClient(QString path,QObject* parent):QObject(parent),path_(path) {
    retry_.setInterval(1000);
    connect(&retry_,&QTimer::timeout,this,[this]{
        if(socket_.state()==QLocalSocket::UnconnectedState) socket_.connectToServer(path_);
    });
    connect(&socket_,&QLocalSocket::connected,this,[this]{
        struct ucred peer{}; socklen_t size=sizeof(peer);
        if(getsockopt(socket_.socketDescriptor(),SOL_SOCKET,SO_PEERCRED,&peer,&size)!=0 || peer.uid!=getuid()){
            socket_.abort(); fail("peer_identity_mismatch","agent 必须由当前用户运行"); return;
        }
        startSession();
    });
    connect(&socket_,&QLocalSocket::errorOccurred,this,[this](QLocalSocket::LocalSocketError){
        fail("socket_error","无法连接本地 agent："+socket_.errorString()+"；正在重试",true);
    });
    connect(&socket_,&QLocalSocket::disconnected,this,[this]{
        buffer_.clear(); pending_.clear(); apiCompatible_=false; contractReady_=false; stale_=true;
        busy_=false; validation_.clear(); actionPending_.clear();
        fail("socket_disconnected","agent 连接已断开；保留的任务快照已过期，正在重新连接",true);
    });
    connect(&socket_,&QLocalSocket::readyRead,this,&RpcClient::read);
    deadlines_.setInterval(500);
    connect(&deadlines_,&QTimer::timeout,this,[this]{
        const auto now=QDateTime::currentMSecsSinceEpoch();
        bool expired=false;
        for(auto it=pending_.begin();it!=pending_.end();){
            if(it->deadline<=now){it=pending_.erase(it); expired=true;}else ++it;
        }
        if(expired){
            busy_=false; actionPending_.clear();
            fail("request_timeout","agent 请求超时；写操作结果尚未确认，请刷新任务后再操作",true);
            refresh();
        }
    });
    deadlines_.start(); retry_.start(); socket_.connectToServer(path_);
}
RpcClient::~RpcClient(){retry_.stop();deadlines_.stop();disconnect(&socket_,nullptr,this,nullptr);socket_.abort();}
void RpcClient::fail(QString code,QString text,bool retryable){
    lastErrorCode_=code;lastErrorRetryable_=retryable;message_=text;emit changed();
}
bool RpcClient::supports(QString method)const {
    if(!contractReady_)return false;
    for(const auto& row:contract_.value("methods").toList()){
        auto m=row.toMap();
        if(m.value("method").toString()==method)
            return m.value("support")=="supported" || (demoAdapter() && m.value("support")=="simulated");
    }
    return false;
}
QString RpcClient::localPath(QString url)const {
    QUrl u(url);return u.isLocalFile()?u.toLocalFile():url;
}
void RpcClient::startSession(){
    stale_=true;apiCompatible_=false;contractReady_=false;contract_.clear();validation_.clear();
    pending_.clear();message_="正在校验 API 与 V3 合同";send("agent.getInfo",{});emit changed();
}
void RpcClient::sendPostHandshake(QString m,QJsonObject p){
    if(!connected()||stale_||!supports(m)){
        fail("operation_unavailable","操作暂不可用："+m+"；请检查连接与 agent 合同");return;
    }
    send(m,p);
}
void RpcClient::send(QString m,QJsonObject p){
    if(!connected()){fail("socket_disconnected","agent 未连接",true);return;}
    if(pending_.size()>=64){fail("request_limit","请求过多，请稍后",true);return;}
    QString id=QString::number(++request_);
    pending_[id]={m,p,QDateTime::currentMSecsSinceEpoch()+30000};
    socket_.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",m},{"params",p}})
        .toJson(QJsonDocument::Compact)+"\n");
}
void RpcClient::upsert(QJsonObject t){
    const QString id=t["transfer_id"].toString();if(id.isEmpty())return;
    for(auto& old:tasks_)if(old.toMap().value("transfer_id").toString()==id){
        if(old.toMap().value("revision").toLongLong()<=t["revision"].toInteger())old=t.toVariantMap();
        return;
    }
    tasks_.prepend(t.toVariantMap());
}
bool RpcClient::acceptWatermark(QJsonObject r){
    if(r["transfer_id"].toString()!=detailTransfer_)return false;
    if(!r["attempt_id"].isNull() && r["attempt_id"].toString()!=detailAttempt_)return false;
    QString *snapshot=&detailAttemptSnapshot_,*epoch=&detailAttemptEpoch_;
    qint64 *revision=&detailAttemptRevision_,*sequence=&detailAttemptSequence_;
    if(r["kind"]=="file_list"){snapshot=&detailFileSnapshot_;epoch=&detailFileEpoch_;revision=&detailFileRevision_;sequence=&detailFileSequence_;}
    if(r["kind"]=="range_list"){snapshot=&detailRangeSnapshot_;epoch=&detailRangeEpoch_;revision=&detailRangeRevision_;sequence=&detailRangeSequence_;}
    if(snapshot->isEmpty()){
        *snapshot=r["snapshot_id"].toString();*epoch=r["agent_epoch"].toString();
        *revision=r["revision"].toInteger();*sequence=r["snapshot_sequence"].toInteger();return true;
    }
    if(r["snapshot_id"].toString()!=*snapshot || r["agent_epoch"].toString()!=*epoch ||
       r["revision"].toInteger()!=*revision || r["snapshot_sequence"].toInteger()!=*sequence){
        detailStatus_="分页快照不一致，请刷新详情";return false;
    }
    return true;
}
void RpcClient::handlePage(QJsonObject r){
    const auto k=r["kind"].toString();
    if(k=="range_list" && (r["file_id"].toString()!=detailFile_ || r["generation"].toInt()!=detailGeneration_))return;
    if(!acceptWatermark(r))return;
    if(k=="attempt_list"){attempts_+=r["items"].toArray().toVariantList();detailCursorAttempts_=r["next_cursor"].toString();}
    if(k=="file_list"){files_+=r["items"].toArray().toVariantList();detailCursorFiles_=r["next_cursor"].toString();}
    if(k=="range_list"){
        if(r["file_id"].toString()!=detailFile_ || r["generation"].toInt()!=detailGeneration_)return;
        ranges_+=r["items"].toArray().toVariantList();detailCursorRanges_=r["next_cursor"].toString();
    }
    detailStatus_=r["observation"].toString()+" · "+r["coverage"].toString();
}
void RpcClient::read(){
    buffer_+=socket_.readAll();
    while(buffer_.contains('\n')){
        int at=buffer_.indexOf('\n');
        if(at>1024*1024){socket_.abort();fail("frame_too_large","agent 单条消息超过限制");return;}
        auto line=buffer_.left(at);buffer_.remove(0,at+1);
        QJsonParseError error;auto doc=QJsonDocument::fromJson(line,&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()){
            socket_.abort();fail("invalid_response","agent 返回了无效 JSON");return;
        }
        const auto m=doc.object();
        if(m["method"]=="transfer.event"){
            auto e=m["params"].toObject();auto seq=e["sequence"].toInteger();
            if(!contractReady_||stale_)continue;
            if(e["agent_epoch"].toString()!=epoch_ || seq>sequence_+1){refresh();continue;}
            if(seq<=sequence_)continue;sequence_=seq;
            if(e["type"]=="transfer.state")upsert(e["payload"].toObject());
            emit changed();continue;
        }
        auto id=m["id"].toString();if(!pending_.contains(id))continue;
        const auto req=pending_.take(id);const auto method=req.method;
        if(m.contains("error")){
            auto e=m["error"].toObject(),d=e["data"].toObject();
            if(method.startsWith("transfer.")){busy_=false;actionPending_.clear();}
            if(method=="transfer.create")creationUncertain_=false;
            if(method=="transfer.validate"){validation_.clear();pendingCreate_=QJsonObject{};}
            if(d["code"]=="cursor_expired")detailStatus_="分页已过期，请刷新详情";
            fail(d["code"].toString(),method+"："+e["message"].toString(),d["retryable"].toBool());continue;
        }
        auto r=m["result"].toObject();auto kind=r["kind"].toString();
        if(method=="agent.getInfo"){
            if(!agentInfo_.isEmpty() && agentInfo_.value("adapter").toString()!=r["adapter"].toString()){
                tasks_.clear();profiles_.clear();attempts_.clear();files_.clear();ranges_.clear();evidence_.clear();
            }
            agentInfo_=r.toVariantMap();apiCompatible_=r["api_version"]=="3.0.0";
            if(!apiCompatible_){fail("api_mismatch","需要 API 3.0.0，实际为 "+r["api_version"].toString());continue;}
            send("agent.getContract",{});
        }else if(method=="agent.getContract"){
            contract_=r.toVariantMap();
            contractReady_=kind=="agent_contract" && r["api_version"]=="3.0.0" &&
                r["runtime_protocol"]=="v3" && r["adapter"]==agentInfo_.value("adapter").toString();
            if(!contractReady_){fail("contract_mismatch","agent 合同的 API、V3 协议或 adapter 身份不匹配");continue;}
            message_="合同已确认，正在恢复任务";
            send("profile.list",{});refresh();
        }else if(kind=="subscription"){
            epoch_=r["agent_epoch"].toString();sequence_=r["sequence"].toInteger();
            if(r["reset"].toBool()){
                tasks_.clear();for(const auto& t:r["snapshots"].toArray())upsert(t.toObject());
            }
            stale_=false;lastErrorCode_.clear();message_="任务已与 agent 同步";
        }else if(kind=="profile_list"){profiles_=r["profiles"].toArray().toVariantList();
        }else if(kind=="profile"){
            send("profile.list",{});message_="节点配置已保存，等待连接检查";
        }else if(kind=="transfer_validation"){
            busy_=false;validation_=r.toVariantMap();message_="预检完成，请核对解析后的策略";
        }else if(kind=="transfer"||kind=="operation"){
            auto t=r["snapshot"].toObject();upsert(t);
            if(method=="transfer.create"){
                creationUncertain_=false;
                pendingCreate_=QJsonObject{};validation_.clear();
                if(t["state"]=="queued"){
                    actionPending_="正在请求启动";send("transfer.start",{{"transfer_id",t["transfer_id"]},
                        {"operation_id",operation()},{"expected_revision",t["revision"]}});
                }else{busy_=false;actionPending_.clear();}
                emit pageRequested(0);
            }else{busy_=false;actionPending_.clear();message_="agent 已返回操作结果；终态以任务快照为准";}
        }else if(kind=="transfer_evidence"){
            if(req.params["transfer_id"].toString()==detailTransfer_ && req.params["attempt_id"].toString()==detailAttempt_)
                evidence_=r.toVariantMap();
        }else if(kind=="attempt_list"||kind=="file_list"||kind=="range_list"){
            if(req.params["transfer_id"].toString()==detailTransfer_ && (method=="attempt.list" || req.params["attempt_id"].toString()==detailAttempt_))
                handlePage(r);
        }else if(kind=="network_inventory")networkInventory_=r.toVariantMap();
        else if(kind=="network_plan")networkPlan_=r.toVariantMap();
        else if(kind=="network_apply"){networkApply_=r.toVariantMap();send("node.network.get",{{"node_id",r["node_id"]}});}
        else if(kind=="network_configuration"){networkBinding_=r.toVariantMap();networkVerify_.clear();}
        else if(kind=="network_verify")networkVerify_=r.toVariantMap();
        else if(kind=="connection_test"){checks_=r["checks"].toObject().toVariantMap();authenticationState_=r["authentication"].toString();}
        emit changed();
    }
    if(buffer_.size()>1024*1024){socket_.abort();fail("frame_too_large","agent 单条消息超过限制");}
}
void RpcClient::refresh(){
    if(!connected()){socket_.abort();socket_.connectToServer(path_);return;}
    if(!apiCompatible_||!contractReady_){startSession();return;}
    for(const auto& req:pending_)if(req.method=="transfer.subscribe")return;
    stale_=true;
    // A reset subscription is required even on a fresh UI with sequence zero.
    send("transfer.subscribe",{{"transfer_ids",QJsonArray{}},{"agent_epoch",QJsonValue()},{"after_sequence",QJsonValue()}});
    emit changed();
}
void RpcClient::loadDetails(QString t,QString a){
    if(!contractReady_||stale_)return;
    for(auto it=pending_.begin();it!=pending_.end();){
        if(it->method=="transfer.evidence"||it->method=="attempt.list"||it->method=="file.list"||it->method=="range.list")it=pending_.erase(it);
        else ++it;
    }
    detailTransfer_=t;detailAttempt_=a;detailFile_.clear();detailGeneration_=0;
    evidence_.clear();attempts_.clear();files_.clear();ranges_.clear();
    detailCursorFiles_.clear();detailCursorRanges_.clear();detailCursorAttempts_.clear();
    detailAttemptSnapshot_.clear();detailFileSnapshot_.clear();detailRangeSnapshot_.clear();
    detailStatus_="正在查询最终观测明细";
    sendPostHandshake("transfer.evidence",{{"transfer_id",t},{"attempt_id",a}});
    sendPostHandshake("attempt.list",{{"transfer_id",t},{"cursor",QJsonValue()},{"limit",64}});
    sendPostHandshake("file.list",{{"transfer_id",t},{"attempt_id",a},{"cursor",QJsonValue()},{"limit",64}});
    emit changed();
}
void RpcClient::loadNextAttempts(){
    if(detailCursorAttempts_.isEmpty())return;
    for(const auto& r:pending_)if(r.method=="attempt.list")return;
    sendPostHandshake("attempt.list",{{"transfer_id",detailTransfer_},{"cursor",detailCursorAttempts_},{"limit",64}});
}
void RpcClient::loadNextFiles(){
    if(detailCursorFiles_.isEmpty())return;
    for(const auto& r:pending_)if(r.method=="file.list")return;
    sendPostHandshake("file.list",{{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"cursor",detailCursorFiles_},{"limit",64}});
}
void RpcClient::selectFile(QVariantMap file){
    for(auto it=pending_.begin();it!=pending_.end();){
        if(it->method=="range.list")it=pending_.erase(it);else ++it;
    }
    detailFile_=file.value("file_id").toString();detailGeneration_=file.value("generation").toInt();
    ranges_.clear();detailCursorRanges_.clear();detailRangeSnapshot_.clear();
    sendPostHandshake("range.list",{{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"file_id",detailFile_},
        {"generation",detailGeneration_},{"cursor",QJsonValue()},{"limit",64}});
    emit changed();
}
void RpcClient::loadNextRanges(){
    if(detailCursorRanges_.isEmpty())return;
    for(const auto& r:pending_)if(r.method=="range.list")return;
    sendPostHandshake("range.list",{{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"file_id",detailFile_},
        {"generation",detailGeneration_},{"cursor",detailCursorRanges_},{"limit",64}});
}
void RpcClient::invalidateDraft(){if(!busy_&&!creationUncertain_){validation_.clear();pendingCreate_=QJsonObject{};emit changed();}}
void RpcClient::create(QString dir,QString local,QString remote,bool failure,QString mode,QString profileId,QVariantMap overrides){
    Q_UNUSED(failure);Q_UNUSED(mode);
    if(busy_||creationUncertain_){fail("request_pending","上次预检或创建结果尚未确认，请先核对原请求");return;}
    if(!supports("transfer.validate")||stale_){fail("operation_unavailable","agent 尚未就绪");return;}
    const QFileInfo info(local);
    if(!demoAdapter()&&(!info.isAbsolute()||!info.exists()||!info.isDir())){
        fail("invalid_path","请选择已存在的本地绝对目录；独立单文件认证尚未支持");return;
    }
    if(remote.startsWith("/")||remote.split('/').contains("..")||remote.contains(QChar::Null)||remote.isEmpty()){
        fail("invalid_remote_path","远端路径必须位于授权 root 内，不能是绝对路径或包含 ..");return;
    }
    QVariantMap profile;
    for(const auto& p:profiles_)if(p.toMap().value("node_id")==profileId)profile=p.toMap();
    if(!demoAdapter()&&profile.isEmpty()){fail("profile_required","请先保存并选择远端节点");return;}
    const QString node=demoAdapter()?"demo":profile.value("node_id").toString();
    const QString root=demoAdapter()?"demo-root":profile.value("default_root_id").toString();
    QJsonObject l{{"kind","local"},{"path",local},{"node_id",QJsonValue()},{"root_id",QJsonValue()}};
    QJsonObject r{{"kind","remote"},{"path",remote},{"node_id",node},{"root_id",root}};
    QJsonObject policy=QJsonObject::fromVariantMap(contract_.value("policy_defaults").toMap());
    if(policy.isEmpty()){fail("contract_mismatch","agent 没有提供默认策略");return;}
    for(auto it=overrides.constBegin();it!=overrides.constEnd();++it)policy[it.key()]=QJsonValue::fromVariant(it.value());
    pendingCreate_={{"operation_id",operation()},{"direction",dir},{"source",dir=="upload"?l:r},
        {"destination",dir=="upload"?r:l},{"policy",policy}};
    auto p=pendingCreate_;p.remove("operation_id");validation_.clear();busy_=true;
    send("transfer.validate",p);message_="正在执行 agent 预检";emit changed();
}
void RpcClient::confirmCreate(){
    if(busy_||pendingCreate_.isEmpty()||(!creationUncertain_&&!validation_.value("valid").toBool())||stale_)return;
    if(!supports("transfer.create")||!supports("transfer.start")){
        fail("operation_unavailable","agent 尚未提供创建或启动能力");return;
    }
    busy_=true;creationUncertain_=true;actionPending_="正在创建或核对原任务";sendPostHandshake("transfer.create",pendingCreate_);emit changed();
}
void RpcClient::action(QString method,QVariantMap t){
    if(!actionPending_.isEmpty()||stale_||!supports(method))return;
    QJsonObject p{{"transfer_id",t.value("transfer_id").toString()},{"operation_id",operation()},
        {"expected_revision",t.value("revision").toLongLong()}};
    if(method=="transfer.resume")p["checkpoint_id"]=QJsonValue::fromVariant(t.value("recovery").toMap().value("checkpoint_id"));
    actionPending_=method=="transfer.cancel"?"取消请求中（等待 agent 确认）":method=="transfer.start"?"启动请求中":"恢复请求中";
    sendPostHandshake(method,p);emit changed();
}
void RpcClient::inspectNetwork(QString node){
    networkInventory_.clear();networkPlan_.clear();networkBinding_.clear();networkVerify_.clear();
    sendPostHandshake("node.network.get",{{"node_id",node}});
    sendPostHandshake("node.network.inventory",{{"node_id",node}});
}
void RpcClient::planNetwork(QString node,QString revision,int links,QVariantList peers,QVariantList selections){
    sendPostHandshake("node.network.plan",{{"node_id",node},{"inventory_revision",revision},{"requested_links",links},
        {"peer_addresses",QJsonArray::fromVariantList(peers)},{"selections",QJsonArray::fromVariantList(selections)}});
}
void RpcClient::applyNetwork(QString node,QString plan,QString revision,bool confirm){
    sendPostHandshake("node.network.apply",{{"operation_id",operation()},{"node_id",node},{"plan_id",plan},{"inventory_revision",revision},{"confirm",confirm}});
}
void RpcClient::clearNetwork(QString node,QString plan){
    sendPostHandshake("node.network.clear",{{"operation_id",operation()},{"node_id",node},{"expected_plan_id",plan},{"confirm",true}});
    sendPostHandshake("node.network.get",{{"node_id",node}});
}
void RpcClient::verifyNetwork(QString node,QString plan){sendPostHandshake("node.network.verify",{{"node_id",node},{"plan_id",plan}});}
void RpcClient::invalidateAuthentication(){checks_.clear();authenticationState_="unknown";emit changed();}
void RpcClient::saveProfile(QVariantMap p,bool update){
    invalidateAuthentication();sendPostHandshake(update?"profile.update":"profile.create",{{"operation_id",operation()},{"profile",QJsonObject::fromVariantMap(p)}});
}
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
void RpcClient::demo(){if(demoAdapter())create("upload","/tmp/demo","demo",false,"v3","",{});}
