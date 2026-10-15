#include <QtTest>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../ui/rpc_client.h"

class RpcClientIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void create_is_idempotent_and_local_hide_survives_refresh();
};

class FakeAgent : public QObject {
public:
    QLocalServer server;
    QLocalSocket* peer = nullptr;
    int createRequests = 0;
    bool delayCreate = true;
    QJsonObject task;
    QString delayedCreateId;

    bool listen(const QString& path) {
        QFile::remove(path);
        return server.listen(path);
    }
    void send(const QJsonObject& result, const QString& id) {
        if (!peer) return;
        peer->write(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}).toJson(QJsonDocument::Compact) + "\n");
        peer->flush();
    }
    void attach() {
        peer = server.nextPendingConnection();
        auto consume = [this] {
            while (peer && peer->canReadLine()) {
                const QJsonObject request = QJsonDocument::fromJson(peer->readLine()).object();
                const QString method = request.value("method").toString();
                const QString id = request.value("id").toString();
                if (method == "agent.getInfo") {
                    send(QJsonObject{{"kind", "agent_info"}, {"api_version", "2.0.0"}, {"agent_version", "test"}, {"agent_epoch", "test-epoch"}, {"adapter", "demo"}}, id);
                } else if (method == "transfer.subscribe") {
                    QJsonArray snapshots;
                    if (!task.isEmpty()) snapshots.append(task);
                    send(QJsonObject{{"kind", "subscription"}, {"agent_epoch", "test-epoch"}, {"sequence", 0}, {"reset", true}, {"snapshots", snapshots}}, id);
                } else if (method == "profile.list") {
                    send(QJsonObject{{"kind", "profile_list"}, {"profiles", QJsonArray{}}}, id);
                } else if (method == "node.capabilities") {
                    send(QJsonObject{{"kind", "capabilities"}, {"resume_supported", false}}, id);
                } else if (method == "transfer.create") {
                    ++createRequests;
                    task = QJsonObject{{"transfer_id", "transfer-test-1"}, {"revision", 0}, {"adapter", "demo"}, {"state", "queued"}, {"attempt", QJsonObject{{"attempt_id", "attempt-test-1"}}}};
                    if (!delayCreate) send(QJsonObject{{"kind", "transfer"}, {"snapshot", task}}, id);
                    else delayedCreateId = id;
                } else if (method == "transfer.get") {
                    send(QJsonObject{{"kind", "transfer"}, {"snapshot", task}}, id);
                }
            }
        };
        connect(peer, &QLocalSocket::readyRead, this, consume);
        consume();
    }
    void finishCreate() {
        delayCreate = false;
        send(QJsonObject{{"kind", "transfer"}, {"snapshot", task}}, delayedCreateId);
    }
};

void RpcClientIntegrationTest::create_is_idempotent_and_local_hide_survives_refresh() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
    const QString socketPath = temp.path() + "/agent.sock";
    FakeAgent agent;
    QVERIFY(agent.listen(socketPath));
    QSignalSpy connectionSpy(&agent.server, &QLocalServer::newConnection);
    RpcClient client(socketPath);
    QTRY_VERIFY_WITH_TIMEOUT(connectionSpy.count() > 0, 3000);
    agent.attach();
    QTRY_VERIFY_WITH_TIMEOUT(agent.peer != nullptr, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(client.apiCompatible(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!client.stale(), 5000);

    const QString localPath = temp.path() + "/input.txt";
    QFile local(localPath);
    QVERIFY(local.open(QIODevice::WriteOnly));
    local.write("payload");
    local.close();
    client.create("upload", localPath, "remote", false, "auto", "");
    client.create("upload", localPath, "remote", false, "auto", "");
    QTRY_COMPARE_WITH_TIMEOUT(agent.createRequests, 1, 3000);
    QVERIFY(client.createInFlight());
    agent.finishCreate();
    QTRY_VERIFY_WITH_TIMEOUT(!client.createInFlight(), 3000);
    QCOMPARE(client.tasks().size(), 1);
    QCOMPARE(client.tasks().first().toMap().value("transfer_id").toString(), QString("transfer-test-1"));

    client.removeTask("transfer-test-1", "demo");
    QTRY_VERIFY_WITH_TIMEOUT(!client.removeInFlight(), 3000);
    QCOMPARE(client.tasks().size(), 0);
    client.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!client.stale(), 3000);
    QCOMPARE(client.tasks().size(), 0);
}

QTEST_MAIN(RpcClientIntegrationTest)
#include "test_rpc_client.moc"
