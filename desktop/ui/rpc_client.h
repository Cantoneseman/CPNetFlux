#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QVariantList>
#include <QTimer>
#include <QHash>
#include <QDateTime>
class RpcClient : public QObject {
    Q_OBJECT
 Q_PROPERTY(bool creationUncertain READ creationUncertain NOTIFY changed)
 Q_PROPERTY(QVariantMap validation READ validation NOTIFY changed)
 Q_PROPERTY(bool busy READ busy NOTIFY changed)
 Q_PROPERTY(bool hasMoreFiles READ hasMoreFiles NOTIFY changed)
 Q_PROPERTY(bool hasMoreAttempts READ hasMoreAttempts NOTIFY changed)
 Q_PROPERTY(bool hasMoreRanges READ hasMoreRanges NOTIFY changed)
 Q_PROPERTY(QVariantMap networkBinding READ networkBinding NOTIFY changed)
 Q_PROPERTY(QString actionPending READ actionPending NOTIFY changed)
 Q_PROPERTY(QString socketPath READ socketPath CONSTANT)
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
 Q_PROPERTY(QVariantMap networkInventory READ networkInventory NOTIFY changed)
 Q_PROPERTY(QVariantMap networkPlan READ networkPlan NOTIFY changed)
 Q_PROPERTY(QVariantMap networkApply READ networkApply NOTIFY changed)
 Q_PROPERTY(QVariantMap networkVerify READ networkVerify NOTIFY changed)
 Q_PROPERTY(QVariantMap agentInfo READ agentInfo NOTIFY changed)
 Q_PROPERTY(QVariantMap contract READ contract NOTIFY changed)
 Q_PROPERTY(bool contractReady READ contractReady NOTIFY changed)
 Q_PROPERTY(QString apiVersion READ apiVersion NOTIFY changed)
 Q_PROPERTY(QString runtimeProtocol READ runtimeProtocol NOTIFY changed)
 Q_PROPERTY(QString adapterName READ adapterName NOTIFY changed)
 Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY changed)
 Q_PROPERTY(bool lastErrorRetryable READ lastErrorRetryable NOTIFY changed)
 Q_PROPERTY(QVariantMap evidence READ evidence NOTIFY changed)
 Q_PROPERTY(QVariantMap checks READ checks NOTIFY changed)
 Q_PROPERTY(QString authenticationState READ authenticationState NOTIFY changed)
 Q_PROPERTY(QString detailStatus READ detailStatus NOTIFY changed)
 Q_PROPERTY(QString detailWatermark READ detailWatermark NOTIFY changed)
 Q_PROPERTY(QString message READ message NOTIFY changed)
public:
 bool creationUncertain()const{return creationUncertain_;}
 QVariantMap validation()const{return validation_;}
 bool busy()const{return busy_;}
 bool hasMoreFiles()const{return !detailCursorFiles_.isEmpty();}
 bool hasMoreRanges()const{return !detailCursorRanges_.isEmpty();}
 bool hasMoreAttempts()const{return !detailCursorAttempts_.isEmpty();}
 QVariantMap networkBinding()const{return networkBinding_;}
 QString actionPending()const{return actionPending_;}
 QString socketPath()const{return path_;}
 Q_INVOKABLE bool supports(QString method)const;
 Q_INVOKABLE void confirmCreate();
 Q_INVOKABLE void invalidateDraft();
 Q_INVOKABLE void selectFile(QVariantMap file);
 Q_INVOKABLE void loadNextAttempts();
 Q_INVOKABLE void clearNetwork(QString node,QString plan);
 Q_INVOKABLE QString localPath(QString url)const;
 explicit RpcClient(QString,QObject* parent=nullptr);~RpcClient()override;
 bool connected()const{return socket_.state()==QLocalSocket::ConnectedState;}bool apiCompatible()const{return apiCompatible_;}bool stale()const{return stale_;}bool demoAdapter()const{return agentInfo_.value("adapter").toString()=="demo";}
 QVariantList tasks()const{return tasks_;}QVariantList profiles()const{return profiles_;}QVariantList attempts()const{return attempts_;}QVariantList files()const{return files_;}QVariantList ranges()const{return ranges_;}QVariantMap capabilities()const{return capabilities_;}QVariantMap networkInventory()const{return networkInventory_;}QVariantMap networkPlan()const{return networkPlan_;}QVariantMap networkApply()const{return networkApply_;}QVariantMap networkVerify()const{return networkVerify_;}QVariantMap agentInfo()const{return agentInfo_;}QVariantMap contract()const{return contract_;}bool contractReady()const{return contractReady_;}QString apiVersion()const{return agentInfo_.value("api_version").toString();}QString runtimeProtocol()const{return agentInfo_.value("runtime_protocol").toString().isEmpty()?contract_.value("runtime_protocol").toString():agentInfo_.value("runtime_protocol").toString();}QString adapterName()const{return agentInfo_.value("adapter").toString().isEmpty()?contract_.value("adapter").toString():agentInfo_.value("adapter").toString();}QString lastErrorCode()const{return lastErrorCode_;}bool lastErrorRetryable()const{return lastErrorRetryable_;}QVariantMap evidence()const{return evidence_;}QVariantMap checks()const{return checks_;}QString authenticationState()const{return authenticationState_;}QString detailStatus()const{return detailStatus_;}QString detailWatermark()const{return detailFileSnapshot_.isEmpty()?QString("未建立"):detailFileSnapshot_.left(8)+" · epoch "+detailFileEpoch_.left(8)+" · rev "+QString::number(detailFileRevision_)+" · seq "+QString::number(detailFileSequence_);}QString message()const{return message_;}
 Q_INVOKABLE void create(QString,QString,QString,bool,QString,QString,QVariantMap);Q_INVOKABLE void inspectNetwork(QString);Q_INVOKABLE void planNetwork(QString,QString,int,QVariantList,QVariantList);Q_INVOKABLE void applyNetwork(QString,QString,QString,bool);Q_INVOKABLE void verifyNetwork(QString,QString);Q_INVOKABLE void importProvisioningBundle(QString);Q_INVOKABLE void action(QString,QVariantMap);Q_INVOKABLE void saveProfile(QVariantMap,bool);Q_INVOKABLE void testNode(QString,QString);Q_INVOKABLE void invalidateAuthentication();Q_INVOKABLE void refresh();Q_INVOKABLE void demo();Q_INVOKABLE void loadDetails(QString,QString);Q_INVOKABLE void loadNextFiles();Q_INVOKABLE void loadNextRanges();Q_INVOKABLE void setPage(int p){emit pageRequested(p);}int snapshotCount()const{return tasks_.size();}
signals:
    void changed();
    void pageRequested(int);
private:
    void send(QString,QJsonObject);void sendPostHandshake(QString,QJsonObject);void read();void upsert(QJsonObject);void handlePage(QJsonObject);bool acceptWatermark(QJsonObject);void startSession();
 struct Request {QString method; QJsonObject params; qint64 deadline;};
 void fail(QString code,QString text,bool retryable=false);
 QLocalSocket socket_;QTimer retry_,deadlines_;QString path_,epoch_,message_="连接本地 agent 中",lastErrorCode_;qint64 sequence_=0,request_=0;QByteArray buffer_;QHash<QString,Request> pending_;QVariantList tasks_,profiles_,attempts_,files_,ranges_;QVariantMap capabilities_,agentInfo_,contract_,evidence_,checks_,networkInventory_,networkPlan_,networkApply_,networkVerify_;QString authenticationState_="unknown";QString detailTransfer_,detailAttempt_,detailFile_,detailCursorFiles_,detailCursorRanges_,detailSnapshot_,detailEpoch_,detailStatus_="未加载",detailAttemptSnapshot_,detailAttemptEpoch_,detailFileSnapshot_,detailFileEpoch_,detailRangeSnapshot_,detailRangeEpoch_;qint64 detailRevision_=-1,detailSequence_=-1,detailAttemptRevision_=-1,detailAttemptSequence_=-1,detailFileRevision_=-1,detailFileSequence_=-1,detailRangeRevision_=-1,detailRangeSequence_=-1;bool apiCompatible_=false,contractReady_=false,lastErrorRetryable_=false,stale_=true;QJsonObject pendingCreate_,pendingCreateOperation_;
 QVariantMap validation_,networkBinding_;
 bool busy_=false,creationUncertain_=false;
 QString actionPending_,detailCursorAttempts_;
 int detailGeneration_=0;
};
