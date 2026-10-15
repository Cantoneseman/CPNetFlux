#include "rpc_client.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QUuid>
#include <sys/socket.h>
#include <unistd.h>

static QString operation() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

RpcClient::RpcClient(QString path, QObject* parent)
    : QObject(parent), path_(std::move(path)) {
    loadHidden();
    retry_.setInterval(1000);
    connect(&retry_, &QTimer::timeout, this, [this] {
        if (socket_.state() == QLocalSocket::UnconnectedState)
            socket_.connectToServer(path_);
    });
    requestTimer_.setInterval(250);
    connect(&requestTimer_, &QTimer::timeout, this, &RpcClient::checkPending);
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        struct ucred peer{};
        socklen_t size = sizeof(peer);
        if (getsockopt(socket_.socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 || peer.uid != getuid()) {
            message_ = "agent 身份校验失败；请检查 Unix socket 权限";
            socket_.abort();
            emit changed();
            return;
        }
        startSession();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        if (createInFlight_)
            failCreate("agent 连接已断开，未确认 transfer_id；可以重试", true);
        buffer_.clear();
        pending_.clear();
        pendingDeadlines_.clear();
        apiCompatible_ = false;
        stale_ = true;
        detailStatus_ = "agent unavailable / 待重连";
        message_ = "agent 离线；当前任务快照已标记 stale，正在重连";
        emit changed();
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &RpcClient::read);
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        Q_UNUSED(error);
        stale_ = true;
        apiCompatible_ = false;
        detailStatus_ = "agent unavailable / socket 错误";
        if (socket_.error() == QLocalSocket::SocketAccessError)
            message_ = "agent socket 权限被拒绝；请检查 Unix socket 所属用户和权限";
        else if (socket_.error() == QLocalSocket::ServerNotFoundError)
            message_ = "agent unavailable；未找到 Unix socket，请启动 cpnetflux-agent";
        else
            message_ = "agent socket 错误：" + socket_.errorString();
        emit changed();
    });
    retry_.start();
    requestTimer_.start();
    socket_.connectToServer(path_);
}

RpcClient::~RpcClient() {
    retry_.stop();
    requestTimer_.stop();
    disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
}

QVariantList RpcClient::tasks() const {
    QVariantList visible;
    for (const QVariant& item : tasks_) {
        const QVariantMap task = item.toMap();
        const QString id = task.value("transfer_id").toString();
        QString adapter = task.value("adapter").toString();
        if (adapter.isEmpty()) adapter = demoAdapter() ? "demo" : "real";
        if (!taskHidden(id, adapter)) visible.push_back(task);
    }
    return visible;
}

bool RpcClient::taskHidden(const QString& id, const QString& adapter) const {
    return adapter == "demo" ? hiddenDemo_.contains(id) : hiddenReal_.contains(id);
}

void RpcClient::loadHidden() {
    QSettings settings("CPNetFlux", "CPNetFluxDesktop");
    for (const QString& id : settings.value("hidden_tasks/demo").toStringList()) hiddenDemo_.insert(id);
    for (const QString& id : settings.value("hidden_tasks/real").toStringList()) hiddenReal_.insert(id);
}

void RpcClient::persistHidden() const {
    QSettings settings("CPNetFlux", "CPNetFluxDesktop");
    settings.setValue("hidden_tasks/demo", QStringList(hiddenDemo_.values()));
    settings.setValue("hidden_tasks/real", QStringList(hiddenReal_.values()));
    settings.sync();
}

void RpcClient::startSession() {
    if (createInFlight_)
        failCreate("agent 会话重建，未确认 transfer_id；可以重试", true);
    stale_ = true;
    apiCompatible_ = false;
    pending_.clear();
    pendingDeadlines_.clear();
    message_ = "正在校验 desktop-agent API 2.0.0";
    send("agent.getInfo", {});
    emit changed();
}

void RpcClient::sendPostHandshake(QString method, QJsonObject params) {
    if (!connected()) {
        message_ = "agent unavailable；请启动 agent 或检查 Unix socket";
        emit changed();
        return;
    }
    if (!apiCompatible_) {
        message_ = "agent API 未兼容，已阻止 " + method;
        emit changed();
        return;
    }
    send(method, params);
}

QString RpcClient::sendRequest(QString method, QJsonObject params) {
    if (!connected()) {
        message_ = "agent unavailable；请启动 agent 或检查 Unix socket";
        emit changed();
        return {};
    }
    if (pending_.size() > 64) {
        message_ = "agent 请求过多，请稍后重试";
        emit changed();
        return {};
    }
    const QString id = QString::number(++request_);
    pending_[id] = method;
    pendingDeadlines_[id] = nowMs() + 10000;
    const QJsonObject request{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    if (socket_.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n") < 0) {
        pending_.remove(id);
        pendingDeadlines_.remove(id);
        message_ = "agent 请求写入失败；请重连后重试";
        emit changed();
        return {};
    }
    return id;
}

void RpcClient::send(QString method, QJsonObject params) { sendRequest(std::move(method), std::move(params)); }

void RpcClient::checkPending() {
    const qint64 now = nowMs();
    const auto ids = pending_.keys();
    for (const QString& id : ids) {
        if (pendingDeadlines_.value(id, now + 1) > now) continue;
        const QString method = pending_.take(id);
        pendingDeadlines_.remove(id);
        if (method == "transfer.create") {
            failCreate("agent 创建请求超时；未确认 transfer_id，未显示为成功任务；可以用相同表单重试", true);
        } else {
            if (method == "transfer.subscribe" || method == "transfer.get" || method.endsWith(".list")) {
                stale_ = true;
                detailStatus_ = "agent unavailable / 请求超时，请刷新或重连";
            }
            message_ = method + " 超时；agent 状态未知，请刷新或重连";
            emit changed();
        }
    }
}

void RpcClient::failCreate(const QString& error, bool retryable) {
    createInFlight_ = false;
    createStatus_ = retryable ? "创建失败，可重试" : "创建失败";
    createError_ = error;
    currentCreateRequestId_.clear();
    if (!retryable) {
        lastCreateOperationId_.clear();
        lastCreateKey_.clear();
    }
    message_ = error;
    emit changed();
}

void RpcClient::upsert(QJsonObject task) {
    const QString id = task.value("transfer_id").toString();
    if (id.isEmpty()) return;
    for (QVariant& old : tasks_) {
        if (old.toMap().value("transfer_id").toString() != id) continue;
        if (old.toMap().value("revision").toLongLong() <= task.value("revision").toInteger())
            old = task.toVariantMap();
        emit changed();
        return;
    }
    tasks_.prepend(task.toVariantMap());
    emit changed();
}

bool RpcClient::acceptWatermark(QJsonObject response) {
    const QString transfer = response.value("transfer_id").toString();
    const QString attempt = response.value("attempt_id").toString();
    if (transfer != detailTransfer_ || (!attempt.isEmpty() && attempt != detailAttempt_)) {
        detailStatus_ = "旧 transfer/attempt 页面已丢弃，请重新加载详情";
        return false;
    }
    const QString kind = response.value("kind").toString();
    const QString epoch = response.value("agent_epoch").toString();
    const QString snapshot = response.value("snapshot_id").toString();
    const qint64 revision = response.value("revision").toInteger();
    const qint64 sequence = response.value("snapshot_sequence").toInteger();
    if (!epoch_.isEmpty() && epoch != epoch_) {
        stale_ = true;
        detailStatus_ = "agent epoch 已变化；详情不可用，请刷新";
        return false;
    }
    if (detailPageSnapshots_.contains(kind) &&
        (detailPageSnapshots_.value(kind) != snapshot || detailPageEpochs_.value(kind) != epoch ||
         detailPageRevisions_.value(kind) != revision || detailPageSequences_.value(kind) != sequence)) {
        detailStatus_ = "分页 watermark 不一致，请刷新详情";
        return false;
    }
    detailPageSnapshots_[kind] = snapshot;
    detailPageEpochs_[kind] = epoch;
    detailPageRevisions_[kind] = revision;
    detailPageSequences_[kind] = sequence;
    if (detailSnapshot_.isEmpty()) {
        detailSnapshot_ = snapshot;
        detailEpoch_ = epoch;
        detailRevision_ = revision;
        detailSequence_ = sequence;
    }
    return true;
}

void RpcClient::handlePage(QJsonObject response) {
    if (!acceptWatermark(response)) {
        emit changed();
        return;
    }
    const QString kind = response.value("kind").toString();
    if (kind == "attempt_list") {
        attempts_ += response.value("items").toArray().toVariantList();
    } else if (kind == "file_list") {
        files_ += response.value("items").toArray().toVariantList();
        detailCursorFiles_ = response.value("next_cursor").toString();
        if (!files_.isEmpty()) detailFile_ = files_.last().toMap().value("file_id").toString();
    } else if (kind == "range_list") {
        ranges_ += response.value("items").toArray().toVariantList();
        detailCursorRanges_ = response.value("next_cursor").toString();
    }
    detailStatus_ = (detailCursorFiles_.isEmpty() && detailCursorRanges_.isEmpty()) ? "分页完整" : "分页部分加载";
    emit changed();
}

void RpcClient::read() {
    buffer_ += socket_.readAll();
    if (buffer_.size() > 1024 * 1024) {
        message_ = "agent 消息超长，连接已关闭";
        socket_.abort();
        emit changed();
        return;
    }
    while (buffer_.contains('\n')) {
        const int at = buffer_.indexOf('\n');
        const QByteArray line = buffer_.left(at);
        buffer_.remove(0, at + 1);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            message_ = "agent 响应 JSON 无效，连接已关闭";
            socket_.abort();
            emit changed();
            return;
        }
        const QJsonObject message = document.object();
        if (message.value("method").toString() == "transfer.event") {
            const QJsonObject params = message.value("params").toObject();
            const qint64 sequence = params.value("sequence").toInteger();
            if (!apiCompatible_) continue;
            if (params.value("agent_epoch").toString() != epoch_) {
                stale_ = true;
                detailStatus_ = "agent epoch 已变化；正在重新同步";
                refresh();
                continue;
            }
            if (sequence <= sequence_) continue;
            if (sequence != sequence_ + 1) {
                stale_ = true;
                detailStatus_ = "事件序号不连续；正在重新同步";
                refresh();
                continue;
            }
            sequence_ = sequence;
            if (params.value("type").toString() == "transfer.state") {
                const QJsonObject payload = params.value("payload").toObject();
                if (detailAttempt_.isEmpty() || payload.value("attempt").toObject().value("attempt_id").toString() == detailAttempt_)
                    upsert(payload);
            }
            continue;
        }
        const QString id = message.value("id").toString();
        const QString method = pending_.take(id);
        pendingDeadlines_.remove(id);
        if (method.isEmpty()) continue;
        if (message.contains("error")) {
            const QJsonObject rpcError = message.value("error").toObject();
            const QJsonObject data = rpcError.value("data").toObject();
            const QString code = data.value("code").toString();
            if (method == "transfer.create") {
                failCreate("创建失败：" + (rpcError.value("message").toString().isEmpty() ? code : rpcError.value("message").toString()), true);
            } else {
                if (method == "transfer.subscribe" || method == "transfer.get" || code == "revision_conflict") {
                    stale_ = true;
                    if (code == "revision_conflict") detailStatus_ = "任务已更新；当前详情已标记 stale，请刷新";
                }
                if (method == "attempt.list" || method == "file.list" || method == "range.list")
                    detailStatus_ = code == "unsupported" ? "该分页能力由 agent 未提供" : "分页失败：" + rpcError.value("message").toString();
                if (code == "cursor_expired") detailStatus_ = "分页已过期，请刷新详情";
                message_ = rpcError.value("message").toString();
                emit changed();
            }
            continue;
        }
        const QJsonObject result = message.value("result").toObject();
        const QString kind = result.value("kind").toString();
        if (method == "agent.getInfo" || kind == "agent_info") {
            agentInfo_ = result.toVariantMap();
            apiCompatible_ = result.value("api_version").toString() == "2.0.0";
            if (!apiCompatible_) {
                message_ = "agent API 不兼容：需要 2.0.0，实际 " + result.value("api_version").toString();
                stale_ = true;
                emit changed();
                continue;
            }
            epoch_ = result.value("agent_epoch").toString();
            stale_ = true;
            message_ = QString("agent API 2.0.0 已确认（") + (demoAdapter() ? "开发诊断/合成数据" : "真实 adapter") + "）";
            send("transfer.subscribe", QJsonObject{{"transfer_ids", QJsonArray{}}, {"agent_epoch", epoch_}, {"after_sequence", sequence_}});
            send("profile.list", {});
            if (demoAdapter()) send("node.capabilities", QJsonObject{{"node_id", "demo"}});
        } else if (kind == "subscription") {
            const QString subscriptionEpoch = result.value("agent_epoch").toString();
            if (!epoch_.isEmpty() && subscriptionEpoch != epoch_) {
                stale_ = true;
                message_ = "agent epoch 已变化，请重新同步";
                refresh();
                continue;
            }
            if (epoch_.isEmpty()) epoch_ = subscriptionEpoch;
            sequence_ = result.value("sequence").toInteger();
            if (result.value("reset").toBool()) {
                tasks_.clear();
                for (const QJsonValue& item : result.value("snapshots").toArray()) upsert(item.toObject());
            }
            stale_ = false;
            detailStatus_ = detailStatus_ == "agent unavailable / 待重连" ? "已重连，任务快照已恢复" : detailStatus_;
        } else if (method == "transfer.create") {
            const QJsonObject snapshot = result.value("snapshot").toObject();
            const QString transferId = snapshot.value("transfer_id").toString();
            const qint64 revision = snapshot.value("revision").toInteger();
            if (transferId.isEmpty() || epoch_.isEmpty() || revision < 0) {
                failCreate("agent 创建响应缺少真实 transfer_id/epoch/revision；未显示为成功任务", true);
                continue;
            }
            upsert(snapshot);
            sendPostHandshake("transfer.get", QJsonObject{{"transfer_id", transferId}});
            createInFlight_ = false;
            createStatus_ = "已创建，正在读取任务快照";
            createError_.clear();
            currentCreateRequestId_.clear();
            lastCreateOperationId_.clear();
            lastCreateKey_.clear();
            emit createSucceeded(transferId, snapshot.value("attempt").toObject().value("attempt_id").toString());
        } else if (kind == "transfer" || kind == "operation") {
            upsert(result.value("snapshot").toObject());
        } else if (kind == "profile_list") {
            profiles_ = result.value("profiles").toArray().toVariantList();
            if (!demoAdapter() && !profiles_.isEmpty())
                send("node.capabilities", QJsonObject{{"node_id", profiles_.first().toMap().value("node_id").toString()}});
        } else if (kind == "profile") {
            sendPostHandshake("profile.list", {});
            message_ = "节点配置已保存；凭据仅保存引用，等待 agent 进行真实 TLS/认证检查";
        } else if (kind == "capabilities") {
            capabilities_ = result.toVariantMap();
        } else if (kind == "connection_test") {
            checks_ = result.value("checks").toObject().toVariantMap();
        } else if (kind == "attempt_list" || kind == "file_list" || kind == "range_list") {
            handlePage(result);
        }
        emit changed();
    }
}

void RpcClient::refresh() {
    if (!connected()) {
        stale_ = true;
        detailStatus_ = "agent unavailable / 待重连";
        message_ = "agent unavailable；请启动 agent 或检查 Unix socket";
        emit changed();
        return;
    }
    if (!apiCompatible_) {
        startSession();
        return;
    }
    stale_ = true;
    detailStatus_ = "正在刷新详情与任务列表";
    epoch_.clear();
    sequence_ = 0;
    send("transfer.subscribe", QJsonObject{{"transfer_ids", QJsonArray{}}, {"agent_epoch", QJsonValue()}, {"after_sequence", QJsonValue()}});
}

void RpcClient::loadDetails(QString transferId, QString attemptId) {
    detailTransfer_ = std::move(transferId);
    detailAttempt_ = std::move(attemptId);
    detailSnapshot_.clear();
    detailEpoch_.clear();
    detailRevision_ = -1;
    detailSequence_ = -1;
    detailPageSnapshots_.clear();
    detailPageEpochs_.clear();
    detailPageRevisions_.clear();
    detailPageSequences_.clear();
    detailCursorFiles_.clear();
    detailCursorRanges_.clear();
    attempts_.clear();
    files_.clear();
    ranges_.clear();
    detailFile_.clear();
    if (!connected() || !apiCompatible_ || stale_) {
        detailStatus_ = "agent unavailable / 待重连，详情未刷新";
        message_ = "agent unavailable；详情不会用旧快照伪装成功";
        emit changed();
        return;
    }
    detailStatus_ = "正在加载任务快照与详情分页";
    sendPostHandshake("transfer.get", QJsonObject{{"transfer_id", detailTransfer_}});
    sendPostHandshake("attempt.list", QJsonObject{{"transfer_id", detailTransfer_}, {"attempt_id", detailAttempt_}, {"cursor", QJsonValue()}, {"limit", 64}});
    sendPostHandshake("file.list", QJsonObject{{"transfer_id", detailTransfer_}, {"attempt_id", detailAttempt_}, {"cursor", QJsonValue()}, {"limit", 64}});
    emit changed();
}

void RpcClient::loadNextFiles() {
    if (detailCursorFiles_.isEmpty()) return;
    sendPostHandshake("file.list", QJsonObject{{"transfer_id", detailTransfer_}, {"attempt_id", detailAttempt_}, {"cursor", detailCursorFiles_}, {"limit", 64}});
}

void RpcClient::loadNextRanges() {
    if (detailFile_.isEmpty()) return;
    const QJsonValue cursor = detailCursorRanges_.isEmpty() ? QJsonValue() : QJsonValue(detailCursorRanges_);
    sendPostHandshake("range.list", QJsonObject{{"transfer_id", detailTransfer_}, {"attempt_id", detailAttempt_}, {"file_id", detailFile_}, {"generation", 1}, {"cursor", cursor}, {"limit", 64}});
}

void RpcClient::create(QString direction, QString local, QString remote, bool failure, QString mode, QString profileId) {
    if (createInFlight_) {
        message_ = "正在创建任务；重复点击已忽略";
        emit changed();
        return;
    }
    if (!connected() || !apiCompatible_ || stale_) {
        failCreate("agent unavailable 或 API 尚未协商完成；未提交任务", true);
        return;
    }
    const QFileInfo info(local);
    if (!info.isAbsolute() || !info.exists()) {
        failCreate("本地文件/目录必须存在且使用绝对路径；未提交任务", false);
        return;
    }
    QString node;
    QString root;
    if (!demoAdapter() && !profiles_.isEmpty()) {
        QVariantMap selected;
        for (const QVariant& item : profiles_)
            if (item.toMap().value("node_id").toString() == profileId) selected = item.toMap();
        if (selected.isEmpty()) selected = profiles_.first().toMap();
        node = selected.value("node_id").toString();
        root = selected.value("default_root_id").toString();
    }
    if (node.isEmpty()) node = "demo";
    if (root.isEmpty()) root = "demo-root";
    const QJsonObject source{{"kind", "local"}, {"path", local}, {"node_id", QJsonValue()}, {"root_id", QJsonValue()}};
    const QJsonObject destination{{"kind", "remote"}, {"path", failure ? "demo-fail" : remote}, {"node_id", node}, {"root_id", root}};
    const QJsonObject policy{{"requested_mode", demoAdapter() ? mode : "v2"}, {"scheduler", demoAdapter() ? "auto" : "dynamic_ring"}, {"channel_count", 1}, {"pending_window", 2}, {"queue_depth", 256}, {"range_policy", demoAdapter() ? "auto" : "file"}, {"checksum_policy", "required_crc32c"}, {"resume_policy", "allow"}, {"control_tls", "verify_ca"}, {"data_tls", "required"}, {"fallback_policy", "allow_compatible"}, {"conflict_policy", "fail"}, {"experimental_raw_ack", false}};
    const QJsonObject key{{"direction", direction}, {"local", local}, {"remote", remote}, {"failure", failure}, {"mode", mode}, {"profile_id", profileId}};
    const QString keyText = QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact));
    if (keyText != lastCreateKey_ || lastCreateOperationId_.isEmpty()) {
        lastCreateKey_ = keyText;
        lastCreateOperationId_ = operation();
    }
    const QJsonObject params{{"operation_id", lastCreateOperationId_}, {"direction", direction}, {"source", source}, {"destination", destination}, {"policy", policy}};
    createInFlight_ = true;
    createStatus_ = "正在向 agent 创建任务";
    createError_.clear();
    currentCreateRequestId_ = sendRequest("transfer.create", params);
    if (currentCreateRequestId_.isEmpty()) failCreate("agent 创建请求未发出；请检查 socket", true);
    else emit changed();
}

void RpcClient::action(QString method, QVariantMap task) {
    if (!connected() || !apiCompatible_ || stale_) {
        message_ = "agent unavailable 或任务快照 stale；已阻止操作";
        emit changed();
        return;
    }
    QJsonObject params{{"transfer_id", task.value("transfer_id").toString()}, {"operation_id", operation()}, {"expected_revision", task.value("revision").toLongLong()}};
    if (method == "transfer.resume") params["checkpoint_id"] = task.value("recovery").toMap().value("checkpoint_id").toString();
    sendPostHandshake(method, params);
}

void RpcClient::removeTask(QString transferId, QString adapter) {
    if (removeInFlight_) {
        removeError_ = "正在处理上一次列表清理请求";
        emit changed();
        return;
    }
    QVariantMap found;
    for (const QVariant& item : tasks_) {
        if (item.toMap().value("transfer_id").toString() == transferId) { found = item.toMap(); break; }
    }
    if (found.isEmpty()) {
        removeStatus_ = "清理失败";
        removeError_ = "任务不存在或已经从本机列表隐藏";
        emit changed();
        return;
    }
    if (adapter != "demo" && adapter != "real") adapter = found.value("adapter").toString();
    if (adapter != "demo" && adapter != "real") adapter = demoAdapter() ? "demo" : "real";
    removeInFlight_ = true;
    removeStatus_ = "正在从本机列表隐藏";
    removeError_.clear();
    emit changed();
    QTimer::singleShot(0, this, [this, transferId, adapter] {
        if (adapter == "demo") hiddenDemo_.insert(transferId); else hiddenReal_.insert(transferId);
        persistHidden();
        removeInFlight_ = false;
        removeStatus_ = "已从本机列表隐藏";
        removeError_ = "不会删除远端文件、目录或 agent 任务";
        emit taskRemoved(transferId);
        emit changed();
    });
}

void RpcClient::saveProfile(QVariantMap profile, bool update) {
    sendPostHandshake(update ? "profile.update" : "profile.create", QJsonObject{{"operation_id", operation()}, {"profile", QJsonObject::fromVariantMap(profile)}});
}

void RpcClient::testNode(QString id, QString root) {
    sendPostHandshake("node.testConnection", QJsonObject{{"node_id", id}, {"root_id", root}, {"path", ""}, {"access", "read"}});
}

void RpcClient::demo() { create("upload", "/home/student/demo", "演示数据", false, "auto", ""); }
