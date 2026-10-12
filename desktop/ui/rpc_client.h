#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QVariantList>
#include <QTimer>
#include <QHash>

class RpcClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY changed)
    Q_PROPERTY(QVariantMap checks READ checks NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    explicit RpcClient(QString path, QObject* parent=nullptr);
    ~RpcClient() override;
    bool connected() const { return socket_.state()==QLocalSocket::ConnectedState; }
    QVariantList tasks() const { return tasks_; }
    QVariantList profiles() const { return profiles_; }
    QVariantMap capabilities() const { return capabilities_; }
    QVariantMap checks() const { return checks_; }
    QString message() const { return message_; }
    Q_INVOKABLE void create(QString direction, QString local, QString remote, bool failure, QString mode);
    Q_INVOKABLE void action(QString method, QVariantMap task);
    Q_INVOKABLE void saveProfile(QVariantMap profile, bool update);
    Q_INVOKABLE void testNode(QString nodeId, QString root);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void demo();
    Q_INVOKABLE void setPage(int page) { emit pageRequested(page); }
    int snapshotCount() const { return tasks_.size(); }
signals:
    void changed();
    void pageRequested(int page);
private:
    void send(QString method, QJsonObject params);
    void read();
    void upsert(QJsonObject snapshot);
    QLocalSocket socket_;
    QTimer retry_;
    QString path_, epoch_, message_="连接本地 agent 中";
    qint64 sequence_=0, request_=0;
    QByteArray buffer_;
    QHash<QString,QString> pending_;
    QVariantList tasks_, profiles_;
    QVariantMap capabilities_, checks_;
};
