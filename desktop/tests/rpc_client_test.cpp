#include "../ui/rpc_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <functional>
#include <stdexcept>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static bool until(std::function<bool()> f) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed()<1500) {
        QCoreApplication::processEvents();
        if (f()) return true;
        QThread::msleep(1);
    }
    return f();
}
struct Peer {
    QTemporaryDir dir; QLocalServer server; QLocalSocket* socket=nullptr;
    QByteArray buffer; QList<QJsonObject> requests;
    QString path() { return dir.path()+"/agent.sock"; }
    Peer() {
        require(server.listen(path()),"test socket listen failed");
        QObject::connect(&server,&QLocalServer::newConnection,[this]{
            socket=server.nextPendingConnection();
            QObject::connect(socket,&QLocalSocket::readyRead,[this]{
                buffer+=socket->readAll();
                while(buffer.contains('\n')){
                    auto at=buffer.indexOf('\n'); auto line=buffer.left(at);buffer.remove(0,at+1);
                    requests.append(QJsonDocument::fromJson(line).object());
                }
            });
        });
    }
    QJsonObject take(QString method) {
        require(until([&]{for(auto r:requests)if(r["method"].toString()==method)return true;return false;}),"expected RPC missing");
        for(int i=0;i<requests.size();++i)if(requests[i]["method"].toString()==method)return requests.takeAt(i);
        return {};
    }
    void response(QJsonObject request, QJsonObject result) { write({{"jsonrpc","2.0"},{"id",request["id"]},{"result",result}}); }
    void write(QJsonObject message) {socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact)+"\n");socket->flush();}
    void event(QJsonObject payload, int sequence, QString subscription="sub", QString type="transfer.state") {
        write({{"jsonrpc","2.0"},{"method","transfer.event"},{"params",QJsonObject{
            {"agent_epoch","epoch-1"},{"subscription_id",subscription},{"sequence",sequence},{"type",type},
            {"transfer_id",payload["transfer_id"]},{"attempt_id",payload["attempt"].toObject()["attempt_id"]},
            {"revision",payload["revision"]},{"payload",payload}}}});
    }
    void handshake(RpcClient& client, QString epoch="epoch-1") {
        auto info=take("agent.getInfo");
        response(info,{{"kind","agent_info"},{"api_version","2.0.0"},{"agent_epoch",epoch},{"adapter","demo"}});
        auto sub=take("transfer.subscribe");
        response(sub,{{"kind","subscription"},{"agent_epoch",epoch},{"subscription_id","sub"},
                      {"sequence",0},{"reset",true},{"snapshots",QJsonArray{}}});
        require(until([&]{return client.apiCompatible()&&!client.stale();}),"initial subscription failed");
    }
};
static QJsonObject task(QString id, QString attempt, int revision) {
    return {{"transfer_id",id},{"revision",revision},{"attempt",QJsonObject{{"attempt_id",attempt},{"attempt_number",1}}}};
}
static QJsonObject page(QString kind, QString snapshot, QJsonArray items, QJsonValue cursor=QJsonValue()) {
    return {{"kind",kind},{"transfer_id","t1"},{"attempt_id",kind=="attempt_list"?QJsonValue():QJsonValue("a1")},
            {"snapshot_id",snapshot},{"agent_epoch","epoch-1"},{"revision",1},{"snapshot_sequence",0},
            {"items",items},{"next_cursor",cursor}};
}
static void refresh_and_restart() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    client.refresh();auto sub=peer.take("transfer.subscribe");
    peer.response(sub,{{"kind","subscription"},{"agent_epoch","epoch-2"},{"subscription_id","sub-2"},
        {"sequence",9},{"reset",true},{"snapshots",QJsonArray{task("t1","a1",1)}}});
    require(until([&]{return !client.stale()&&client.snapshotCount()==1;}),"refresh loop / epoch reset did not converge");
    // A restart can reuse low sequence numbers; handshake must request a reset.
    peer.socket->disconnectFromServer();
    require(until([&]{return !client.connected();}),"disconnect did not mark UI offline");
    auto info=peer.take("agent.getInfo");
    peer.response(info,{{"kind","agent_info"},{"api_version","2.0.0"},{"agent_epoch","epoch-3"},{"adapter","demo"}});
    sub=peer.take("transfer.subscribe");
    require(sub["params"].toObject()["after_sequence"].isNull(),"restart reused previous epoch sequence");
    peer.response(sub,{{"kind","subscription"},{"agent_epoch","epoch-3"},{"subscription_id","sub-3"},
        {"sequence",0},{"reset",true},{"snapshots",QJsonArray{task("t2","a2",1)}}});
    require(until([&]{return !client.stale()&&client.tasks().first().toMap()["transfer_id"]=="t2";}),"restart snapshots not replaced");
}
static void detail_and_events() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    client.loadDetails("t1","a1");
    auto attempts=peer.take("attempt.list");auto files=peer.take("file.list");
    peer.response(attempts,page("attempt_list","attempt-snapshot",QJsonArray{QJsonObject{{"attempt_id","a1"}}}));
    peer.response(files,page("file_list","file-snapshot",QJsonArray{QJsonObject{{"transfer_id","t1"},{"attempt_id","a1"},{"file_id","f1"},{"generation",7}}}));
    require(until([&]{return client.files().size()==1&&client.attempts().size()==1;}),"independent entity snapshots discarded");
    client.loadNextRanges();auto range=peer.take("range.list");
    require(range["params"].toObject()["generation"].toInt()==7,"range query ignored file generation");
    auto rp=page("range_list","range-snapshot",QJsonArray{QJsonObject{{"transfer_id","t1"},{"attempt_id","a1"},{"file_id","f1"},{"generation",7},{"range_id","r1"}}});
    rp["file_id"]="f1";rp["generation"]=7;peer.response(range,rp);
    require(until([&]{return client.ranges().size()==1;}),"valid ranges discarded");
    peer.event(task("t2","a2",3),1);
    require(until([&]{return client.snapshotCount()==1&&client.tasks().first().toMap()["transfer_id"]=="t2";}),"selected detail filtered other task event");
}
static void superseded_pages() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    client.loadDetails("t1","a1");auto old=peer.take("file.list");peer.take("attempt.list");
    client.loadDetails("t1","a1");auto current=peer.take("file.list");peer.take("attempt.list");
    peer.response(old,page("file_list","old",QJsonArray{QJsonObject{{"transfer_id","t1"},{"attempt_id","a1"},{"file_id","wrong"},{"generation",1}}}));
    peer.response(current,page("file_list","new",QJsonArray{QJsonObject{{"transfer_id","t1"},{"attempt_id","a1"},{"file_id","right"},{"generation",8}}}));
    require(until([&]{return client.files().size()==1;}),"detail page did not arrive");
    require(client.files().first().toMap()["file_id"]=="right","delayed pre-refresh page contaminated details");
    client.loadDetails("t1","a1");auto paged=peer.take("file.list");peer.take("attempt.list");
    peer.response(paged,page("file_list","bound",QJsonArray{QJsonObject{{"transfer_id","t1"},{"attempt_id","a1"},{"file_id","f1"},{"generation",8}}},"cursor"));
    require(until([&]{return client.files().size()==1;}),"first page missing");
    client.loadNextFiles();auto next=peer.take("file.list");
    peer.response(next,page("file_list","different",QJsonArray{QJsonObject{{"transfer_id","t1"},{"attempt_id","a1"},{"file_id","bad"},{"generation",8}}}));
    require(until([&]{return client.detailStatus().contains("不一致");}),"mismatched cursor snapshot not rejected");
    require(client.files().size()==1,"mismatched cursor appended items");
}
static void sequence_gap() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    peer.event(task("t1","a1",1),3);
    auto reset=peer.take("transfer.subscribe");
    require(client.stale(),"gap did not mark stale");
    peer.response(reset,{{"kind","subscription"},{"agent_epoch","epoch-1"},{"subscription_id","sub-2"},
        {"sequence",3},{"reset",true},{"snapshots",QJsonArray{task("t1","a1",2)}}});
    require(until([&]{return !client.stale()&&client.snapshotCount()==1;}),"gap reset did not converge");
}

static void completion_and_stale_actions() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    auto incomplete=task("t1","a1",1);incomplete["state"]="completed";
    peer.event(incomplete,1);
    require(until([&]{return client.message().contains("证据");}),"completed without proof accepted");
    require(client.snapshotCount()==0,"unsupported completed shown as success");
    client.refresh();auto reset=peer.take("transfer.subscribe");
    client.create("upload","/tmp/data","demo",false,"auto");
    client.action("transfer.start",QVariantMap{{"transfer_id","t1"},{"revision",1}});
    QCoreApplication::processEvents();
    for(auto request:peer.requests)require(request["method"].toString()!="transfer.create"&&request["method"].toString()!="transfer.start","stale UI sent mutating RPC");
    peer.response(reset,{{"kind","subscription"},{"agent_epoch","epoch-1"},{"subscription_id","sub-2"},
        {"sequence",1},{"reset",true},{"snapshots",QJsonArray{}}});
    require(until([&]{return !client.stale();}),"refresh after stale action did not sync");
    auto complete=task("t1","a1",2);
    complete["state"]="completed";complete["commit_confirmed"]=true;complete["scan_complete"]=true;
    complete["transfer_status"]="pass";complete["integrity_status"]="pass";complete["evidence_status"]="complete";
    complete["policy"]=QJsonObject{{"checksum_policy","required_crc32c"},{"control_tls","verify_ca"},{"data_tls","required"}};
    complete["metrics"]=QJsonObject{{"bytes_total",100},{"bytes_committed",100},{"files_total",2},
        {"files_committed",1},{"files_skipped",1},{"files_failed",0}};
    complete["effective_policy"]=QJsonObject{{"resolution","resolved"},{"mode","v2"},{"checksum","crc32c"},
        {"control_tls","verified"},{"data_tls","verified"}};
    complete["actual_mode"]="v2";complete["error"]=QJsonValue();
    peer.event(complete,2,"sub-2");
    require(until([&]{return client.snapshotCount()==1;}),"verified completion including skipped file rejected");
}

static QJsonObject completed(QString id) {
    auto complete=task(id,"a1",2);
    complete["state"]="completed";complete["commit_confirmed"]=true;complete["scan_complete"]=true;
    complete["transfer_status"]="pass";complete["integrity_status"]="pass";complete["evidence_status"]="complete";
    complete["policy"]=QJsonObject{{"checksum_policy","required_crc32c"},{"control_tls","verify_ca"},{"data_tls","required"}};
    complete["metrics"]=QJsonObject{{"bytes_total",100},{"bytes_committed",100},{"files_total",2},
        {"files_committed",1},{"files_skipped",1},{"files_failed",0}};
    complete["effective_policy"]=QJsonObject{{"resolution","resolved"},{"mode","v2"},{"checksum","crc32c"},
        {"control_tls","verified"},{"data_tls","verified"}};
    complete["actual_mode"]="v2";complete["error"]=QJsonValue();
    return complete;
}
static void forged_completion() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    int sequence=0;
    for (int i=0;i<5;++i) {
        auto bad=completed("bad-"+QString::number(i));
        if(i==0) bad["error"]=QJsonObject{{"code","internal_error"}};
        if(i==1) {auto m=bad["metrics"].toObject();m["bytes_total"]=-1;m["bytes_committed"]=-1;bad["metrics"]=m;}
        if(i==2) {auto e=bad["effective_policy"].toObject();e["checksum"]="none";bad["effective_policy"]=e;}
        if(i==3) {auto e=bad["effective_policy"].toObject();e["data_tls"]="off";bad["effective_policy"]=e;}
        if(i==4) {auto e=bad["effective_policy"].toObject();e["control_tls"]="failed";bad["effective_policy"]=e;}
        peer.event(bad,++sequence);
    }
    peer.event(task("sentinel","a1",1),++sequence);
    require(until([&]{for(auto t:client.tasks())if(t.toMap()["transfer_id"]=="sentinel")return true;return false;}),"completion events not consumed");
    require(client.snapshotCount()==1,"forged completion shown as success");
}
static void event_identity() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    auto original=task("t1","a1",1);
    peer.event(original,1,"sub","transfer.snapshot");
    require(until([&]{return client.snapshotCount()==1;}),"transfer.snapshot ignored");
    auto wrong=task("other","wrong",2);
    peer.write({{"jsonrpc","2.0"},{"method","transfer.event"},{"params",QJsonObject{
        {"agent_epoch","epoch-1"},{"subscription_id","sub"},{"sequence",2},{"type","transfer.state"},
        {"transfer_id","t1"},{"attempt_id","a1"},{"revision",2},{"payload",wrong}}}});
    peer.event(task("t1","same-number-new-id",3),3);
    peer.event(task("sentinel","a1",1),4);
    require(until([&]{for(auto t:client.tasks())if(t.toMap()["transfer_id"]=="sentinel")return true;return false;}),"identity events not consumed");
    require(client.snapshotCount()==2,"event envelope mismatch accepted");
    for(auto t:client.tasks())if(t.toMap()["transfer_id"]=="t1")
        require(t.toMap()["attempt"].toMap()["attempt_id"]=="a1","same attempt number changed identity");
}

static void page_item_identity() {
    Peer peer; RpcClient client(peer.path());peer.handshake(client);
    client.loadDetails("t1","a1");peer.take("attempt.list");auto files=peer.take("file.list");
    peer.response(files,page("file_list","files",QJsonArray{
        QJsonObject{{"transfer_id","other"},{"attempt_id","a1"},{"file_id","wrong"},{"generation",7}}}));
    require(until([&]{return client.detailStatus().contains("身份");}),"mismatched page item identity not rejected");
    require(client.files().isEmpty(),"foreign entity contaminated details");
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);int failed=0;
    for (auto test : {std::pair{"refresh_restart",refresh_and_restart},
                      std::pair{"details_events",detail_and_events},
                      std::pair{"superseded_pages",superseded_pages},
                      std::pair{"sequence_gap",sequence_gap},
                      std::pair{"completion_stale",completion_and_stale_actions},
                      std::pair{"forged_completion",forged_completion},
                      std::pair{"event_identity",event_identity},
                      std::pair{"page_item_identity",page_item_identity}}) {
        try {test.second();std::cout<<test.first<<" PASS\n";}
        catch(const std::exception& e){++failed;std::cerr<<test.first<<" FAIL: "<<e.what()<<"\n";}
    }
    return failed?1:0;
}
