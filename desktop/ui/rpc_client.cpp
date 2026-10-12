#include "rpc_client.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QFileInfo>
#include <sys/socket.h>
#include <unistd.h>
#include <cmath>

static bool safeCount(QJsonValue value) {
    double n = value.toDouble(-1);
    return value.isDouble() && std::isfinite(n) && n >= 0 && n <= 9007199254740991.0 && std::floor(n) == n;
}

static QString operation() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

RpcClient::RpcClient(QString path, QObject* parent) : QObject(parent), path_(path) {
    retry_.setInterval(1000);
    connect(&retry_, &QTimer::timeout, this, [this] {
        if (socket_.state() == QLocalSocket::UnconnectedState) socket_.connectToServer(path_);
    });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        struct ucred peer{};
        socklen_t size = sizeof(peer);
        if (getsockopt(socket_.socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 || peer.uid != getuid()) {
            message_ = "agent 身份校验失败";
            socket_.abort();
            emit changed();
            return;
        }
        startSession();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        buffer_.clear();
        pending_.clear();
        subscription_.clear();
        syncInFlight_ = false;
        apiCompatible_ = false;
        stale_ = true;
        ++detailSerial_;
        message_ = "agent 离线，当前快照已标记 stale；正在重连";
        emit changed();
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &RpcClient::read);
    retry_.start();
    socket_.connectToServer(path_);
}
RpcClient::~RpcClient() {
    retry_.stop();
    disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
}
void RpcClient::startSession() {
    stale_ = true;
    apiCompatible_ = false;
    pending_.clear();
    subscription_.clear();
    syncInFlight_ = false;
    message_ = "正在校验 desktop-agent API 2.0.0";
    send("agent.getInfo", {});
    emit changed();
}
void RpcClient::sendPostHandshake(QString method, QJsonObject params) {
    if (!apiCompatible_ || stale_) {
        message_ = "agent 尚未同步，已阻止 " + method;
        emit changed();
        return;
    }
    send(method, params);
}
void RpcClient::send(QString method, QJsonObject params) {
    if (!connected()) { message_ = "agent 未连接"; emit changed(); return; }
    if (pending_.size() >= 64) { message_ = "请求过多，请稍后"; emit changed(); return; }
    if (method == "attempt.list" || method == "file.list" || method == "range.list") {
        for (const auto& pending : pending_) {
            if (pending.method == method && pending.params == params && pending.detailSerial == detailSerial_) return;
        }
    }
    QString id = QString::number(++request_);
    pending_[id] = {method, params, detailSerial_};
    QTimer::singleShot(5000, this, [this,id] {
        if (!pending_.contains(id)) return;
        message_ = "agent 请求超时；断开后重新握手，未重发修改操作";
        socket_.abort();
        emit changed();
    });
    socket_.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}})
                  .toJson(QJsonDocument::Compact) + "\n");
}
void RpcClient::upsert(QJsonObject task) {
    QString id = task["transfer_id"].toString();
    if (id.isEmpty()) return;
    if (task["state"].toString() == "completed") {
        auto metrics = task["metrics"].toObject();
        const auto policy = task["policy"].toObject();
        const auto effective = task["effective_policy"].toObject();
        bool verified = policy["checksum_policy"].toString() == "none"
            ? policy["experimental_raw_ack"].toBool() && task["integrity_status"].toString() == "not_requested"
                && effective["checksum"].toString() == "none"
            : policy["checksum_policy"].toString() == "required_crc32c"
                && task["integrity_status"].toString() == "pass" && effective["checksum"].toString() == "crc32c";
        bool counts = true;
        for (auto key : {"bytes_total", "bytes_committed", "files_total", "files_committed", "files_skipped", "files_failed"})
            counts = counts && safeCount(metrics[key]);
        bool secure = effective["control_tls"].toString() == "verified"
            && (effective["data_tls"].toString() == "verified"
                || (policy["data_tls"].toString() == "off" && effective["data_tls"].toString() == "off"));
        if (!task["commit_confirmed"].toBool() || task["transfer_status"].toString() != "pass"
            || task["evidence_status"].toString() != "complete" || !task["scan_complete"].toBool()
            || !task["error"].isNull() || !verified || !secure || !counts
            || effective["resolution"].toString() != "resolved"
            || effective["mode"].toString() != task["actual_mode"].toString()
            || (effective["mode"].toString() != "v1" && effective["mode"].toString() != "v2")
            || metrics["bytes_total"].toInteger() != metrics["bytes_committed"].toInteger()
            || metrics["files_total"].toInteger() != metrics["files_committed"].toInteger()+metrics["files_skipped"].toInteger()
            || metrics["files_failed"].toInteger(-1) != 0) {
            message_ = "agent completed 缺少提交/完整性证据，已拒绝该状态";
            emit changed();
            return;
        }
    }
    for (auto& old : tasks_) {
        auto previous = old.toMap();
        if (previous.value("transfer_id").toString() != id) continue;
        auto previousAttempt = previous.value("attempt").toMap();
        auto newAttempt = task["attempt"].toObject();
        auto previousNumber = previousAttempt.value("attempt_number").toLongLong();
        auto newNumber = newAttempt["attempt_number"].toInteger();
        bool sameAttempt = previousNumber == newNumber
            && previousAttempt.value("attempt_id").toString() == newAttempt["attempt_id"].toString();
        if (previous.value("revision").toLongLong() < task["revision"].toInteger()
            && (sameAttempt || newNumber > previousNumber)) {
            if (id == detailTransfer_ && !detailAttempt_.isEmpty() && newAttempt["attempt_id"].toString() != detailAttempt_) {
                ++detailSerial_;
                attempts_.clear(); files_.clear(); ranges_.clear();
                detailStatus_ = "attempt 已更新，请重新加载详情";
            }
            old = task.toVariantMap();
        }
        emit changed();
        return;
    }
    tasks_.prepend(task.toVariantMap());
    emit changed();
}
bool RpcClient::acceptWatermark(QJsonObject result, const PendingRequest& request) {
    QString kind = result["kind"].toString();
    if (kind != request.method.split('.').first()+"_list") return false;
    QString attempt = result["attempt_id"].toString();
    if (request.detailSerial != detailSerial_ || result["transfer_id"].toString() != detailTransfer_
        || (!attempt.isEmpty() && attempt != detailAttempt_)) return false;
    if (kind == "range_list"
        && (result["file_id"].toString() != detailFile_
            || result["generation"].toInteger(-1) != detailGeneration_)) {
        detailStatus_ = "旧 file/generation 页面已丢弃";
        return false;
    }
    for (const auto& value : result["items"].toArray()) {
        auto item = value.toObject();
        // Attempt entries carry no transfer_id in API2; their page owns that scope.
        bool identity = !item["attempt_id"].toString().isEmpty();
        if (kind != "attempt_list") identity = identity && item["transfer_id"].toString() == detailTransfer_
            && item["attempt_id"].toString() == detailAttempt_;
        if (kind == "file_list") identity = identity && !item["file_id"].toString().isEmpty() && safeCount(item["generation"]);
        if (kind == "range_list") identity = identity && item["file_id"].toString() == detailFile_
            && item["generation"].toInteger(-1) == detailGeneration_ && !item["range_id"].toString().isEmpty();
        if (!identity) { detailStatus_ = "分页条目身份不匹配，整页已丢弃"; return false; }
    }
    QString epoch = result["agent_epoch"].toString();
    qint64 revision = result["revision"].toInteger(-1), sequence = result["snapshot_sequence"].toInteger(-1);
    QString snapshot = result["snapshot_id"].toString();
    if (epoch != epoch_ || snapshot.isEmpty() || revision < 0 || sequence < 0) return false;
    // Each entity query owns its cursor snapshot. All detail queries must still
    // agree on the transfer revision and event watermark before mixing items.
    if (detailEpoch_.isEmpty()) {
        detailEpoch_ = epoch;
        detailRevision_ = revision;
        detailSequence_ = sequence;
    } else if (epoch != detailEpoch_ || revision != detailRevision_ || sequence != detailSequence_) {
        detailStatus_ = "分页 watermark 不一致，请刷新详情";
        return false;
    }
    if (request.params["cursor"].isNull()) {
        if (detailSnapshots_.contains(kind)) return false;
        detailSnapshots_[kind] = snapshot;
        detailSnapshot_ = snapshot;
    } else if (detailSnapshots_.value(kind) != snapshot) {
        detailStatus_ = "分页 snapshot 不一致，请刷新详情";
        return false;
    }
    return true;
}
void RpcClient::handlePage(QJsonObject result, const PendingRequest& request) {
    if (!acceptWatermark(result, request)) { emit changed(); return; }
    QString kind = result["kind"].toString();
    if (kind == "attempt_list") {
        attempts_ += result["items"].toArray().toVariantList();
        detailCursorAttempts_ = result["next_cursor"].toString();
    } else if (kind == "file_list") {
        files_ += result["items"].toArray().toVariantList();
        detailCursorFiles_ = result["next_cursor"].toString();
        if (!files_.isEmpty()) {
            auto file = files_.last().toMap();
            detailFile_ = file.value("file_id").toString();
            detailGeneration_ = file.value("generation").toLongLong();
            detailCursorRanges_.clear();
            detailSnapshots_.remove("range_list");
            ranges_.clear();
        }
    } else if (kind == "range_list") {
        ranges_ += result["items"].toArray().toVariantList();
        detailCursorRanges_ = result["next_cursor"].toString();
    }
    bool complete = detailCursorAttempts_.isEmpty() && detailCursorFiles_.isEmpty() && detailCursorRanges_.isEmpty();
    detailStatus_ = complete ? "当前实体分页已加载" : "分页部分加载";
    emit changed();
}
void RpcClient::read() {
    buffer_ += socket_.readAll();
    while (buffer_.contains('\n')) {
        int at = buffer_.indexOf('\n');
        if (at > 1024*1024) { message_ = "agent 消息超长"; socket_.abort(); emit changed(); return; }
        auto line = buffer_.left(at);
        buffer_.remove(0, at+1);
        QJsonParseError error;
        auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            message_ = "agent 响应 JSON 无效"; socket_.abort(); emit changed(); return;
        }
        auto message = document.object();
        if (message["jsonrpc"].toString() != "2.0") {
            message_ = "agent RPC 版本无效"; socket_.abort(); emit changed(); return;
        }
        if (message["method"].toString() == "transfer.event") {
            auto event = message["params"].toObject();
            if (!apiCompatible_ || syncInFlight_ || event["subscription_id"].toString() != subscription_) continue;
            auto sequence = event["sequence"].toInteger(-1);
            if (event["agent_epoch"].toString() != epoch_ || sequence > sequence_+1) { refresh(); continue; }
            if (sequence <= sequence_) continue;
            sequence_ = sequence;
            auto payload = event["payload"].toObject();
            if (event["type"].toString() == "transfer.state" || event["type"].toString() == "transfer.snapshot") {
                if (event["transfer_id"].toString() != payload["transfer_id"].toString()
                    || event["attempt_id"].toString() != payload["attempt"].toObject()["attempt_id"].toString()
                    || event["revision"].toInteger(-1) != payload["revision"].toInteger(-2)) {
                    message_ = "事件 envelope 与 payload 身份不一致，已拒绝";
                    emit changed();
                    continue;
                }
                upsert(payload);
            }
            continue;
        }
        QString id = message["id"].toString();
        if (!pending_.contains(id)) continue;
        auto request = pending_.take(id);
        bool entityPage = request.method == "attempt.list" || request.method == "file.list" || request.method == "range.list";
        if (entityPage && request.detailSerial != detailSerial_) continue;
        if (message.contains("error")) {
            auto rpcError = message["error"].toObject();
            message_ = rpcError["message"].toString();
            if (request.method == "transfer.subscribe") { syncInFlight_ = false; stale_ = true; }
            if (rpcError["data"].toObject()["code"].toString() == "cursor_expired")
                detailStatus_ = "分页已过期，请刷新详情";
            emit changed();
            continue;
        }
        auto result = message["result"].toObject();
        QString kind = result["kind"].toString();
        if (request.method == "agent.getInfo") {
            agentInfo_ = result.toVariantMap();
            apiCompatible_ = kind == "agent_info" && result["api_version"].toString() == "2.0.0"
                             && !result["agent_epoch"].toString().isEmpty();
            if (!apiCompatible_) {
                message_ = "agent API 不兼容：需要 2.0.0，实际 " + result["api_version"].toString();
                emit changed(); continue;
            }
            QString newEpoch = result["agent_epoch"].toString();
            bool sameEpoch = newEpoch == epoch_ && haveSequence_;
            epoch_ = newEpoch;
            message_ = demoAdapter() ? "agent API 2.0.0 已确认（演示 adapter）" : "agent API 2.0.0 已确认（真实 adapter 未接入）";
            syncInFlight_ = true;
            send("transfer.subscribe", {{"transfer_ids",QJsonArray{}},{"agent_epoch",sameEpoch?QJsonValue(epoch_):QJsonValue()},
                                        {"after_sequence",sameEpoch?QJsonValue(sequence_):QJsonValue()}});
            send("profile.list", {});
            if (demoAdapter()) send("node.capabilities", {{"node_id","demo"}});
        } else if (request.method == "transfer.subscribe" && kind == "subscription") {
            QString newEpoch = result["agent_epoch"].toString();
            auto newSequence = result["sequence"].toInteger(-1);
            if (newEpoch.isEmpty() || result["subscription_id"].toString().isEmpty() || newSequence < 0 || (newEpoch != epoch_ && !result["reset"].toBool())) {
                message_ = "订阅响应身份无效"; socket_.abort(); emit changed(); return;
            }
            epoch_ = newEpoch;
            subscription_ = result["subscription_id"].toString();
            sequence_ = newSequence;
            haveSequence_ = true;
            if (result["reset"].toBool()) {
                tasks_.clear();
                for (const auto& task : result["snapshots"].toArray()) upsert(task.toObject());
            }
            syncInFlight_ = false;
            stale_ = false;
        } else if (kind == "transfer" || kind == "operation") {
            upsert(result["snapshot"].toObject());
        } else if (kind == "profile_list") {
            profiles_ = result["profiles"].toArray().toVariantList();
        } else if (kind == "profile") {
            sendPostHandshake("profile.list", {});
            message_ = "节点配置已保存；凭据仅保存引用，真实连接未接入";
        } else if (kind == "capabilities") capabilities_ = result.toVariantMap();
        else if (kind == "connection_test") checks_ = result["checks"].toObject().toVariantMap();
        else if (entityPage) handlePage(result, request);
        emit changed();
    }
    if (buffer_.size() > 1024*1024) { message_ = "agent 消息超长"; socket_.abort(); emit changed(); }
}
void RpcClient::refresh() {
    if (!connected()) return;
    if (!apiCompatible_) { startSession(); return; }
    if (syncInFlight_) return;
    stale_ = true;
    syncInFlight_ = true;
    ++detailSerial_;
    detailStatus_ = "快照同步中，请重新加载详情";
    send("transfer.subscribe", {{"transfer_ids",QJsonArray{}},{"agent_epoch",QJsonValue()},{"after_sequence",QJsonValue()}});
    emit changed();
}
void RpcClient::loadDetails(QString transfer, QString attempt) {
    if (!apiCompatible_ || stale_) { detailStatus_ = "agent 尚未同步"; emit changed(); return; }
    ++detailSerial_;
    detailTransfer_ = transfer; detailAttempt_ = attempt;
    detailSnapshots_.clear(); detailSnapshot_.clear(); detailEpoch_.clear();
    detailRevision_ = -1; detailSequence_ = -1;
    detailCursorAttempts_.clear(); detailCursorFiles_.clear(); detailCursorRanges_.clear();
    attempts_.clear(); files_.clear(); ranges_.clear(); detailFile_.clear(); detailGeneration_ = -1;
    detailStatus_ = "正在加载详情分页";
    sendPostHandshake("attempt.list", {{"transfer_id",transfer},{"cursor",QJsonValue()},{"limit",64}});
    sendPostHandshake("file.list", {{"transfer_id",transfer},{"attempt_id",attempt},{"cursor",QJsonValue()},{"limit",64}});
    emit changed();
}
void RpcClient::loadNextAttempts() {
    if (!detailCursorAttempts_.isEmpty()) sendPostHandshake("attempt.list", {{"transfer_id",detailTransfer_},{"cursor",detailCursorAttempts_},{"limit",64}});
}
void RpcClient::loadNextFiles() {
    if (!detailCursorFiles_.isEmpty()) sendPostHandshake("file.list", {{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"cursor",detailCursorFiles_},{"limit",64}});
}
void RpcClient::loadNextRanges() {
    if (detailFile_.isEmpty() || detailGeneration_ < 0 || (detailSnapshots_.contains("range_list") && detailCursorRanges_.isEmpty())) return;
    QJsonValue cursor = detailCursorRanges_.isEmpty() ? QJsonValue() : QJsonValue(detailCursorRanges_);
    sendPostHandshake("range.list", {{"transfer_id",detailTransfer_},{"attempt_id",detailAttempt_},{"file_id",detailFile_},
                                     {"generation",detailGeneration_},{"cursor",cursor},{"limit",64}});
}
void RpcClient::create(QString direction, QString local, QString remote, bool failure, QString mode) {
    if (!apiCompatible_ || stale_) { message_ = "agent 尚未同步，已阻止创建"; emit changed(); return; }
    if (!demoAdapter()) { message_ = "真实传输 adapter 尚未接入，不能创建真实任务"; emit changed(); return; }
    if (!QFileInfo(local).isAbsolute()) { message_ = "本地目录必须是绝对路径"; emit changed(); return; }
    QJsonObject l{{"kind","local"},{"path",local},{"node_id",QJsonValue()},{"root_id",QJsonValue()}},
        r{{"kind","remote"},{"path",failure?"demo-fail":remote},{"node_id","demo"},{"root_id","demo-root"}},
        policy{{"requested_mode",mode},{"scheduler","auto"},{"channel_count","auto"},{"pending_window",2},{"queue_depth",256},
               {"range_policy","auto"},{"checksum_policy","required_crc32c"},{"resume_policy","allow"},{"control_tls","verify_ca"},
               {"data_tls","required"},{"fallback_policy","allow_compatible"},{"conflict_policy","fail"},{"experimental_raw_ack",false}};
    sendPostHandshake("transfer.create", {{"operation_id",operation()},{"direction",direction},
                                           {"source",direction=="upload"?l:r},{"destination",direction=="upload"?r:l},{"policy",policy}});
}
void RpcClient::action(QString method, QVariantMap task) {
    QJsonObject params{{"transfer_id",task.value("transfer_id").toString()},{"operation_id",operation()},
                       {"expected_revision",task.value("revision").toLongLong()}};
    if (method == "transfer.resume") params["checkpoint_id"] = task.value("recovery").toMap().value("checkpoint_id").toString();
    sendPostHandshake(method, params);
}
void RpcClient::saveProfile(QVariantMap profile, bool update) {
    sendPostHandshake(update?"profile.update":"profile.create", {{"operation_id",operation()},{"profile",QJsonObject::fromVariantMap(profile)}});
}
void RpcClient::testNode(QString id, QString root) {
    sendPostHandshake("node.testConnection", {{"node_id",id},{"root_id",root},{"path",""},{"access","read"}});
}
void RpcClient::demo() { create("upload","/home/student/demo","演示数据",false,"auto"); }
