#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QVariantList>
#include <QTimer>
#include <QHash>
#include <QSet>

class RpcClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool apiCompatible READ apiCompatible NOTIFY changed)
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(bool demoAdapter READ demoAdapter NOTIFY changed)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QVariantList attempts READ attempts NOTIFY changed)
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(QVariantList ranges READ ranges NOTIFY changed)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY changed)
    Q_PROPERTY(QVariantMap agentInfo READ agentInfo NOTIFY changed)
    Q_PROPERTY(QVariantMap checks READ checks NOTIFY changed)
    Q_PROPERTY(QString detailStatus READ detailStatus NOTIFY changed)
    Q_PROPERTY(QString detailWatermark READ detailWatermark NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool createInFlight READ createInFlight NOTIFY changed)
    Q_PROPERTY(QString createStatus READ createStatus NOTIFY changed)
    Q_PROPERTY(QString createError READ createError NOTIFY changed)
    Q_PROPERTY(bool removeInFlight READ removeInFlight NOTIFY changed)
    Q_PROPERTY(QString removeStatus READ removeStatus NOTIFY changed)
    Q_PROPERTY(QString removeError READ removeError NOTIFY changed)
    Q_PROPERTY(bool actionInFlight READ actionInFlight NOTIFY changed)
    Q_PROPERTY(QString actionTransferId READ actionTransferId NOTIFY changed)
    Q_PROPERTY(QString actionStatus READ actionStatus NOTIFY changed)
    Q_PROPERTY(QString actionError READ actionError NOTIFY changed)
public:
    explicit RpcClient(QString, QObject* parent=nullptr);
    ~RpcClient() override;
    bool connected() const { return socket_.state()==QLocalSocket::ConnectedState; }
    bool apiCompatible() const { return apiCompatible_; }
    bool stale() const { return stale_; }
    bool demoAdapter() const { return agentInfo_.value("adapter").toString()=="demo"; }
    QVariantList tasks() const;
    QVariantList profiles() const { return profiles_; }
    QVariantList attempts() const { return attempts_; }
    QVariantList files() const { return files_; }
    QVariantList ranges() const { return ranges_; }
    QVariantMap capabilities() const { return capabilities_; }
    QVariantMap agentInfo() const { return agentInfo_; }
    QVariantMap checks() const { return checks_; }
    QString detailStatus() const { return detailStatus_; }
    QString detailWatermark() const { return detailSnapshot_.isEmpty() ? QString("未建立") : detailSnapshot_.left(8)+" · epoch "+detailEpoch_.left(8)+" · rev "+QString::number(detailRevision_)+" · seq "+QString::number(detailSequence_); }
    QString message() const { return message_; }
    bool createInFlight() const { return createInFlight_; }
    QString createStatus() const { return createStatus_; }
    QString createError() const { return createError_; }
    bool removeInFlight() const { return removeInFlight_; }
    QString removeStatus() const { return removeStatus_; }
    QString removeError() const { return removeError_; }
    bool actionInFlight() const { return actionInFlight_; }
    QString actionTransferId() const { return actionTransferId_; }
    QString actionStatus() const { return actionStatus_; }
    QString actionError() const { return actionError_; }
    Q_INVOKABLE void create(QString,QString,QString,bool,QString,QString);
    Q_INVOKABLE void action(QString,QVariantMap);
    Q_INVOKABLE void saveProfile(QVariantMap,bool);
    Q_INVOKABLE void testNode(QString,QString);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void demo();
    Q_INVOKABLE void loadDetails(QString,QString);
    Q_INVOKABLE void loadNextFiles();
    Q_INVOKABLE void loadNextRanges();
    Q_INVOKABLE void removeTask(QString,QString);
    Q_INVOKABLE void requestCapabilities(QString);
    Q_INVOKABLE void setPage(int p) { emit pageRequested(p); }
    int snapshotCount() const { return tasks().size(); }
signals:
    void changed();
    void pageRequested(int);
    void createSucceeded(QString transferId, QString attemptId, qint64 revision);
    void taskRemoved(QString transferId);
private:
    QString sendRequest(QString, QJsonObject, QString transferId = {});
    void send(QString,QJsonObject);
    void sendPostHandshake(QString,QJsonObject);
    void read();
    void upsert(QJsonObject);
    void handlePage(QJsonObject);
    bool acceptWatermark(QJsonObject);
    void startSession();
    void checkPending();
    void loadHidden();
    void persistHidden() const;
    bool taskHidden(const QString&, const QString&) const;
    void failCreate(const QString&, bool retryable=true);
    void finishActionFailure(const QString&, const QString&);
    QString friendlyActionError(const QString&, const QString&) const;

    QLocalSocket socket_;
    QTimer retry_;
    QTimer requestTimer_;
    QString path_, epoch_, message_="连接本地 agent 中";
    qint64 sequence_=0, request_=0;
    QByteArray buffer_;
    QHash<QString,QString> pending_;
    QHash<QString,qint64> pendingDeadlines_;
    QHash<QString,QString> pendingTransferIds_;
    QVariantList tasks_,profiles_,attempts_,files_,ranges_;
    QVariantMap capabilities_,agentInfo_,checks_;
    QString detailTransfer_,detailAttempt_,detailFile_,detailCursorFiles_,detailCursorRanges_;
    QString detailSnapshot_,detailEpoch_,detailStatus_="未加载";
    qint64 detailRevision_=-1,detailSequence_=-1;
    QHash<QString,QString> detailPageSnapshots_, detailPageEpochs_;
    QHash<QString,qint64> detailPageRevisions_, detailPageSequences_;
    QSet<QString> hiddenDemo_, hiddenReal_;
    bool apiCompatible_=false, stale_=true;
    bool createInFlight_=false;
    QString createStatus_, createError_, currentCreateRequestId_;
    QString lastCreateOperationId_, lastCreateKey_;
    bool removeInFlight_=false;
    QString removeStatus_, removeError_;
    bool actionInFlight_=false;
    QString actionTransferId_, actionMethod_, actionStatus_, actionError_, lastCreatedTransferId_;
};
