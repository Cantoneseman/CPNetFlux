import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1180; height: 780; minimumWidth: 760; minimumHeight: 560
    visible: true
    title: "CPNetFlux · 真实传输客户端"
    color: "#f3f5fa"
    font.family: "Noto Sans CJK SC"
    font.pixelSize: 14
    property int page: initialPage
    property string selectedId: ""
    property string selectedProfileId: ""
    property bool pickerDirectory: false
    property var pendingRemoveTask: null
    function pickPath(directory) { pickerDirectory=directory; pathPicker.active=true; if(pathPicker.status===Loader.Ready){ pathPicker.item.directoryMode=directory; pathPicker.item.open() } }
    property var selectedTask: {
        for (var i=0;i<rpc.tasks.length;i++) if(rpc.tasks[i].transfer_id === selectedId) return rpc.tasks[i]
        return rpc.tasks.length ? rpc.tasks[0] : null
    }
    property var pageTitles: ["传输任务", "新建任务", "任务详情", "节点连接", "远端目录", "历史与恢复", "高级设置"]
    function known(v) { return v !== null && v !== undefined && v !== "" }
    function value(v, unit) { return known(v) ? String(v) + (unit || "") : "agent 未提供" }
    function seconds(v) { return known(v) ? Number(v).toFixed(3) + " 秒" : "agent 未提供" }
    function bytes(v) { return known(v) ? (Number(v)/1048576).toFixed(2)+" MiB" : "agent 未提供" }
    function metric(t, key) { return t && t.metrics ? t.metrics[key] : null }
    function stateName(s) {
        return ({queued:"已创建，等待启动", scanning:"正在扫描目录", connecting:"正在连接远端", negotiating:"正在协商传输", transferring:"正在传输", committing:"正在校验并提交", completed:"传输已完成", failed:"传输失败", cancelled:"已取消", recoverable:"可恢复", paused:"已暂停"})[s] || "等待 agent 提供状态"
    }
    function stateSummary(t) {
        if (!t) return "请选择一项任务。"
        return ({
            queued:"任务已在 agent 中建立，等待启动。",
            scanning:"正在扫描目录，文件数量可能仍在变化；请等待扫描和远端准备完成。",
            connecting:"正在连接远端并等待控制通道准备。",
            negotiating:"正在与远端协商传输能力和数据通道。",
            transferring:"agent 正在传输已发现的文件；速度和进度只在 agent 返回时显示。",
            committing:"正在校验 manifest、checksum 并提交传输结果。",
            completed:"agent 已返回完成状态，传输结果可以核验。",
            failed:"agent 返回失败状态，请查看错误详情并按恢复资格重试。",
            cancelled:"任务已取消；是否可恢复由 agent 返回的恢复资格决定。",
            recoverable:"agent 已保留恢复点，可以尝试恢复或查看失败原因。",
            paused:"任务已暂停；继续操作取决于 agent 的实际能力。"
        })[t.state] || "agent 尚未提供可理解的阶段状态。"
    }
    function phaseKey(t) {
        if (!t) return "scan"
        if (t.state === "queued") return "queued"
        if (t.state === "scanning") return "scan"
        if (t.state === "connecting" || t.state === "negotiating") return "prepare"
        if (t.state === "transferring") return "transfer"
        if (t.state === "committing") return "verify"
        if (t.state === "completed") return "done"
        if (t.integrity_status === "fail" || t.state === "failed") return "error"
        return "error"
    }
    function phaseStatus(t, key) {
        if (!t) return "pending"
        if (t.state === "completed") return "done"
        var current = phaseKey(t)
        if (current === "error") return key === "verify" && t.integrity_status === "fail" ? "error" : "pending"
        if (key === current) return "current"
        if (key === "scan" && t.scan_complete === true) return "done"
        return "pending"
    }
    function nextStep(t) {
        if (!t) return ""
        if (t.state === "queued") return "下一步：确认路径和节点后开始任务。"
        if (t.state === "scanning") return "下一步：等待目录扫描完成；agent 未提供总数时不显示百分比。"
        if (t.state === "connecting" || t.state === "negotiating") return "下一步：等待远端准备和数据连接。"
        if (t.state === "transferring") return "下一步：等待 agent 返回传输完成或错误。"
        if (t.state === "committing") return "下一步：等待 manifest、checksum 和 finalize 结果。"
        if (t.state === "completed") return "下一步：查看完整性结果或从列表隐藏。"
        if (t.state === "recoverable") return "下一步：使用 agent 提供的恢复能力继续任务。"
        if (t.state === "cancelled") return "下一步：如有恢复资格，可请求 agent 恢复。"
        return "下一步：查看错误详情并按 agent 返回的恢复资格处理。"
    }
    function integrityText(t, key) {
        var e = t && t.backend_evidence ? t.backend_evidence[key] : null
        if (!known(e)) return "agent 未提供此项证据"
        if (typeof e === "object" && known(e.status)) return String(e.status)
        return "agent 已返回"
    }
    function fraction(done,total) { return known(done) && known(total) && Number(total) > 0 ? Number(done)/Number(total) : 0 }
    function progressKnown(done,total) { return known(done) && known(total) && Number(total) > 0 }
    function terminal(s) { return ["completed","failed","cancelled","recoverable"].indexOf(s)>=0 }
    function selectTask(t) { selectedId=t.transfer_id; page=2 }
    function loadProfile(p) {
        nodeId.text=p.node_id; nodeName.text=p.name; host.text=p.host; port.text=p.control_port
        rootPath.text=p.default_root_id || "home"; ca.text=p.ca_path; serverName.text=p.server_name
        credentialRef.text=p.credential_ref || ""; auth.currentIndex=p.auth_mode==="admin_credential"?1:0
    }
    Connections {
        target: rpc
        function onPageRequested(p) { window.page=p }
        function onCreateSucceeded(transferId, attemptId, revision) {
            window.selectedId = transferId
            window.page = 2
            rpc.loadDetails(transferId, attemptId)
        }
    }

    Dialog {
        id: removeDialog
        modal: true
        title: "从本机任务列表隐藏"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var task: null
        width: 460
        contentItem: ColumnLayout {
            spacing: 10
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: removeDialog.task ? "将隐藏任务 “" + removeDialog.task.transfer_id + "”。这不会删除远端文件、目录或 agent 任务。" : ""
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#657389"
                text: "隐藏后会在本机重启和 agent 刷新后继续保持；demo 与真实任务分别保存。"
            }
        }
        onAccepted: {
            if (task) rpc.removeTask(task.transfer_id, task.adapter || (rpc.demoAdapter ? "demo" : "real"))
            task = null
        }
        onRejected: task = null
    }

    component Card: Rectangle {
        color: "white"; radius: 12; border.color: "#e2e7f0"
        implicitHeight: 100
    }
    component Muted: Label { color: "#657389"; wrapMode: Text.WordWrap }
    component Heading: Label { font.pixelSize: 21; font.bold: true; color: "#14233b" }

    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 195; Layout.minimumWidth: 176; Layout.maximumWidth: 210; color: "#15253d"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 12
                Label { text: "CPNetFlux"; color: "white"; font.pixelSize: 25; font.bold: true }
                Label { text: "真实传输客户端"; color: "#a9bed9"; font.pixelSize: 12 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#31435c"; Layout.topMargin: 14 }
                Repeater {
                    model: ["传输任务", "新建任务", "任务详情", "节点连接", "远端目录", "历史与恢复", "高级设置"]
                    Button {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 42; text: modelData
                        highlighted: false
                        background: Rectangle {
                            radius: 7
                            color: window.page===index ? "#dceaff" : (parent.hovered ? "#263d59" : "transparent")
                            border.color: window.page===index ? "#93b5e5" : "transparent"
                        }
                        contentItem: Label {
                            text: modelData
                            leftPadding: 14
                            verticalAlignment: Text.AlignVCenter
                            color: window.page===index ? "#17375f" : "#d2dbea"
                            font.bold: window.page===index
                            elide: Text.ElideRight
                        }
                        onClicked: window.page=index
                    }
                }
                Item { Layout.fillHeight: true }
                Label { Layout.fillWidth:true; text: "CPNetFlux\n本地传输客户端"; color:"#a9bed9"; font.pixelSize:11; wrapMode:Text.WordWrap }
            }
        }
        ColumnLayout {
            Layout.fillHeight:true; Layout.fillWidth:true; Layout.margins:28; spacing:16
            RowLayout {
                Layout.fillWidth:true
                Heading { text: window.pageTitles[window.page] }
                Item { Layout.fillWidth:true }
                Label { text: rpc.connected ? "● 本地 agent 已连接" : "● agent 离线 / 重连中"; color:rpc.connected?"#267a5a":"#a64b31" }
                Button { text:"刷新"; enabled:true; onClicked:rpc.refresh() }
                Label { visible:rpc.removeStatus.length>0; text:rpc.removeStatus; color:rpc.removeInFlight?"#8a5a18":"#36734f"; elide:Text.ElideRight }
            }
            Rectangle {
                Layout.fillWidth:true; implicitHeight:54; radius:8; color:rpc.demoAdapter?"#fff1d7":"#e4f3eb"
                Label { anchors.fill:parent; anchors.margins:12; text:rpc.demoAdapter ? "开发诊断/合成数据：不会读取或传输用户文件。" : (rpc.connected && rpc.apiCompatible ? "真实传输模式：任务状态、进度、校验和错误均来自 agent。" : "真实传输模式不可用：等待 agent 连接和 API 协商。"); color:rpc.demoAdapter?"#76501e":"#245d42"; wrapMode:Text.WordWrap; verticalAlignment:Text.AlignVCenter }
            }
            Muted { Layout.fillWidth:true; text:rpc.message; maximumLineCount:2; elide:Text.ElideRight; visible:rpc.message.length>0 }
            Muted { Layout.fillWidth:true; text:rpc.createError.length>0 ? rpc.createError : rpc.removeError; maximumLineCount:2; elide:Text.ElideRight; color:"#a64b31"; visible:text.length>0 }
            // The picker is loaded lazily so the client still starts with a clear
            // diagnostic when the distro has not installed qt6-qpa-plugins/dialogs.
            Loader { id:pathPicker; active:false; source:"qrc:/ui/PathPicker.qml"
                onLoaded: { item.directoryMode=window.pickerDirectory; item.selected.connect(function(path){ localPath.text=path; pathPicker.active=false }) ; item.open() }
            }
            StackLayout {
                currentIndex: window.page; Layout.fillWidth:true; Layout.fillHeight:true
                // 0: tasks
                ScrollView {
                    id: homeScroll
                    clip:true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width:homeScroll.availableWidth; spacing:14
                        RowLayout {
                            Button { text:"＋ 新建传输任务"; highlighted:true; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale && !rpc.createInFlight; onClicked:window.page=1 }
                            Label { text:rpc.tasks.length+" 项任务"; color:"#657389" }
                        }
                        Card {
                            visible:rpc.tasks.length===0; Layout.fillWidth:true; Layout.minimumWidth:homeScroll.availableWidth; implicitHeight:190
                            ColumnLayout { anchors.fill:parent; anchors.margins:20; spacing:12
                                Heading { text:"还没有传输任务" }
                                Muted { Layout.fillWidth:true; text:rpc.demoAdapter ? "开发诊断/合成数据不会访问本地目录或远端节点。" : "选择新建任务并填写两端路径。提交后可在此查看服务端返回的进度、结果和错误。" }
                                Button { text:rpc.demoAdapter ? "创建本地示例任务" : "新建传输任务"; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale && !rpc.createInFlight; highlighted:true; onClicked:rpc.demoAdapter ? rpc.demo() : window.page=1 }
                            }
                        }
                        Repeater {
                            model:rpc.tasks
                            Card {
                                required property var modelData
                                Layout.fillWidth:true; implicitHeight:165
                                ColumnLayout { anchors.fill:parent; anchors.margins:18; spacing:8
                                    RowLayout { Layout.fillWidth:true
                                        Label { text:(modelData.direction==="upload"?"↑ 上传":"↓ 下载")+" · "+window.stateName(modelData.state); font.bold:true; color:"#14233b" }
                                        Item { Layout.fillWidth:true }
                                        Label { text:rpc.demoAdapter?"开发诊断":"真实 agent 任务"; color:rpc.demoAdapter?"#8a5a18":"#36734f" }
                                        Button { text:"查看详情"; onClicked:window.selectTask(modelData) }
                                        Button { text:"从列表隐藏"; enabled:!rpc.removeInFlight; onClicked:{ removeDialog.task=modelData; removeDialog.open() } }
                                    }
                                    Muted { Layout.fillWidth:true; text:modelData.source.path+" → "+modelData.destination.path; elide:Text.ElideMiddle; maximumLineCount:1 }
                                    ProgressBar { visible:window.progressKnown(modelData.metrics.bytes_committed,modelData.metrics.bytes_total); Layout.fillWidth:true; value:window.fraction(modelData.metrics.bytes_committed,modelData.metrics.bytes_total) }
                                    RowLayout { Layout.fillWidth:true
                                        Label { text:window.bytes(modelData.metrics.bytes_committed)+" / "+window.bytes(modelData.metrics.bytes_total); color:"#44556e" }
                                        Item { Layout.fillWidth:true }
                                        Label { text:"文件 "+window.value(modelData.metrics.files_committed)+" / "+window.value(modelData.metrics.files_total); color:"#44556e" }
                                        Label { text:"实际模式 "+(window.known(modelData.actual_mode)?modelData.actual_mode.toUpperCase():"agent 未提供"); color:"#44556e" }
                                    }
                                }
                            }
                        }
                    }
                }
                // 1: create
                ScrollView { id:createScroll; clip:true; contentWidth:availableWidth
                    ColumnLayout { width:createScroll.availableWidth; spacing:14
                        Card { Layout.fillWidth:true; Layout.minimumWidth:createScroll.availableWidth; implicitHeight:410
                            ColumnLayout { anchors.fill:parent; anchors.margins:24; spacing:12
                                Heading { text:"新建传输任务" }
                                Muted { Layout.fillWidth:true; text:rpc.demoAdapter ? "填写的路径仅用于任务流程界面，不会被读取；当前连接不会访问任何节点。" : "填写本地路径与远端相对路径，然后提交到当前连接的传输服务。" }
                                GridLayout { columns:window.width<980?1:2; Layout.fillWidth:true; columnSpacing:18; rowSpacing:8
                                    Label { text:"方向" }
                                    ComboBox { id:direction; objectName:"direction"; model:["本机 → 远端（上传）","远端 → 本机（下载）"]; Layout.fillWidth:true }
                                    Label { text:"远端节点" }
                                    ComboBox { id:profileChoice; model:rpc.profiles.length ? rpc.profiles.map(function(p){return p.name+" · "+p.host+":"+p.control_port}) : ["请先在“节点连接”保存真实节点"]; enabled:!rpc.demoAdapter; Layout.fillWidth:true
                                        onActivated: {
                                            if (index >= 0 && index < rpc.profiles.length) {
                                                window.selectedProfileId = rpc.profiles[index].node_id
                                                rpc.requestCapabilities(window.selectedProfileId)
                                            }
                                        }
                                    }
                                    Label { text:"本地文件或目录" }
                                    RowLayout { Layout.fillWidth:true
                                        TextField { id:localPath; objectName:"localPath"; text:""; Layout.fillWidth:true; placeholderText:"绝对路径；agent 会在提交前校验" }
                                        Button { text:"文件…"; enabled:!rpc.demoAdapter; onClicked:window.pickPath(false) }
                                        Button { text:"目录…"; enabled:!rpc.demoAdapter; onClicked:window.pickPath(true) }
                                    }
                                    Label { text:"远端相对目录" }
                                    TextField { id:remotePath; text:"dataset"; Layout.fillWidth:true }
                                    Label { text:"请求引擎" }
                                    ComboBox { id:mode; model:rpc.demoAdapter ? ["自动选择","V1（合成）","V2（合成回退）"] : ["V2 目录传输"]; enabled:rpc.demoAdapter; Layout.fillWidth:true }
                                    Label { text:"后端能力" }
                                    Label { text:rpc.demoAdapter ? "开发诊断/合成数据" : (rpc.capabilities.modes ? "已从 agent 读取" : "未知/未提供"); color:"#657389"; Layout.fillWidth:true }
                                }
                                Muted { Layout.fillWidth:true; text:rpc.demoAdapter ? "开发诊断入口；不会读取本地路径。" : "默认要求 CA/TLS 与凭据引用。agent 会在创建前检查路径、权限、认证和后端能力；缺失字段显示未知。" }
                                Label { visible:rpc.createInFlight || rpc.createStatus.length>0; Layout.fillWidth:true; text:rpc.createStatus; color:rpc.createInFlight?"#8a5a18":"#36734f"; wrapMode:Text.WordWrap }
                                Label { visible:rpc.createError.length>0; Layout.fillWidth:true; text:rpc.createError; color:"#a64b31"; wrapMode:Text.WordWrap }
                                Button { objectName:"createButton"; text:rpc.createInFlight ? "正在创建…" : "创建传输任务"; highlighted:true; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale && !rpc.createInFlight && (rpc.demoAdapter || rpc.profiles.length>0)
                                    onClicked: rpc.create(direction.currentIndex===0?"upload":"download",localPath.text,remotePath.text,false,rpc.demoAdapter?["auto","v1","v2"][mode.currentIndex]:"v2",window.selectedProfileId)
                                }
                            }
                        }
                    }
                }
                // 2: detail
                ScrollView { id:detailScroll; objectName:"detailScroll"; clip:true; contentWidth:availableWidth
                    ColumnLayout { width:detailScroll.availableWidth; spacing:14
                        Muted { visible:!window.selectedTask; text:"尚无任务，请先创建一项传输任务。" }
                        Card { id:detailCard; property bool technicalExpanded: false; visible:!!window.selectedTask; Layout.fillWidth:true; implicitHeight:980
                            ColumnLayout { anchors.fill:parent; anchors.margins:22; spacing:12
                                Heading { text:window.selectedTask?window.stateName(window.selectedTask.state):"无任务" }
                                Muted { Layout.fillWidth:true; text:window.selectedTask ? (window.selectedTask.direction==="upload"?"上传":"下载")+" · "+window.selectedTask.source.path+" → "+window.selectedTask.destination.path : ""; elide:Text.ElideMiddle; maximumLineCount:2 }
                                Rectangle { Layout.fillWidth:true; implicitHeight:112; radius:10; color:window.selectedTask && ["failed","cancelled"].indexOf(window.selectedTask.state)>=0 ? "#fff0ee" : "#eef5ff"; border.color:window.selectedTask && ["failed","cancelled"].indexOf(window.selectedTask.state)>=0 ? "#edb8b0" : "#c5d9f5"
                                    ColumnLayout { anchors.fill:parent; anchors.margins:14; spacing:4
                                        Label { text:window.selectedTask?window.stateName(window.selectedTask.state):""; font.pixelSize:18; font.bold:true; color:"#17375f" }
                                        Muted { Layout.fillWidth:true; text:window.stateSummary(window.selectedTask) }
                                        Muted { Layout.fillWidth:true; text:window.nextStep(window.selectedTask) }
                                        Label { visible:window.selectedTask && window.selectedTask.state==="scanning"; text:"已发现文件数："+window.value(window.metric(window.selectedTask,"files_discovered"))+" · 等待远端准备"; color:"#8a5a18" }
                                    }
                                }
                                Label { visible:!!window.selectedTask; Layout.fillWidth:true; text:"阶段进度"; font.bold:true; color:"#274970" }
                                RowLayout { visible:!!window.selectedTask; Layout.fillWidth:true; spacing:8
                                    Repeater { model:[{key:"scan",name:"扫描目录"},{key:"prepare",name:"远端准备"},{key:"transfer",name:"传输数据"},{key:"verify",name:"校验清单"},{key:"done",name:"提交完成"}]
                                        Rectangle { required property var modelData; Layout.fillWidth:true; Layout.preferredHeight:58; radius:8
                                            property string phaseState:window.phaseStatus(window.selectedTask,modelData.key)
                                            color:phaseState==="current"?"#dceaff":(phaseState==="done"?"#e4f3eb":(phaseState==="error"?"#fff0ee":"#f4f6fa"))
                                            border.color:phaseState==="current"?"#80a9df":(phaseState==="done"?"#9bcfb3":(phaseState==="error"?"#edb8b0":"#e2e7f0"))
                                            ColumnLayout { anchors.fill:parent; anchors.margins:7; spacing:2
                                                Label { Layout.fillWidth:true; text:modelData.name; horizontalAlignment:Text.AlignHCenter; font.bold:true; color:"#274970"; elide:Text.ElideRight }
                                                Label { Layout.fillWidth:true; text:phaseState==="current"?"进行中":(phaseState==="done"?"已完成":(phaseState==="error"?"失败":"待 agent 提供")); horizontalAlignment:Text.AlignHCenter; color:"#657389"; font.pixelSize:11 }
                                            }
                                        }
                                    }
                                }
                                Label { visible:!!window.selectedTask; Layout.fillWidth:true; text:"文件进度"; font.bold:true; color:"#274970" }
                                Label { visible:!!window.selectedTask; text:window.progressKnown(window.metric(window.selectedTask,"files_committed"),window.metric(window.selectedTask,"files_total")) ? "已提交 "+window.value(window.metric(window.selectedTask,"files_committed"))+" / "+window.value(window.metric(window.selectedTask,"files_total")) : "文件进度：agent 未提供（不推算百分比）"; color:"#44556e" }
                                ProgressBar { visible:window.selectedTask && window.progressKnown(window.metric(window.selectedTask,"files_committed"),window.metric(window.selectedTask,"files_total")); Layout.fillWidth:true; value:window.fraction(window.metric(window.selectedTask,"files_committed"),window.metric(window.selectedTask,"files_total")) }
                                Label { visible:!!window.selectedTask; Layout.fillWidth:true; text:"字节进度"; font.bold:true; color:"#274970" }
                                Label { visible:!!window.selectedTask; text:window.progressKnown(window.metric(window.selectedTask,"bytes_committed"),window.metric(window.selectedTask,"bytes_total")) ? "已传输 "+window.bytes(window.metric(window.selectedTask,"bytes_transferred"))+" · 已提交 "+window.bytes(window.metric(window.selectedTask,"bytes_committed"))+" / "+window.bytes(window.metric(window.selectedTask,"bytes_total")) : "字节进度：agent 未提供（不推算百分比、速度或 ETA）"; color:"#44556e" }
                                ProgressBar { visible:window.selectedTask && window.progressKnown(window.metric(window.selectedTask,"bytes_committed"),window.metric(window.selectedTask,"bytes_total")); Layout.fillWidth:true; value:window.fraction(window.metric(window.selectedTask,"bytes_committed"),window.metric(window.selectedTask,"bytes_total")) }
                                Rectangle { visible:!!window.selectedTask; Layout.fillWidth:true; implicitHeight:72; radius:8; color:"#f7f9fc"; border.color:"#e2e7f0"
                                    ColumnLayout { anchors.fill:parent; anchors.margins:12; spacing:3
                                        Label { text:"完整性核验"; font.bold:true; color:"#274970" }
                                        Muted { Layout.fillWidth:true; text:"manifest："+window.integrityText(window.selectedTask,"manifest")+" · checksum："+window.integrityText(window.selectedTask,"checksum")+" · finalize："+window.integrityText(window.selectedTask,"finalize") }
                                    }
                                }
                                Label { visible:!!window.selectedTask && !!window.selectedTask.error; Layout.fillWidth:true; wrapMode:Text.WordWrap; text:window.selectedTask && window.selectedTask.error ? "错误："+window.selectedTask.error.message : ""; color:"#b34236" }
                                Label { visible:!!window.selectedTask && rpc.actionTransferId===window.selectedTask.transfer_id && (rpc.actionStatus.length>0 || rpc.actionError.length>0); Layout.fillWidth:true; wrapMode:Text.WordWrap; text:rpc.actionError.length>0 ? rpc.actionError : rpc.actionStatus; color:rpc.actionError.length>0?"#a64b31":"#8a5a18" }
                                RowLayout { visible:!!window.selectedTask; Layout.fillWidth:true
                                    Button { objectName:"startButton"; text:rpc.actionInFlight && rpc.actionTransferId===(window.selectedTask ? window.selectedTask.transfer_id : "") ? "启动中…" : "开始任务"; highlighted:true; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale && !rpc.actionInFlight && window.selectedTask && window.selectedTask.state==="queued"; onClicked:rpc.action("transfer.start",window.selectedTask) }
                                    Button { text:"取消任务"; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale && !rpc.actionInFlight && window.selectedTask && !window.terminal(window.selectedTask.state); onClicked:rpc.action("transfer.cancel",window.selectedTask) }
                                    Button { text:"恢复任务"; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale && !rpc.actionInFlight && !!rpc.capabilities.resume_supported && window.selectedTask && window.selectedTask.recovery.eligibility==="eligible" && window.terminal(window.selectedTask.state); onClicked:rpc.action("transfer.resume",window.selectedTask) }
                                    Button { text:"从列表隐藏"; enabled:!rpc.removeInFlight; onClicked:{ removeDialog.task=window.selectedTask; removeDialog.open() } }
                                }
                                Button { visible:!!window.selectedTask; text:detailCard.technicalExpanded?"收起技术详情":"展开技术详情"; onClicked:detailCard.technicalExpanded=!detailCard.technicalExpanded }
                                ColumnLayout { visible:detailCard.technicalExpanded; Layout.fillWidth:true; spacing:7
                                    Rectangle { Layout.fillWidth:true; height:1; color:"#e2e7f0" }
                                    Label { text:"技术详情（原样保留 agent 返回值）"; font.bold:true; color:"#274970" }
                                    RowLayout { Layout.fillWidth:true
                                        Button { text:"加载 attempt / file 分页"; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale; onClicked:rpc.loadDetails(window.selectedTask.transfer_id,window.selectedTask.attempt.attempt_id) }
                                        Button { text:"继续加载 file"; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale; onClicked:rpc.loadNextFiles() }
                                        Button { text:"加载 range"; enabled:rpc.connected && rpc.apiCompatible && !rpc.stale; onClicked:rpc.loadNextRanges() }
                                    }
                                    Label { Layout.fillWidth:true; text:"attempt："+(rpc.attempts.length>0?rpc.attempts.length:"agent 未提供")+" · file："+(rpc.files.length>0?rpc.files.length:"agent 未提供")+" · range："+(rpc.ranges.length>0?rpc.ranges.length:"agent 未提供"); color:"#657389" }
                                    Muted { Layout.fillWidth:true; text:window.selectedTask ? "请求引擎："+window.value(window.selectedTask.policy ? window.selectedTask.policy.requested_mode : null)+" · 实际引擎："+window.value(window.selectedTask.actual_mode)+" · 调度请求："+window.value(window.selectedTask.policy ? window.selectedTask.policy.scheduler : null)+" · 实际调度："+window.value(window.selectedTask.actual_scheduler)+" · range 策略："+window.value(window.selectedTask.actual_range_policy) : "" }
                                    Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:"#a56b20"; text:window.selectedTask && window.selectedTask.fallback_reason ? "回退原因："+window.selectedTask.fallback_reason.message : "回退原因：agent 未提供（尚无或未协商）" }
                                    Muted { Layout.fillWidth:true; text:window.selectedTask ? "通道："+window.value(window.metric(window.selectedTask,"channel_count_active"))+" · 数据连接："+window.value(window.metric(window.selectedTask,"data_connections"))+" · 队列："+window.value(window.metric(window.selectedTask,"queue_depth_current")) : "" }
                                    Muted { Layout.fillWidth:true; wrapMode:Text.WordWrap; text:window.selectedTask && window.selectedTask.backend_evidence && window.selectedTask.backend_evidence.phase_timing ? "阶段耗时：control_prepare "+window.seconds(window.selectedTask.backend_evidence.phase_timing.control_prepare)+" · data_connect "+window.seconds(window.selectedTask.backend_evidence.phase_timing.data_connect)+" · first_payload "+window.seconds(window.selectedTask.backend_evidence.phase_timing.first_payload)+" · payload_io "+window.seconds(window.selectedTask.backend_evidence.phase_timing.payload_io)+" · transfer_complete_wait "+window.seconds(window.selectedTask.backend_evidence.phase_timing.transfer_complete_wait) : "阶段耗时：agent 未提供" }
                                    Muted { Layout.fillWidth:true; text:"恢复资格："+(window.selectedTask && window.selectedTask.recovery ? window.value(window.selectedTask.recovery.eligibility) : "agent 未提供")+" · attempt："+(window.selectedTask && window.selectedTask.attempt ? window.value(window.selectedTask.attempt.attempt_number) : "agent 未提供") }
                                    Muted { Layout.fillWidth:true; text:"详情状态："+rpc.detailStatus+" · "+(rpc.stale?"连接已断开，等待重连":"当前 agent epoch")+" · watermark："+window.value(rpc.detailWatermark) }
                                }
                            }
                        }
                    }
                }
                // 3: profiles
                ScrollView { id:profileScroll; clip:true; contentWidth:availableWidth
                    ColumnLayout { width:profileScroll.availableWidth; spacing:14
                        Card { Layout.fillWidth:true; implicitHeight:580
                            ColumnLayout { anchors.fill:parent; anchors.margins:22; spacing:10
                                Heading { text:"节点连接配置" }
                                Muted { Layout.fillWidth:true; text:"接收节点必须先运行受控CPNetFlux服务端。安装两个GUI不等于能互传。当前只保存配置和凭据引用，不接收密码或token值。" }
                                ComboBox { Layout.fillWidth:true; model:rpc.profiles; textRole:"name"; displayText:currentIndex>=0 && currentIndex<rpc.profiles.length ? rpc.profiles[currentIndex].name : "已保存节点（选择以编辑）"; onActivated:window.loadProfile(rpc.profiles[currentIndex]) }
                                GridLayout { columns:window.width<1120?2:4; Layout.fillWidth:true; columnSpacing:12; rowSpacing:8
                                    Label { text:"节点ID" } TextField { id:nodeId; text:"lab-node"; Layout.fillWidth:true }
                                    Label { text:"名称" } TextField { id:nodeName; text:"实验室节点"; Layout.fillWidth:true }
                                    Label { text:"地址" } TextField { id:host; text:"node.example"; Layout.fillWidth:true }
                                    Label { text:"控制端口" } TextField { id:port; text:"22310"; validator:IntValidator { bottom:1; top:65535 } Layout.fillWidth:true }
                                    Label { text:"默认root" } TextField { id:rootPath; text:"home"; Layout.fillWidth:true }
                                    Label { text:"TLS主机名" } TextField { id:serverName; text:host.text; Layout.fillWidth:true }
                                    Label { text:"CA文件" } TextField { id:ca; text:"/home/student/.config/cpnetflux/ca.pem"; Layout.fillWidth:true; Layout.columnSpan:3 }
                                    Label { text:"身份方式" } ComboBox { id:auth; model:["短期token引用","管理员凭据引用"]; Layout.fillWidth:true }
                                    Label { text:"凭据引用" } TextField { id:credentialRef; text:"secret-service:cpnetflux/lab-node"; placeholderText:"仅引用，禁止填写凭据值"; Layout.fillWidth:true }
                                }
                                RowLayout {
                                    Button { text:"保存配置"; enabled:rpc.connected && port.acceptableInput; onClicked:{
                                        var update=false; for(var i=0;i<rpc.profiles.length;i++) if(rpc.profiles[i].node_id===nodeId.text) update=true
                                        rpc.saveProfile({node_id:nodeId.text,name:nodeName.text,host:host.text,control_port:parseInt(port.text),server_name:serverName.text,ca_path:ca.text,auth_mode:["token","admin_credential"][auth.currentIndex],credential_ref:credentialRef.text,default_root_id:rootPath.text},update)
                                    } }
                                    Button { text:"分步连接检查"; enabled:rpc.connected; onClicked:rpc.testNode(nodeId.text,rootPath.text) }
                                }
                                Repeater { model:[{key:"dns_control_port",name:"① DNS / 控制端口"},{key:"tls_identity",name:"② TLS服务端身份"},{key:"login",name:"③ 登录"},{key:"capabilities",name:"④ 能力协商"},{key:"directory_permission",name:"⑤ 目录读写授权"},{key:"data_port",name:"协商数据端口"}]
                                    Label { required property var modelData; text:modelData.name+"："+(rpc.checks[modelData.key] ? rpc.checks[modelData.key].state+" · "+rpc.checks[modelData.key].message : "尚未检查 / 未提供"); color:"#657389" }
                                }
                            }
                        }
                    }
                }
                // 4: directory
                Card { ColumnLayout { anchors.fill:parent; anchors.margins:24
                    Heading { text:"远端目录浏览" }
                    Muted { Layout.fillWidth:true; text:"目录浏览仅在 agent 提供真实 directory.list 能力后启用；当前未提供时保持禁用。" }
                    TextField { placeholderText:"root-relative 路径"; Layout.fillWidth:true; enabled:false }
                    Button { text:"浏览 · 未接入"; enabled:false }
                    Item { Layout.fillHeight:true }
                } }
                // 5: history
                ScrollView { id:historyScroll; clip:true; contentWidth:availableWidth
                    ColumnLayout { width:historyScroll.availableWidth; spacing:12
                        Muted { Layout.fillWidth:true; text:"历史由 agent 持久保存。恢复按钮只在 agent 返回真实 checkpoint 时启用；未知字段保持未知。" }
                        Repeater { model:rpc.tasks
                            Card { required property var modelData; Layout.fillWidth:true; implicitHeight:95; visible:window.terminal(modelData.state)
                                RowLayout { anchors.fill:parent; anchors.margins:18
                                    ColumnLayout { Layout.fillWidth:true
                                        Label { text:window.stateName(modelData.state)+(rpc.demoAdapter?" · 开发诊断":" · 真实 agent"); font.bold:true }
                                        Muted { text:modelData.source.path; elide:Text.ElideMiddle; Layout.fillWidth:true }
                                    }
                                    Label { text:modelData.recovery.eligibility==="eligible"?"可恢复":"不可恢复"; color:"#657389" }
                                    Button { text:"查看"; onClicked:window.selectTask(modelData) }
                                    Button { text:"从列表隐藏"; enabled:!rpc.removeInFlight; onClicked:{ removeDialog.task=modelData; removeDialog.open() } }
                                }
                            }
                        }
                    }
                }
                // 6: advanced
                Card { ColumnLayout { anchors.fill:parent; anchors.margins:24; spacing:16
                    Heading { text:"引擎与调度能力" }
                    Muted { Layout.fillWidth:true; text:"能力和阶段均来自 agent/backend evidence；未提供的 V3/range 字段显示未知，相关操作保持禁用。" }
                    Label { text:"当前能力："+(rpc.demoAdapter?"开发诊断/合成数据":"真实 agent 返回的能力事实") }
                    Label { text:"checksum / resume / data TLS：以 agent capability 与任务 effective_policy 为准"; color:"#a46a1b" }
                    Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; text:"未提供的 V3/range 能力保持禁用，不在 UI 内推算进度。"; color:"#a46a1b" }
                    ComboBox { model:["通道自动选择（请求）"]; enabled:false }
                    ComboBox { model:["range policy：agent 未提供"]; enabled:false; Layout.fillWidth:true }
                    Muted { Layout.fillWidth:true; text:"速度、ETA和阶段耗时未观测时显示未知。暂停不支持；恢复按钮只用于agent实际实现的demo checkpoint。真实安全策略须由权威底层协商后回读。" }
                    Item { Layout.fillHeight:true }
                } }
            }
        }
    }
}
