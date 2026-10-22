import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1180; height: 780; minimumWidth: 760; minimumHeight: 560
    visible: true
    title: "CPNetFlux"
    color: "#f5f5f7"
    font.family: "Noto Sans CJK SC"
    font.pixelSize: 14
    palette.highlight: "#007aff"
    palette.highlightedText: "#ffffff"
    palette.button: "#ffffff"
    palette.window: "#f5f5f7"
    palette.base: "#ffffff"
    property int page: initialPage
    property string selectedId: ""
    property string loadedAttempt: ""
    property int detailTab: 0
    property var draftPolicy: ({})
    property bool defaultsLoaded: false
    property bool editingProfile: false
    property string pickerTarget: "local"
    property var manualPicks: []
    property var selectedTask: {
        for (var i=0; i<rpc.tasks.length; i++) if (rpc.tasks[i].transfer_id===selectedId) return rpc.tasks[i]
        return rpc.tasks.length ? rpc.tasks[0] : null
    }
    readonly property bool ready: rpc.connected && rpc.contractReady && !rpc.stale
    property var titles: ["传输任务", "新建传输", "任务详情", "连接设置", "远端目录", "历史与恢复", "网络绑定", "日志与诊断"]
    function value(v, suffix) { return v===null || v===undefined || v==="" ? "未知 / 未提供" : String(v)+(suffix || "") }
    function bytes(v) { return v===null || v===undefined ? "未知 / 未提供" : Number(v).toLocaleString(Qt.locale(), "f", 0)+" B" }
    function state(s) { return ({queued:"等待启动",scanning:"扫描中",connecting:"连接中",negotiating:"协商中",transferring:"传输中",committing:"提交中",completed:"已完成",failed:"失败",cancelled:"已取消",recoverable:"可恢复",cancelling:"正在取消"})[s] || value(s) }
    function terminal(s) { return ["completed","failed","cancelled","recoverable"].indexOf(s)>=0 }
    function statusColor(s) { return s==="completed" ? "#16815d" : s==="failed" ? "#c43735" : s==="cancelled" ? "#6e7075" : "#007aff" }
    function json(v) { return v===null || v===undefined ? "未知 / 未提供" : JSON.stringify(v,null,2) }
    function showTask(t) { selectedId=t.transfer_id; detailTab=0; page=2; loadSelected() }
    function loadSelected() {
        if (ready && selectedTask && page===2) {
            loadedAttempt=selectedTask.attempt.attempt_id
            rpc.loadDetails(selectedTask.transfer_id, loadedAttempt)
        }
    }
    function changedDraft() { rpc.invalidateDraft() }
    function setPolicy(key,v) { var d=Object.assign({},draftPolicy); d[key]=v; draftPolicy=d; changedDraft() }
    function fieldSchema(key) {
        return rpc.contract.policy_schema && rpc.contract.policy_schema.properties ? rpc.contract.policy_schema.properties[key] || {} : {}
    }
    function enumOptions(key) {
        var p=fieldSchema(key)
        if (p.enum) return p.enum
        if (p.const!==undefined) return [p.const]
        return []
    }
    function choosePath(target,directory) {
        pickerTarget=target; picker.directoryMode=directory; picker.active=true
        if (picker.status===Loader.Ready) { picker.item.directoryMode=directory; picker.item.open() }
    }
    function acceptPath(url) {
        var p=rpc.localPath(url)
        if(pickerTarget==="local") localPath.text=p
        else if(pickerTarget==="ca") ca.text=p
        else credential.text="file:"+p
        picker.active=false
    }
    function loadProfile(p) {
        rpc.invalidateAuthentication(); editingProfile=true
        nodeId.text=p.node_id; nodeName.text=p.name; host.text=p.host; port.text=String(p.control_port)
        serverName.text=p.server_name; rootId.text=p.default_root_id; ca.text=p.ca_path; credential.text=p.credential_ref
    }
    function resetProfile() {
        editingProfile=false; rpc.invalidateAuthentication()
        nodeId.clear();nodeName.clear();host.clear();serverName.clear();rootId.clear();ca.clear();credential.clear();port.text="2121"
    }
    onPageChanged: if(page===2) loadSelected()
    Connections {
        target: rpc
        function onPageRequested(p) { window.page=p }
        function onChanged() {
            if(rpc.contractReady && !window.defaultsLoaded) {
                window.draftPolicy=Object.assign({},rpc.contract.policy_defaults)
                window.defaultsLoaded=true
            }
            if(window.page===2 && window.selectedTask && window.loadedAttempt!==window.selectedTask.attempt.attempt_id)
                window.loadSelected()
        }
    }
    component Title: Label { color:"#202124"; font.pixelSize:20; font.bold:true; wrapMode:Text.WordWrap }
    component Muted: Label { color:"#74777f"; wrapMode:Text.Wrap; Layout.fillWidth:true }
    component Rule: Rectangle { Layout.fillWidth:true; implicitHeight:1; color:"#e3e4e8" }
    component Command: Button {
        implicitHeight:36
        padding:10
        ToolTip.visible:hovered && ToolTip.text.length>0
        ToolTip.delay:500
        implicitWidth:Math.max(36,contentItem.implicitWidth+20)
        background:Rectangle {
            radius:5
            color:!parent.enabled?"#e9e9ec":parent.highlighted?"#007aff":parent.down?"#e3e4e8":parent.hovered?"#f1f2f4":"#ffffff"
            border.color:parent.highlighted?"transparent":"#d9dbdf"
        }
        contentItem:Label {
            text:parent.text;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter
            color:!parent.enabled?"#a1a3aa":parent.highlighted?"white":"#424650";font.pixelSize:13
        }
    }
    component Field: TextField {
        Layout.fillWidth:true
        implicitHeight:38
        selectByMouse:true
        color:"#202124"
        background:Rectangle { color:parent.enabled ? "#ffffff" : "#eeeeef"; radius:5; border.color:parent.activeFocus ? "#007aff" : "#d9dbdf" }
    }
    component Pair: RowLayout {
        property string label: ""
        property string detail: ""
        Layout.fillWidth:true
        spacing:12
        Label { Layout.preferredWidth:140; Layout.maximumWidth:140; text:parent.label; color:"#74777f"; wrapMode:Text.Wrap }
        Label { Layout.fillWidth:true; text:parent.detail; color:"#202124"; wrapMode:Text.Wrap; textFormat:Text.PlainText }
    }
    component Dump: TextArea {
        Layout.fillWidth:true
        readOnly:true; selectByMouse:true; wrapMode:TextEdit.Wrap
        font.pixelSize:12; color:"#555a64"; padding:12
        background:Rectangle { color:"#edeef1"; radius:5 }
    }

    Loader {
        id:picker; active:false
        property bool directoryMode:true
        source:"qrc:/ui/PathPicker.qml"
        onLoaded: { item.directoryMode=directoryMode; item.selected.connect(window.acceptPath); item.open() }
    }
    Loader {
        id:bundlePicker; active:false; source:"qrc:/ui/ProvisioningPicker.qml"
        onLoaded: { item.selected.connect(function(path){rpc.importProvisioningBundle(path);bundlePicker.active=false});item.open() }
    }
    Dialog {
        id:confirmBinding
        title:"确认网络绑定"; modal:true; anchors.centerIn:parent
        width:Math.min(520,window.width-48); standardButtons:Dialog.Ok|Dialog.Cancel
        Label { width:parent.width; text:"节点："+window.value(rpc.networkPlan.node_id)+"\n方案："+window.value(rpc.networkPlan.plan_id)+"\n\n"+window.json(rpc.networkPlan.bindings); wrapMode:Text.Wrap }
        onAccepted:rpc.applyNetwork(rpc.networkPlan.node_id,rpc.networkPlan.plan_id,rpc.networkPlan.inventory_revision,true)
    }
    Dialog {
        id:confirmClear
        title:"清除当前 CPNetFlux 绑定？"; modal:true; anchors.centerIn:parent
        width:Math.min(480,window.width-48); standardButtons:Dialog.Ok|Dialog.Cancel
        Label { width:parent.width; text:window.json(rpc.networkBinding.binding); wrapMode:Text.Wrap }
        onAccepted:rpc.clearNetwork(rpc.networkBinding.node_id,rpc.networkBinding.binding.plan_id)
    }
    Rectangle { anchors.fill:parent; color:"#f5f5f7"; z:-1 }
    RowLayout {
        anchors.fill:parent; spacing:0
        Rectangle {
            Layout.fillHeight:true; Layout.preferredWidth:window.width<950?172:218
            color:"#ffffff"
            Rectangle { anchors.right:parent.right; width:1; height:parent.height; color:"#e7e8ec" }
            ColumnLayout {
                anchors.fill:parent; anchors.margins:18; spacing:7
                Label { text:"CPNetFlux"; font.pixelSize:23; font.bold:true; color:"#202124"; Layout.topMargin:12 }
                Label { text:rpc.demoAdapter?"开发诊断 · 合成数据":"真实传输模式"; color:rpc.demoAdapter?"#a06118":"#74777f"; font.pixelSize:12; Layout.bottomMargin:28 }
                Repeater {
                    model:[{p:0,n:"传输任务",i:"folder-download"},{p:1,n:"新建传输",i:"list-add"},{p:5,n:"历史与恢复",i:"document-revert"},{p:3,n:"连接设置",i:"network-server"},{p:6,n:"网络绑定",i:"network-wired"},{p:7,n:"日志与诊断",i:"utilities-system-monitor"}]
                    Button {
                        required property var modelData
                        Layout.fillWidth:true; implicitHeight:42
                        text:modelData.n; icon.name:modelData.i
                        display:window.width<950?AbstractButton.TextOnly:AbstractButton.TextBesideIcon
                        contentItem:Label { text:parent.text; color:window.page===modelData.p || (window.page===2&&modelData.p===0) ? "#007aff":"#62666e"; font.bold:window.page===modelData.p; verticalAlignment:Text.AlignVCenter; leftPadding:10; elide:Text.ElideRight }
                        background:Rectangle { color:window.page===modelData.p || (window.page===2&&modelData.p===0) ? "#eaf3ff":parent.hovered?"#f5f5f7":"transparent"; radius:5 }
                        onClicked:window.page=modelData.p
                    }
                }
                Item { Layout.fillHeight:true }
                RowLayout {
                    spacing:7
                    Rectangle { width:7;height:7;radius:3.5;color:window.ready?"#16815d":"#c47b16" }
                    Label { text:window.ready?"agent 已同步":rpc.connected?"正在协商":"agent 离线"; color:"#62666e"; font.pixelSize:12 }
                }
                Label { text:"API "+window.value(rpc.apiVersion)+" · "+window.value(rpc.runtimeProtocol); color:"#9699a0"; font.pixelSize:11 }
            }
        }
        ColumnLayout {
            Layout.fillWidth:true;Layout.fillHeight:true
            Layout.margins:window.width<950?20:32
            spacing:16
            RowLayout {
                Layout.fillWidth:true
                Title { text:window.titles[window.page]; font.pixelSize:24; Layout.fillWidth:true }
                Command { icon.name:"view-refresh"; text:"↻"; ToolTip.text:"重新连接并同步任务"; onClicked:rpc.refresh() }
                Command { text:"新建传输"; icon.name:"list-add"; highlighted:true; visible:window.page!==1; onClicked:window.page=1 }
            }
            Rectangle {
                Layout.fillWidth:true; implicitHeight:notice.implicitHeight+20
                color:rpc.demoAdapter?"#fff3dd":rpc.lastErrorCode.length?"#fff0ee":"#eaf3ff"; radius:5
                Label {
                    id:notice; anchors.left:parent.left;anchors.right:parent.right;anchors.margins:12;anchors.verticalCenter:parent.verticalCenter
                    font.pixelSize:12; wrapMode:Text.Wrap; textFormat:Text.PlainText
                    color:rpc.lastErrorCode.length?"#ae3d35":rpc.demoAdapter?"#925c16":"#356191"
                    text:(rpc.demoAdapter?"开发诊断 / 合成数据 · ":"")+(rpc.stale&&rpc.tasks.length?"快照已过期 · ":"")+
                        rpc.message+(rpc.lastErrorCode.length?" ["+rpc.lastErrorCode+"]":"")
                }
            }
            StackLayout {
                currentIndex:window.page; Layout.fillWidth:true;Layout.fillHeight:true
                // Tasks
                ColumnLayout {
                    spacing:18
                    RowLayout {
                        Layout.fillWidth:true
                        Label { text:"全部任务  "+rpc.tasks.length; color:"#62666e"; font.pixelSize:13 }
                        Item { Layout.fillWidth:true }
                        Field { id:search; Layout.maximumWidth:240; placeholderText:"搜索名称或任务 ID" }
                        ComboBox { id:taskFilter; model:["全部状态","进行中","已完成","需要处理"]; implicitWidth:126 }
                    }
                    Rule {}
                    ScrollView {
                        id:taskScroll; Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth
                        ColumnLayout {
                            width:taskScroll.availableWidth;spacing:0
                            Label { visible:!rpc.tasks.length; text:window.ready?"还没有传输任务":"等待连接 agent"; color:"#74777f"; Layout.topMargin:48; Layout.alignment:Qt.AlignHCenter }
                            Repeater {
                                model:rpc.tasks
                                delegate:ColumnLayout {
                                    required property var modelData
                                    readonly property bool matches: (search.text.length===0 || JSON.stringify(modelData.source).indexOf(search.text)>=0 || modelData.transfer_id.indexOf(search.text)>=0) &&
                                        (taskFilter.currentIndex===0 || taskFilter.currentIndex===1&&!window.terminal(modelData.state) || taskFilter.currentIndex===2&&modelData.state==="completed" || taskFilter.currentIndex===3&&["failed","recoverable","cancelled"].indexOf(modelData.state)>=0)
                                    visible:matches; Layout.fillWidth:true; spacing:10; Layout.topMargin:matches?12:0; Layout.bottomMargin:matches?12:0
                                    RowLayout {
                                        Layout.fillWidth:true;spacing:12
                                        ColumnLayout {
                                            Layout.fillWidth:true;spacing:6
                                            Label { Layout.fillWidth:true; text:modelData.source.path+"  →  "+modelData.destination.path; elide:Text.ElideMiddle; color:"#202124"; font.bold:true }
                                            Label { Layout.fillWidth:true; text:modelData.transfer_id; font.pixelSize:11; color:"#92959d"; elide:Text.ElideMiddle }
                                            Label { Layout.fillWidth:true; text:window.bytes(modelData.metrics.bytes_committed)+" / "+window.bytes(modelData.metrics.bytes_total); font.pixelSize:12;color:"#74777f" }
                                        }
                                        Label { text:window.state(modelData.attempt.state==="cancelling"?"cancelling":modelData.state); color:window.statusColor(modelData.state); font.pixelSize:12 }
                                        Command { text:"详情"; onClicked:window.showTask(modelData) }
                                    }
                                    Rule {}
                                }
                            }
                        }
                    }
                }
                // New transfer
                ScrollView {
                    id:newScroll;clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:newScroll.availableWidth;spacing:18
                        ColumnLayout {
                            Layout.fillWidth:true; enabled:!rpc.busy&&!rpc.creationUncertain; spacing:14
                            RowLayout {
                                Layout.fillWidth:true
                                Label { text:"传输内容";font.bold:true;color:"#202124" }
                                Item { Layout.fillWidth:true }
                                ComboBox { id:direction;objectName:"direction"; model:["上传目录","下载目录"]; onActivated:window.changedDraft() }
                            }
                            Muted { text:"V3 · TLS / token · 目录传输";font.pixelSize:12 }
                            GridLayout {
                                columns:window.width<950?1:2;columnSpacing:20;rowSpacing:12;Layout.fillWidth:true
                                ColumnLayout {
                                    Layout.fillWidth:true;spacing:7
                                    Label { text:direction.currentIndex===0?"本地源目录":"本地目标目录";color:"#62666e";font.pixelSize:12 }
                                    RowLayout {
                                        Field { id:localPath;objectName:"localPath";placeholderText:"本地绝对目录";onTextEdited:window.changedDraft() }
                                        Command { icon.name:"folder-open";text:"…";ToolTip.text:"选择目录";onClicked:window.choosePath("local",true) }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth:true;spacing:7
                                    Label { text:direction.currentIndex===0?"远端目标路径":"远端源路径";color:"#62666e";font.pixelSize:12 }
                                    Field { id:remotePath;objectName:"remotePath";placeholderText:"根目录内的相对路径";onTextEdited:window.changedDraft() }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth:true
                                Label { text:"远端节点";color:"#62666e" }
                                ComboBox { id:profileChoice;objectName:"profileChoice";Layout.fillWidth:true;model:rpc.profiles;textRole:"name";onActivated:window.changedDraft() }
                                Command { text:"设置";onClicked:window.page=3 }
                            }
                            Muted { text:profileChoice.currentIndex>=0&&profileChoice.currentIndex<rpc.profiles.length ? rpc.profiles[profileChoice.currentIndex].host+":"+rpc.profiles[profileChoice.currentIndex].control_port+" · root "+rpc.profiles[profileChoice.currentIndex].default_root_id : "尚未配置节点";font.pixelSize:12 }
                            Rule {}
                            Button { id:advanced; text:checked?"高级策略  −":"高级策略  +";checkable:true;flat:true;Layout.alignment:Qt.AlignLeft }
                            GridLayout {
                                visible:advanced.checked;columns:window.width<950?1:2;columnSpacing:20;rowSpacing:12;Layout.fillWidth:true
                                Repeater {
                                    model:[{k:"compression",n:"压缩",type:"enum"},{k:"channel_count",n:"通道数（auto 或数字）",type:"number"},
                                        {k:"pending_window",n:"数据窗口",type:"number"},{k:"control_pipeline_depth",n:"控制流水线深度",type:"number"},
                                        {k:"reconnect_attempts",n:"重连次数",type:"number"},{k:"transfer_timeout_ms",n:"总超时（ms，0 为默认）",type:"number"},
                                        {k:"chunk_size",n:"块大小（B）",type:"number"},{k:"buffer_size",n:"缓冲区（B）",type:"number"},
                                        {k:"scheduler_mode",n:"调度范围",type:"enum"},{k:"scheduler_policy",n:"调度策略",type:"enum"},
                                        {k:"scheduler_work_item_min_bytes",n:"调度预算下限（B）",type:"number"},{k:"scheduler_work_item_max_bytes",n:"调度预算上限（B）",type:"number"},
                                        {k:"scheduler_default_rtt_ms",n:"默认 RTT（ms）",type:"number"},{k:"scheduler_capacity_gbps",n:"容量预算（Gbps）",type:"number"}]
                                    ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth:true;spacing:6
                                        Label { text:modelData.n;color:"#74777f";font.pixelSize:12 }
                                        ComboBox {
                                            visible:modelData.type==="enum";Layout.fillWidth:true
                                            model:window.enumOptions(modelData.k)
                                            currentIndex:Math.max(0,model.indexOf(window.draftPolicy[modelData.k]))
                                            onActivated:window.setPolicy(modelData.k,currentText)
                                        }
                                        Field {
                                            visible:modelData.type==="number"
                                            text:window.draftPolicy[modelData.k]===undefined?"":String(window.draftPolicy[modelData.k])
                                            onTextEdited: {
                                                var n=Number(text)
                                                window.setPolicy(modelData.k, text.length&&isFinite(n) ? n : text)
                                            }
                                        }
                                    }
                                }
                            }
                            Pair { label:"安全策略"; detail:"控制 TLS：verify_ca · 数据 TLS：required · CRC32C" }
                            Pair { label:"恢复策略"; detail:window.value(window.draftPolicy.resume_policy)+" · fallback: reject" }
                            Command {
                                objectName:"validateButton";text:rpc.busy?"正在预检…":"预检传输";highlighted:true
                                enabled:window.ready&&rpc.supports("transfer.validate")&&localPath.text.length>0&&remotePath.text.length>0&&(rpc.demoAdapter||profileChoice.currentIndex>=0)
                                onClicked:rpc.create(direction.currentIndex===0?"upload":"download",localPath.text,remotePath.text,false,"v3",profileChoice.currentIndex>=0?rpc.profiles[profileChoice.currentIndex].node_id:"",window.draftPolicy)
                            }
                        }
                        ColumnLayout {
                            visible:rpc.validation.valid===true||rpc.creationUncertain;Layout.fillWidth:true;spacing:12
                            Rule {}
                            Title { text:rpc.creationUncertain?"核对上次提交":"确认传输";font.pixelSize:18 }
                            Muted { visible:rpc.creationUncertain;text:"上次提交结果尚未确认，将使用原 operation_id 查询或重放同一请求。";color:"#a06118" }
                            Repeater { model:rpc.validation.warnings || [];Muted { required property string modelData;text:modelData;color:"#a06118" } }
                            Dump { text:window.json(rpc.validation.resolved_options) }
                            Command { objectName:"confirmCreateButton";text:rpc.busy?"提交中…":rpc.creationUncertain?"核对并继续":"创建并开始";highlighted:true;enabled:window.ready&&!rpc.busy&&rpc.supports("transfer.create")&&rpc.supports("transfer.start");onClicked:rpc.confirmCreate() }
                        }
                        Muted {
                            visible:rpc.contractReady&&!rpc.supports("transfer.validate")
                            text:(rpc.contract.methods || []).filter(function(m){return m.method==="transfer.validate"}).map(function(m){return m.reason || "未提供"}).join("")
                            color:"#a06118"
                        }
                        Muted { text:"独立单文件任务：后端尚未提供统一认证接口";font.pixelSize:12 }
                        Button { text:"选择单文件";enabled:false;ToolTip.visible:hovered;ToolTip.text:"当前 agent 仅支持认证目录任务" }
                    }
                }
                // Task detail
                ScrollView {
                    id:detailScroll;objectName:"detailScroll";clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:detailScroll.availableWidth;spacing:16
                        readonly property var task:window.selectedTask
                        readonly property var metrics:task?task.metrics:({})
                        readonly property var evidence:task?task.backend_evidence:({})
                        RowLayout {
                            Layout.fillWidth:true
                            Title { Layout.fillWidth:true;text:window.selectedTask?window.state(window.selectedTask.state):"未选择任务" }
                            Command { text:"刷新详情";enabled:window.ready&&window.selectedTask!==null;onClicked:window.loadSelected() }
                            Command { objectName:"cancelButton";text:"取消";icon.name:"process-stop";enabled:window.ready&&rpc.actionPending.length===0&&window.selectedTask!==null&&!window.terminal(window.selectedTask.state)&&rpc.supports("transfer.cancel");onClicked:rpc.action("transfer.cancel",window.selectedTask) }
                            Command {
                                objectName:"resumeButton"
                                readonly property bool queued:window.selectedTask!==null&&window.selectedTask.state==="queued"
                                text:queued?"开始":"恢复 / 重试";icon.name:queued?"media-playback-start":"document-revert"
                                enabled:window.ready&&rpc.actionPending.length===0&&window.selectedTask!==null&&(queued?rpc.supports("transfer.start"):window.selectedTask.recovery.eligibility==="eligible"&&rpc.supports("transfer.resume"))
                                onClicked:rpc.action(queued?"transfer.start":"transfer.resume",window.selectedTask)
                            }
                        }
                        Muted { visible:rpc.actionPending.length>0;text:rpc.actionPending }
                        Pair { label:"任务 ID / transfer_id";detail:window.selectedTask?window.selectedTask.transfer_id:"未选择" }
                        Pair { label:"attempt";detail:window.selectedTask?window.selectedTask.attempt.attempt_id+" · #"+window.selectedTask.attempt.attempt_number:"未知" }
                        Pair { label:"源 → 目标";detail:window.selectedTask?window.selectedTask.source.path+" → "+window.selectedTask.destination.path:"未知" }
                        Pair { label:"恢复资格";detail:window.selectedTask?window.value(window.selectedTask.recovery.eligibility)+" · "+window.value(window.selectedTask.recovery.reason):"未知" }
                        TabBar {
                            Layout.fillWidth:true;currentIndex:window.detailTab;onCurrentIndexChanged:window.detailTab=currentIndex
                            TabButton { text:"概览" } TabButton { text:"文件 / range" } TabButton { text:"执行记录" } TabButton { text:"原始证据" }
                        }
                        ColumnLayout {
                            visible:window.detailTab===0;Layout.fillWidth:true;spacing:12
                            Pair { label:"已提交 / 总大小";detail:window.bytes(parent.parent.metrics.bytes_committed)+" / "+window.bytes(parent.parent.metrics.bytes_total) }
                            Pair { label:"已传输 / 已核验";detail:window.bytes(parent.parent.metrics.bytes_transferred)+" / "+window.bytes(parent.parent.metrics.bytes_verified) }
                            Pair { label:"速度 / 剩余时间";detail:window.value(parent.parent.metrics.speed_bps," B/s")+" / "+window.value(parent.parent.metrics.eta_seconds," s") }
                            Pair { label:"完成 / 失败 / 跳过";detail:window.value(parent.parent.metrics.files_committed)+" / "+window.value(parent.parent.metrics.files_failed)+" / "+window.value(parent.parent.metrics.files_skipped) }
                            Pair { label:"当前阶段";detail:window.selectedTask?window.value(window.selectedTask.backend_evidence.phase):"未知 / 未提供" }
                            Rule {}
                            Title { text:"阶段计时";font.pixelSize:17 }
                            Repeater {
                                model:["queue_wait","control_prepare","data_connect","first_payload","payload_io","transfer_complete_wait","manifest","checksum","finalize"]
                                Pair { required property string modelData;label:modelData;detail:window.selectedTask&&window.selectedTask.backend_evidence.phase_timing ? window.value(window.selectedTask.backend_evidence.phase_timing[modelData]," s"):"未知 / 未提供" }
                            }
                            Rule {}
                            Title { text:"核验结果";font.pixelSize:17 }
                            Pair { label:"checksum";detail:window.selectedTask?window.json(window.selectedTask.backend_evidence.checksum):"未知" }
                            Pair { label:"manifest";detail:window.selectedTask?window.json(window.selectedTask.backend_evidence.manifest):"未知" }
                            Pair { label:"finalize / commit";detail:window.selectedTask?window.json(window.selectedTask.backend_evidence.finalize)+" / "+window.value(window.selectedTask.commit_confirmed):"未知" }
                            Pair { label:"transfer / integrity";detail:window.selectedTask?window.value(window.selectedTask.transfer_status)+" / "+window.value(window.selectedTask.integrity_status):"未知" }
                            Title { text:"错误详情";font.pixelSize:17 }
                            Dump { text:window.selectedTask&&window.selectedTask.error ? window.json(window.selectedTask.error) : "agent 未返回任务错误" }
                            Title { text:"请求策略";font.pixelSize:17 }
                            Dump { text:window.selectedTask?window.json(window.selectedTask.policy):"未知" }
                            Title { text:"实际策略";font.pixelSize:17 }
                            Dump { text:window.selectedTask?window.json(window.selectedTask.effective_policy):"未知" }
                        }
                        ColumnLayout {
                            visible:window.detailTab===1;Layout.fillWidth:true;spacing:12
                            Muted { text:rpc.detailStatus+" · "+rpc.detailWatermark;font.pixelSize:12 }
                            Label { visible:rpc.files.length===0;text:"尚无已观测文件";color:"#74777f" }
                            Repeater {
                                model:rpc.files
                                ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth:true
                                    RowLayout {
                                        Layout.fillWidth:true
                                        Label { Layout.fillWidth:true;wrapMode:Text.Wrap;text:window.value(modelData.path || modelData.relative_path)+" · "+window.value(modelData.state);font.pixelSize:13 }
                                        Command { text:"range";onClicked:rpc.selectFile(modelData) }
                                    }
                                    Dump { text:window.json(modelData) }
                                }
                            }
                            Command { text:"更多文件";visible:rpc.hasMoreFiles;enabled:window.ready;onClicked:rpc.loadNextFiles() }
                            Title { text:"已选择文件的 range";font.pixelSize:17 }
                            Repeater { model:rpc.ranges;Dump { required property var modelData;text:window.json(modelData) } }
                            Command { text:"更多 range";visible:rpc.hasMoreRanges;enabled:window.ready;onClicked:rpc.loadNextRanges() }
                        }
                        ColumnLayout {
                            visible:window.detailTab===2;Layout.fillWidth:true;spacing:12
                            Repeater { model:rpc.attempts;Dump { required property var modelData;text:window.json(modelData) } }
                            Command { text:"更多执行记录";visible:rpc.hasMoreAttempts;enabled:window.ready;onClicked:rpc.loadNextAttempts() }
                        }
                        ColumnLayout {
                            visible:window.detailTab===3;Layout.fillWidth:true;spacing:12
                            Pair { label:"观测来源";detail:window.value(rpc.evidence.observation) }
                            Dump { text:window.json(rpc.evidence.summary) }
                        }
                    }
                }
                // Profiles
                ScrollView {
                    id:profileScroll;clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:profileScroll.availableWidth;spacing:14
                        RowLayout {
                            Layout.fillWidth:true
                            ComboBox { id:editProfile;Layout.fillWidth:true;model:rpc.profiles;textRole:"name";onActivated:window.loadProfile(rpc.profiles[index]) }
                            Command { text:"载入";enabled:editProfile.currentIndex>=0;onClicked:window.loadProfile(rpc.profiles[editProfile.currentIndex]) }
                            Command { text:"新增";icon.name:"list-add";onClicked:window.resetProfile() }
                            Command { text:"导入节点包";icon.name:"document-open";enabled:window.ready;onClicked:{bundlePicker.active=true;if(bundlePicker.status===Loader.Ready)bundlePicker.item.open()} }
                        }
                        GridLayout {
                            columns:window.width<950?1:2;Layout.fillWidth:true;columnSpacing:20;rowSpacing:10
                            ColumnLayout { Layout.fillWidth:true;Label { text:"节点 ID";color:"#74777f" } Field { id:nodeId;enabled:!window.editingProfile;placeholderText:"node-id" } }
                            ColumnLayout { Layout.fillWidth:true;Label { text:"名称";color:"#74777f" } Field { id:nodeName;placeholderText:"节点名称" } }
                            ColumnLayout { Layout.fillWidth:true;Label { text:"地址";color:"#74777f" } Field { id:host;placeholderText:"主机名或 IP";onTextEdited:rpc.invalidateAuthentication() } }
                            ColumnLayout { Layout.fillWidth:true;Label { text:"控制端口";color:"#74777f" } Field { id:port;text:"2121";validator:IntValidator { bottom:1024;top:65535 } onTextEdited:rpc.invalidateAuthentication() } }
                            ColumnLayout { Layout.fillWidth:true;Label { text:"TLS 服务端名称";color:"#74777f" } Field { id:serverName;placeholderText:"证书中的名称";onTextEdited:rpc.invalidateAuthentication() } }
                            ColumnLayout { Layout.fillWidth:true;Label { text:"远端 root ID";color:"#74777f" } Field { id:rootId;placeholderText:"root-id";onTextEdited:rpc.invalidateAuthentication() } }
                        }
                        Label { text:"CA 证书路径";color:"#74777f" }
                        RowLayout { Field { id:ca;placeholderText:"/path/to/ca.pem";onTextEdited:rpc.invalidateAuthentication() } Command { icon.name:"document-open";text:"…";ToolTip.text:"选择 CA 证书";onClicked:window.choosePath("ca",false) } }
                        Label { text:"Token 文件引用";color:"#74777f" }
                        RowLayout { Field { id:credential;placeholderText:"file:/path/to/token";onTextEdited:rpc.invalidateAuthentication() } Command { icon.name:"document-open";text:"…";ToolTip.text:"选择 token 文件";onClicked:window.choosePath("token",false) } }
                        Pair { label:"认证 / TLS";detail:"token 文件 · CA 与服务端身份校验 · 数据 TLS" }
                        RowLayout {
                            Command { text:"保存节点";highlighted:true;enabled:window.ready&&nodeId.text.length>0&&nodeName.text.length>0&&host.text.length>0&&port.acceptableInput&&serverName.text.length>0&&rootId.text.length>0&&ca.text.startsWith("/")&&credential.text.startsWith("file:/")
                                onClicked:rpc.saveProfile({node_id:nodeId.text,name:nodeName.text,host:host.text,control_port:Number(port.text),server_name:serverName.text,ca_path:ca.text,credential_ref:credential.text,auth_mode:"token",default_root_id:rootId.text},window.editingProfile)
                            }
                            Command { text:"检查已保存的连接";enabled:window.ready&&rpc.supports("node.testConnection")&&editProfile.currentIndex>=0
                                onClicked:rpc.testNode(rpc.profiles[editProfile.currentIndex].node_id,rpc.profiles[editProfile.currentIndex].default_root_id)
                            }
                        }
                        Pair { label:"认证状态";detail:window.value(rpc.authenticationState) }
                        Repeater {
                            model:["dns_control_port","tls_identity","login","capabilities","directory_permission","data_port"]
                            Pair { required property string modelData;label:modelData;detail:rpc.checks[modelData]?window.value(rpc.checks[modelData].state)+" · "+window.value(rpc.checks[modelData].message):"未检查" }
                        }
                    }
                }
                ColumnLayout {
                    Title { text:"远端目录浏览" }
                    Muted { text:rpc.contractReady ? (rpc.contract.methods || []).filter(function(m){return m.method==="directory.list"}).map(function(m){return m.reason}).join("") : "等待 agent 合同" }
                    Button { text:"浏览目录";enabled:false }
                    Item { Layout.fillHeight:true }
                }
                ScrollView {
                    id:history;clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:history.availableWidth;spacing:12
                        Repeater {
                            model:rpc.tasks
                            ColumnLayout {
                                required property var modelData
                                visible:window.terminal(modelData.state);Layout.fillWidth:true
                                RowLayout {
                                    Layout.fillWidth:true
                                    Label { text:window.state(modelData.state);color:window.statusColor(modelData.state) }
                                    Label { text:modelData.source.path;elide:Text.ElideMiddle;Layout.fillWidth:true }
                                    Command { text:modelData.recovery.eligibility==="eligible"?"查看并恢复":"查看";onClicked:window.showTask(modelData) }
                                }
                                Rule {}
                            }
                        }
                        Muted { visible:!rpc.tasks.length;text:"暂无 agent 历史记录" }
                    }
                }
                // Network
                ScrollView {
                    id:networkScroll;clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:networkScroll.availableWidth;spacing:14
                        RowLayout {
                            Layout.fillWidth:true
                            ComboBox { id:networkNode;Layout.fillWidth:true;model:rpc.profiles;textRole:"name" }
                            Command { text:"读取本机绑定";enabled:window.ready&&networkNode.currentIndex>=0
                                onClicked:{window.manualPicks=[];rpc.inspectNetwork(rpc.profiles[networkNode.currentIndex].node_id)}
                            }
                        }
                        Title { text:"当前绑定";font.pixelSize:17 }
                        Dump { text:rpc.networkBinding.binding ? window.json(rpc.networkBinding.binding) : "尚未读取或没有已保存绑定" }
                        RowLayout {
                            Command { text:"验证";enabled:window.ready&&rpc.networkBinding.binding!==undefined&&rpc.networkBinding.binding!==null;onClicked:rpc.verifyNetwork(rpc.networkBinding.node_id,rpc.networkBinding.binding.plan_id) }
                            Command { text:"清除绑定";enabled:window.ready&&rpc.networkBinding.binding!==undefined&&rpc.networkBinding.binding!==null;onClicked:confirmClear.open() }
                        }
                        Dump { visible:rpc.networkVerify.verified!==undefined;text:window.json(rpc.networkVerify) }
                        Rule {}
                        Title { text:"本机接口";font.pixelSize:17 }
                        Repeater {
                            model:rpc.networkInventory.interfaces || []
                            ColumnLayout {
                                required property var modelData
                                property string interfaceId:modelData.interface_id
                                Layout.fillWidth:true;spacing:4
                                Label { Layout.fillWidth:true;text:modelData.name+" · "+modelData.state+(modelData.management?" · 管理接口":"");color:"#62666e";wrapMode:Text.Wrap }
                                Repeater {
                                    model:modelData.addresses || []
                                    CheckBox {
                                        required property string modelData
                                        text:modelData
                                        onToggled: {
                                            var rows=window.manualPicks.filter(function(p){return p.local_address!==modelData})
                                            if(checked)rows.push({interface_id:parent.interfaceId,local_address:modelData})
                                            window.manualPicks=rows
                                        }
                                    }
                                }
                                Rule {}
                            }
                        }
                        RowLayout {
                            Label { text:"链路数";color:"#74777f" }
                            SpinBox { id:links;from:1;to:10;value:1 }
                            Label { text:window.manualPicks.length?"已手选 "+window.manualPicks.length+" 个地址":"自动推荐";color:"#74777f" }
                        }
                        Label { text:"逐 lane 远端 IP（逗号分隔，可留空）";color:"#74777f" }
                        Field { id:peers;placeholderText:"IP 1, IP 2" }
                        Command { text:"生成方案";enabled:window.ready&&rpc.networkInventory.inventory_revision!==undefined
                            onClicked:rpc.planNetwork(rpc.networkInventory.node_id,rpc.networkInventory.inventory_revision,links.value,peers.text.trim().length?peers.text.split(",").map(function(s){return s.trim()}):[],window.manualPicks)
                        }
                        Dump { visible:rpc.networkPlan.plan_id!==undefined;text:window.json(rpc.networkPlan) }
                        Command { text:"确认并应用";enabled:window.ready&&rpc.networkPlan.plan_id!==undefined&&(rpc.networkPlan.bindings||[]).length===rpc.networkPlan.requested_links;onClicked:confirmBinding.open() }
                        Muted { text:"物理路径未验证 · 多个远端 IP 必须属于同一 V3 服务和 root";font.pixelSize:12 }
                    }
                }
                // Diagnostics
                ScrollView {
                    id:diagnostics;clip:true;contentWidth:availableWidth
                    ColumnLayout {
                        width:diagnostics.availableWidth;spacing:12
                        Pair { label:"运行模式";detail:rpc.demoAdapter?"开发诊断 / 合成数据":"真实传输模式" }
                        Pair { label:"Unix socket";detail:rpc.socketPath }
                        Pair { label:"API / runtime";detail:window.value(rpc.apiVersion)+" / "+window.value(rpc.runtimeProtocol) }
                        Pair { label:"错误代码";detail:window.value(rpc.lastErrorCode) }
                        Pair { label:"可重试";detail:window.value(rpc.lastErrorRetryable) }
                        Title { text:"agent 状态";font.pixelSize:17 }
                        Dump { text:window.json(rpc.agentInfo) }
                        Title { text:"能力合同";font.pixelSize:17 }
                        Repeater {
                            model:rpc.contract.methods || []
                            Pair { required property var modelData;label:modelData.method;detail:modelData.support+(modelData.reason?" · "+modelData.reason:"") }
                        }
                        Title { text:"当前边界";font.pixelSize:17 }
                        Repeater { model:rpc.contract.limitations || [];Muted { required property string modelData;text:modelData } }
                        Command { text:"远端目录能力";onClicked:window.page=4 }
                    }
                }
            }
        }
    }
}
