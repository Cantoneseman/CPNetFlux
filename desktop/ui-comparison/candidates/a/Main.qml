import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1180; height: 780; minimumWidth: 760; minimumHeight: 560
    visible: true
    title: "CPNetFlux · 真实传输客户端"
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
    function value(v, unit) { return known(v) ? String(v) + (unit || "") : "未提供" }
    function seconds(v) { return known(v) ? Number(v).toFixed(3) + " 秒" : "未提供" }
    function bytes(v) { return known(v) ? (Number(v)/1048576).toFixed(2)+" MiB" : "未提供" }
    function rate(v) { return known(v) ? (Number(v)/1048576).toFixed(2)+" MiB/s" : "等待首个进度事件" }
    function eta(v) { return known(v) ? (Number(v) < 60 ? Number(v).toFixed(1)+" 秒" : (Number(v)/60).toFixed(1)+" 分钟") : "未提供" }
    function engineLabel(t) {
        if (!t) return "等待引擎协商"
        if (t.adapter === "demo") return "演示流程（不传输真实文件）"
        if (t.actual_range_policy === "range_stripe")
            return "CPNetFlux V3（动态调度 + 大文件分段）"
        if (t.actual_scheduler === "dynamic_ring")
            return "CPNetFlux V3（动态文件调度）"
        if (t.actual_mode === "v2") return "CPNetFlux V2 兼容传输"
        if (t.actual_mode === "v1") return "兼容回退路径"
        return "等待后端确认"
    }
    function engineDetail(t) {
        if (!t) return "尚未创建任务"
        if (t.adapter === "demo") return "演示数据只用于展示流程，不代表真实传输"
        if (t.actual_range_policy === "range_stripe")
            return "V3 动态队列和大文件分段均已由节点确认"
        if (t.actual_scheduler === "dynamic_ring")
            return "V3 动态文件队列已启用；本次任务按文件调度"
        if (t.actual_mode === "v2") return "V2 兼容数据面已启用；当前节点未返回 V3 调度证据"
        if (t.actual_mode === "v1") return "兼容回退路径；请检查节点能力和安全策略"
        return "等待后端返回实际协商结果"
    }
    function metric(t, key) { return t && t.metrics ? t.metrics[key] : null }
    function stateName(s) {
        return ({queued:"已创建，等待启动", scanning:"正在扫描目录", connecting:"正在连接远端", negotiating:"正在协商传输", transferring:"正在传输", committing:"正在校验并提交", completed:"传输已完成", failed:"传输失败", cancelled:"已取消", recoverable:"可恢复", paused:"已暂停"})[s] || "等待状态"
    }
    function stateSummary(t) {
        if (!t) return "请选择一项任务。"
        return ({
            queued:"任务已在 agent 中建立，等待启动。",
            scanning:"正在扫描目录，文件数量可能仍在变化；请等待扫描和远端准备完成。",
            connecting:"正在连接远端并等待控制通道准备。",
            negotiating:"正在与远端协商传输能力和数据通道。",
            transferring:"正在传输已发现的文件；速度和进度会随实时事件更新。",
            committing:"正在校验 manifest、checksum 并提交传输结果。",
            completed:"agent 已返回完成状态，传输结果可以核验。",
            failed:"agent 返回失败状态，请查看错误详情并按恢复资格重试。",
            cancelled:"任务已取消；是否可恢复由 agent 返回的恢复资格决定。",
            recoverable:"agent 已保留恢复点，可以尝试恢复或查看失败原因。",
            paused:"任务已暂停；继续操作取决于 agent 的实际能力。"
        })[t.state] || "等待后端确认当前阶段。"
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
        if (t.state === "scanning") return "下一步：等待目录扫描完成；完成后显示文件总数。"
        if (t.state === "connecting" || t.state === "negotiating") return "下一步：等待远端准备和数据连接。"
        if (t.state === "transferring") return "下一步：等待传输完成或错误；实时进度会持续更新。"
        if (t.state === "committing") return "下一步：等待 manifest、checksum 和 finalize 结果。"
        if (t.state === "completed") return "下一步：查看完整性结果或从列表隐藏。"
        if (t.state === "recoverable") return "下一步：按恢复资格继续任务或查看失败原因。"
        if (t.state === "cancelled") return "下一步：如有恢复资格，可继续任务。"
        return "下一步：查看错误详情和恢复资格。"
    }
    function integrityText(t, key) {
        var e = t && t.backend_evidence ? t.backend_evidence[key] : null
        if (!known(e)) return "未提供核验结果"
        if (typeof e === "object" && known(e.status)) return ({pass:"通过",fail:"未通过",observed:"已记录",unknown:"未提供"})[e.status] || String(e.status)
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

    QtObject {
        id: theme
        readonly property color canvas: "#f4f4ed"
        readonly property color paper: "#ffffff"
        readonly property color ink: "#18251f"
        readonly property color muted: "#617066"
        readonly property color line: "#dce2d8"
        readonly property color accent: "#d7f56a"
        readonly property color success: "#23634b"
        readonly property color danger: "#9c3529"
        readonly property int gap: 16
    }
    property bool canOperate: rpc.connected && rpc.apiCompatible && !rpc.stale
    property int taskFilter: 0
    property string taskQuery: ""
    function visibleTasks() {
        return rpc.tasks.filter(function(t) {
            var stateMatches = taskFilter === 0 || (taskFilter === 1 && !terminal(t.state)) || (taskFilter === 2 && terminal(t.state))
            var path = t.source.path + " " + t.destination.path + " " + t.transfer_id
            return stateMatches && path.toLowerCase().indexOf(taskQuery.toLowerCase()) >= 0
        })
    }
    function stateCount(done) { return rpc.tasks.filter(function(t){ return done ? t.state === "completed" : !terminal(t.state) }).length }
    function recoveryText(t) {
        if (!t || !t.recovery || t.recovery.eligibility === "unknown") return "恢复资格尚未确认"
        return t.recovery.eligibility === "eligible" ? "有可用恢复点" : "当前任务没有可用恢复点"
    }
    function phaseTitle(key) { return ({scan:"扫描目录",prepare:"远端准备",transfer:"传输数据",verify:"校验结果",done:"提交完成"})[key] }
    component BodyText: Label { color: theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    component Note: Label { color: theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; lineHeight: 1.25 }
    component Title: Label { color: theme.ink; font.pixelSize: 23; font.bold: true; Layout.fillWidth: true }
    component Surface: Rectangle {
        default property alias contents: surfaceBody.data
        color: theme.paper; border.color: theme.line; radius: 10
        implicitHeight: surfaceBody.implicitHeight + 40
        ColumnLayout { id: surfaceBody; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 20; spacing: 14 }
    }
    component Action: Button {
        id: button
        property bool primary: false
        implicitHeight: 42; horizontalPadding: 16
        contentItem: Text { text: button.text; font.pixelSize: button.font.pixelSize; font.family: button.font.family; font.bold: button.primary; color: button.enabled ? theme.ink : "#8d978d"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 6; color: !button.enabled ? "#e9ede5" : button.primary ? (button.down ? "#c1dc5e" : theme.accent) : button.hovered ? "#eef2e8" : theme.paper; border.color: button.activeFocus ? theme.ink : button.primary ? theme.accent : theme.line; border.width: button.activeFocus ? 2 : 1; Behavior on color { ColorAnimation { duration: 110 } } }
    }
    component Field: TextField {
        implicitHeight: 44; color: theme.ink; selectByMouse: true; padding: 12
        placeholderTextColor: "#778177"
        background: Rectangle { color: parent.enabled ? "#fcfdf9" : "#edf0e9"; radius: 6; border.color: parent.activeFocus ? theme.success : theme.line; border.width: parent.activeFocus ? 2 : 1 }
    }
    component Pill: Rectangle {
        property string text: ""
        property color tone: theme.success
        color: "#f0f4eb"; radius: 4; implicitHeight: 27; implicitWidth: pillText.implicitWidth + 18
        Label { id: pillText; anchors.centerIn: parent; text: parent.text; color: parent.tone; font.pixelSize: 12; font.bold: true }
    }
    component MeasuredProgress: ProgressBar {
        id: measured
        property var done: null
        property var total: null
        visible: window.progressKnown(done, total)
        value: window.fraction(done, total)
        implicitHeight: 5
        background: Rectangle { color: "#e6eadf"; radius: 2 }
        contentItem: Item { Rectangle { width: measured.visualPosition * parent.width; height: parent.height; color: theme.success; radius: 2 } }
    }

    color: theme.canvas
    font.family: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : "Noto Sans CJK SC"
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: window.width < 1040 ? 176 : 216; color: theme.ink
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 10
                RowLayout { spacing: 9
                    Rectangle { width: 27; height: 27; color: theme.accent; radius: 3; Label { anchors.centerIn: parent; text: "↗"; color: theme.ink; font.pixelSize: 23; font.bold: true } }
                    Label { text: "CPNetFlux"; color: "#f4f7ef"; font.pixelSize: 23; font.bold: true }
                }
                Label { text: "TRANSFER WORKSPACE"; color: "#a4b1a6"; font.family: "Consolas"; font.pixelSize: 10; font.letterSpacing: 1.1 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#35433a"; Layout.topMargin: 20; Layout.bottomMargin: 10 }
                Repeater {
                    model: [{p:0,label:"传输工作台",symbol:"↗"},{p:1,label:"新建传输",symbol:"＋"},{p:2,label:"任务详情",symbol:"≡"},{p:3,label:"节点与认证",symbol:"◎"},{p:5,label:"历史与恢复",symbol:"↺"},{p:4,label:"远端目录",symbol:"▤"},{p:6,label:"诊断与能力",symbol:"⋯"}]
                    Button {
                        id: nav; required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 44; text: modelData.label
                        onClicked: window.page = modelData.p
                        background: Rectangle { color: window.page === nav.modelData.p ? theme.accent : nav.hovered ? "#2b3a2f" : "transparent"; radius: 6; border.color: nav.activeFocus ? "#eff5e6" : "transparent" }
                        contentItem: RowLayout { spacing: 11
                            Label { text: nav.modelData.symbol; color: window.page === nav.modelData.p ? theme.ink : "#b0bfae"; font.pixelSize: 19 }
                            Label { text: nav.text; color: window.page === nav.modelData.p ? theme.ink : "#e0e7dc"; font.bold: window.page === nav.modelData.p; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; implicitHeight: 85; color: "#26362c"; radius: 6
                    ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 5
                        Label { text: rpc.demoAdapter ? "设计 / 开发诊断" : "真实传输模式"; color: theme.accent; font.bold: true }
                        Label { text: window.canOperate ? "本地 agent 就绪" : "未连接真实 agent"; color: "#b6c4b5"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
                Item { Layout.preferredHeight: 26 }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle { Layout.fillWidth: true; implicitHeight: 65; color: theme.paper; border.color: theme.line
                RowLayout { anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 28
                    Label { text: "工作区  /  " + window.pageTitles[window.page]; color: theme.muted; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    Pill { text: window.canOperate ? "● agent 已就绪" : rpc.connected ? "● API 协商未完成" : "● agent 未连接"; tone: window.canOperate ? theme.success : "#94602a" }
                    Action { text: "刷新连接"; onClicked: rpc.refresh() }
                }
            }
            ScrollView {
                id: workspace; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                ColumnLayout {
                    width: workspace.availableWidth; spacing: 20
                    Item { Layout.preferredHeight: 4 }
                    ColumnLayout { Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; spacing: 20
                        RowLayout { Layout.fillWidth: true
                            ColumnLayout { Layout.fillWidth: true; spacing: 5
                                Label { text: window.page === 0 ? "传输工作台" : window.page === 3 ? "节点与认证" : window.pageTitles[window.page]; color: theme.ink; font.pixelSize: 32; font.bold: true }
                                Note { text: window.page === 0 ? "从发起到核验，每一步都有依据。" : window.page === 1 ? "明确两端路径，安全配置交给 agent。" : window.page === 3 ? "先连接节点，再开始传输。" : "状态和指标来自后端，缺少的证据明确留空。" }
                            }
                            Action { visible: window.page === 0; text: "＋ 新建传输"; primary: true; enabled: window.canOperate && !rpc.createInFlight; onClicked: window.page = 1 }
                        }
                        Surface { Layout.fillWidth: true; visible: rpc.demoAdapter || !window.canOperate
                            RowLayout { Layout.fillWidth: true
                                Label { text: rpc.demoAdapter ? "预览环境" : "连接需要处理"; font.bold: true; color: "#94602a" }
                                Note { text: rpc.demoAdapter ? "开发诊断 / 合成数据 · 未连接真实 agent；操作不会创建真实传输。" : rpc.stale ? "连接中断，保留最后快照。刷新连接后再继续操作。" : "请启动本地 cpnetflux-agent，检查 socket、权限和 API 版本。" }
                            }
                        }
                        Note { visible: rpc.message.length > 0; text: rpc.message }
                        BodyText { visible: rpc.createError.length > 0 || rpc.actionError.length > 0; text: rpc.createError || rpc.actionError; color: theme.danger }

                        // Overview uses only list counts and metrics supplied by rpc.
                        ColumnLayout { visible: window.page === 0; Layout.fillWidth: true; spacing: 16
                            RowLayout { Layout.fillWidth: true; spacing: 12
                                Repeater { model: [{title:"任务总数",count:rpc.tasks.length},{title:"进行中 / 等待",count:window.stateCount(false)},{title:"已完成",count:window.stateCount(true)}]
                                    Surface { required property var modelData; Layout.fillWidth: true
                                        Note { text: modelData.title }
                                        Label { text: modelData.count; color: theme.ink; font.pixelSize: 34; font.family: "Consolas"; font.bold: true }
                                    }
                                }
                            }
                            RowLayout { Layout.fillWidth: true
                                Field { Layout.fillWidth: true; placeholderText: "搜索文件路径或任务 ID"; onTextChanged: window.taskQuery = text }
                                ComboBox { model: ["全部任务", "进行中", "已结束"]; implicitHeight: 44; onActivated: window.taskFilter = currentIndex }
                            }
                            Surface { visible: window.visibleTasks().length === 0; Layout.fillWidth: true
                                Title { text: rpc.tasks.length === 0 ? "从第一项传输开始" : "没有匹配的任务" }
                                Note { text: rpc.tasks.length === 0 ? "连接 agent、添加远端节点，然后选择文件或目录。" : "试试其他路径或任务筛选条件。" }
                                Action { text: "新建传输"; primary: true; enabled: window.canOperate; onClicked: window.page = 1 }
                            }
                            Repeater { model: window.visibleTasks()
                                Surface { required property var modelData; Layout.fillWidth: true
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: modelData.direction === "upload" ? "↗" : "↙"; font.pixelSize: 25; color: theme.success }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 5
                                            BodyText { text: modelData.source.path.split("/").pop() || modelData.source.path; font.bold: true; font.pixelSize: 17 }
                                            Note { text: modelData.source.path + " → " + modelData.destination.path; maximumLineCount: 1; elide: Text.ElideMiddle }
                                        }
                                        Pill { text: window.stateName(modelData.state); tone: modelData.state === "failed" ? theme.danger : theme.success }
                                        Action { text: "详情 ↗"; onClicked: window.selectTask(modelData) }
                                    }
                                    MeasuredProgress { Layout.fillWidth: true; done: modelData.metrics.bytes_committed; total: modelData.metrics.bytes_total }
                                    RowLayout { Layout.fillWidth: true
                                        Note { text: window.bytes(modelData.metrics.bytes_committed) + " / " + window.bytes(modelData.metrics.bytes_total) + " · 文件 " + window.value(modelData.metrics.files_committed) + " / " + window.value(modelData.metrics.files_total) }
                                        Label { text: window.known(modelData.metrics.speed_bps) ? window.rate(modelData.metrics.speed_bps) : "速率未提供"; color: theme.muted; font.family: "Consolas" }
                                    }
                                }
                            }
                        }

                        ColumnLayout { visible: window.page === 1; Layout.fillWidth: true; spacing: 16
                            Surface { Layout.fillWidth: true
                                Pill { text: "01 / 选择两端" }
                                GridLayout { columns: window.width < 1050 ? 1 : 2; Layout.fillWidth: true; columnSpacing: 24; rowSpacing: 16
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "传输方向"; font.bold: true } ComboBox { id: direction; objectName: "direction"; Layout.fillWidth: true; implicitHeight: 44; model: ["本机 → 远端（上传）", "远端 → 本机（下载）"] } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "远端节点"; font.bold: true } ComboBox { id: profileChoice; Layout.fillWidth: true; implicitHeight: 44; model: rpc.profiles.length ? rpc.profiles.map(function(p){return p.name}) : ["请先添加节点"]; onActivated: { window.selectedProfileId = rpc.profiles[currentIndex].node_id; rpc.requestCapabilities(window.selectedProfileId) } } }
                                }
                                Note { text: "没有远端节点？前往“节点与认证”保存连接配置。" }
                                Action { text: "配置节点 ↗"; onClicked: window.page = 3 }
                            }
                            Surface { Layout.fillWidth: true
                                Pill { text: "02 / 指定路径" }
                                BodyText { text: "本地文件或目录"; font.bold: true }
                                RowLayout { Layout.fillWidth: true
                                    Field { id: localPath; objectName: "localPath"; Layout.fillWidth: true; placeholderText: "/home/you/data · agent 将校验路径和权限" }
                                    Action { text: "选文件"; enabled: !rpc.demoAdapter; onClicked: window.pickPath(false) }
                                    Action { text: "选目录"; enabled: !rpc.demoAdapter; onClicked: window.pickPath(true) }
                                }
                                BodyText { text: "远端相对目录"; font.bold: true }
                                Field { id: remotePath; Layout.fillWidth: true; placeholderText: "incoming/dataset" }
                                Note { text: "路径相对于节点授权根目录。目录浏览尚未开放；不会自动推断或覆盖远端内容。" }
                            }
                            Surface { Layout.fillWidth: true
                                Pill { text: "03 / 安全与提交" }
                                BodyText { text: "TLS 与身份校验默认必需"; font.bold: true }
                                Note { text: "使用节点保存的 CA 路径与凭据引用。UI 不接收私钥或 token 内容；agent 负责检查路径、权限和实际能力。" }
                                BodyText { text: rpc.createStatus; visible: text.length > 0 }
                                Action { objectName: "createButton"; text: rpc.createInFlight ? "正在创建…" : "创建传输任务"; primary: true; enabled: window.canOperate && !rpc.createInFlight && rpc.profiles.length > 0 && localPath.text.length > 0 && remotePath.text.length > 0; onClicked: rpc.create(direction.currentIndex === 0 ? "upload" : "download", localPath.text, remotePath.text, false, "v2", window.selectedProfileId) }
                                Note { text: window.canOperate ? "提交后可在任务详情跟踪真实结果。" : "先连接真实 agent，才能提交任务。" }
                            }
                        }

                        ColumnLayout { visible: window.page === 2; Layout.fillWidth: true; spacing: 16
                            Surface { visible: !window.selectedTask; Layout.fillWidth: true
                                Title { text: "还没有选中任务" }
                                Note { text: "从传输工作台选择任务查看阶段和完整性结果。" }
                                Action { text: "返回工作台"; onClicked: window.page = 0 }
                            }
                            Surface { visible: !!window.selectedTask; Layout.fillWidth: true
                                RowLayout { Layout.fillWidth: true
                                    ColumnLayout { Layout.fillWidth: true; spacing: 7
                                        Pill { text: window.selectedTask ? window.stateName(window.selectedTask.state) : "" }
                                        Title { text: window.selectedTask ? window.selectedTask.source.path.split("/").pop() : ""; font.pixelSize: 27 }
                                        Note { text: window.selectedTask ? window.selectedTask.source.path + " → " + window.selectedTask.destination.path : "" }
                                    }
                                    Action { text: "取消任务"; enabled: window.canOperate && !rpc.actionInFlight && window.selectedTask && !window.terminal(window.selectedTask.state); onClicked: rpc.action("transfer.cancel", window.selectedTask) }
                                }
                                BodyText { text: rpc.stale ? "连接中断，当前显示最后快照；重连后刷新。" : window.stateSummary(window.selectedTask) }
                                BodyText { visible: window.selectedTask && rpc.actionTransferId === window.selectedTask.transfer_id && rpc.actionStatus.length > 0; text: rpc.actionStatus; color: "#94602a" }
                                Note { text: window.nextStep(window.selectedTask) }
                                Note { visible: window.selectedTask && window.selectedTask.state === "scanning"; text: "已发现文件数：" + (window.known(window.metric(window.selectedTask,"files_discovered")) ? window.value(window.metric(window.selectedTask,"files_discovered")) : "agent 未提供") + " · 等待远端准备" }
                                GridLayout { columns: window.width < 1050 ? 1 : 5; Layout.fillWidth: true; columnSpacing: 8; rowSpacing: 8
                                    Repeater { model: ["scan","prepare","transfer","verify","done"]
                                        Rectangle { required property string modelData; property string stage: window.phaseStatus(window.selectedTask,modelData); Layout.fillWidth: true; implicitHeight: 60; color: stage === "current" ? theme.accent : stage === "done" ? "#edf4e8" : "#f5f6f0"; radius: 5
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 4
                                                Label { text: window.phaseTitle(modelData); color: theme.ink; font.bold: true }
                                                Label { text: stage === "current" ? "进行中" : stage === "done" ? "已完成" : "待后端确认"; font.pixelSize: 11; color: theme.muted }
                                            }
                                        }
                                    }
                                }
                                RowLayout { Layout.fillWidth: true
                                    ColumnLayout { Layout.fillWidth: true
                                        Note { text: "已提交 / 总大小" }
                                        Label { text: window.bytes(window.metric(window.selectedTask,"bytes_committed")) + " / " + window.bytes(window.metric(window.selectedTask,"bytes_total")); font.family: "Consolas"; font.pixelSize: 22; color: theme.ink }
                                    }
                                    ColumnLayout { Layout.fillWidth: true
                                        Note { text: "已提交文件" }
                                        Label { text: window.value(window.metric(window.selectedTask,"files_committed")) + " / " + window.value(window.metric(window.selectedTask,"files_total")); font.family: "Consolas"; font.pixelSize: 22; color: theme.ink }
                                    }
                                }
                                MeasuredProgress { Layout.fillWidth: true; done: window.metric(window.selectedTask,"bytes_committed"); total: window.metric(window.selectedTask,"bytes_total") }
                                Note { text: "实时速度：" + (window.known(window.metric(window.selectedTask,"speed_bps")) ? window.rate(window.metric(window.selectedTask,"speed_bps")) : "未提供") + " · 剩余时间：" + (window.known(window.metric(window.selectedTask,"eta_seconds")) ? window.eta(window.metric(window.selectedTask,"eta_seconds")) : "未提供") }
                                BodyText { visible: window.selectedTask && !!window.selectedTask.error; text: window.selectedTask && window.selectedTask.error ? "错误：" + window.selectedTask.error.message : ""; color: theme.danger }
                            }
                            Surface { visible: !!window.selectedTask; Layout.fillWidth: true
                                Title { text: "完整性核验" }
                                BodyText { text: "manifest：" + window.integrityText(window.selectedTask,"manifest") + " · checksum：" + window.integrityText(window.selectedTask,"checksum") + " · finalize：" + window.integrityText(window.selectedTask,"finalize") }
                                Note { text: window.recoveryText(window.selectedTask) }
                                RowLayout { Layout.fillWidth: true
                                    Action { text: rpc.actionInFlight ? "请求中…" : "开始任务"; primary: true; enabled: window.canOperate && !rpc.actionInFlight && window.selectedTask && window.selectedTask.state === "queued"; onClicked: rpc.action("transfer.start",window.selectedTask) }
                                    Action { text: "恢复任务"; enabled: window.canOperate && !rpc.actionInFlight && !!rpc.capabilities.resume_supported && window.selectedTask && window.selectedTask.recovery.eligibility === "eligible" && window.terminal(window.selectedTask.state); onClicked: rpc.action("transfer.resume",window.selectedTask) }
                                    Action { text: "从列表隐藏"; onClicked: { removeDialog.task=window.selectedTask; removeDialog.open() } }
                                }
                                Note { text: "暂停暂不支持；恢复只在 agent 返回真实恢复资格和能力时启用。" }
                            }
                            Surface { id: technical; property bool expanded: false; visible: !!window.selectedTask; Layout.fillWidth: true
                                Action { text: technical.expanded ? "收起技术详情 −" : "展开技术详情 ＋"; onClicked: technical.expanded = !technical.expanded }
                                ColumnLayout { visible: technical.expanded; Layout.fillWidth: true; spacing: 10
                                    BodyText { text: window.selectedTask ? "任务 ID：" + window.selectedTask.transfer_id : ""; font.family: "Consolas" }
                                    Note { text: window.selectedTask ? "引擎：" + window.engineLabel(window.selectedTask) + "\n" + window.engineDetail(window.selectedTask) : "" }
                                    Note { text: window.selectedTask ? "实际调度：" + window.value(window.selectedTask.actual_scheduler) + " · range 策略：" + window.value(window.selectedTask.actual_range_policy) + "\n回退原因：" + (window.selectedTask.fallback_reason ? window.selectedTask.fallback_reason.message : "后端未返回回退记录") : "" }
                                    Note { text: window.selectedTask && window.selectedTask.backend_evidence && window.selectedTask.backend_evidence.phase_timing ? "阶段耗时：control_prepare " + window.seconds(window.selectedTask.backend_evidence.phase_timing.control_prepare) + " · data_connect " + window.seconds(window.selectedTask.backend_evidence.phase_timing.data_connect) + " · first_payload " + window.seconds(window.selectedTask.backend_evidence.phase_timing.first_payload) + " · payload_io " + window.seconds(window.selectedTask.backend_evidence.phase_timing.payload_io) + " · transfer_complete_wait " + window.seconds(window.selectedTask.backend_evidence.phase_timing.transfer_complete_wait) : "阶段计时：agent 未提供" }
                                    Note { text: "分页证据：此项由 agent 提供后可用。当前仅显示已加载条目：attempt " + rpc.attempts.length + " / file " + rpc.files.length + " / range " + rpc.ranges.length }
                                    Note { text: "详情：" + rpc.detailStatus + " · " + rpc.detailWatermark }
                                    Action { text: "刷新详情证据"; enabled: window.canOperate && !!window.selectedTask; onClicked: rpc.loadDetails(window.selectedTask.transfer_id,window.selectedTask.attempt.attempt_id) }
                                }
                            }
                        }

                        ColumnLayout { visible: window.page === 3; Layout.fillWidth: true; spacing: 16
                            Surface { Layout.fillWidth: true
                                Title { text: "连接一个受控节点" }
                                Note { text: "远端需要运行 CPNetFlux 服务端。这里只保存配置和凭据引用，安装两个 UI 不等于可以互传。" }
                                ComboBox { Layout.fillWidth: true; model: rpc.profiles; textRole: "name"; onActivated: window.loadProfile(rpc.profiles[currentIndex]) }
                                Action { text: "＋ 添加新节点"; onClicked: { nodeId.clear(); nodeName.clear(); host.clear(); port.text="22310"; rootPath.text="home"; ca.clear(); serverName.clear(); credentialRef.clear(); auth.currentIndex=0 } }
                                GridLayout { columns: window.width < 1050 ? 1 : 2; Layout.fillWidth: true; columnSpacing: 24; rowSpacing: 16
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "节点名称"; font.bold: true } Field { id: nodeName; Layout.fillWidth: true; placeholderText: "例如：研究节点" } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "节点 ID"; font.bold: true } Field { id: nodeId; Layout.fillWidth: true; placeholderText: "research-node" } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "服务端地址"; font.bold: true } Field { id: host; Layout.fillWidth: true; placeholderText: "node.example.invalid" } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "控制端口"; font.bold: true } Field { id: port; text: "22310"; validator: IntValidator { bottom: 1; top: 65535 } Layout.fillWidth: true } }
                                }
                            }
                            Surface { Layout.fillWidth: true
                                Title { text: "TLS 与身份" }
                                Pill { text: "默认要求 TLS / 认证" }
                                GridLayout { columns: window.width < 1050 ? 1 : 2; Layout.fillWidth: true; columnSpacing: 24; rowSpacing: 16
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "TLS 服务端名称"; font.bold: true } Field { id: serverName; Layout.fillWidth: true; placeholderText: "与证书名称一致" } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "授权根目录 ID"; font.bold: true } Field { id: rootPath; text: "home"; Layout.fillWidth: true } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "CA 文件路径"; font.bold: true } Field { id: ca; Layout.fillWidth: true; placeholderText: "/home/you/config/ca.pem" } }
                                    ColumnLayout { Layout.fillWidth: true; BodyText { text: "凭据引用"; font.bold: true } Field { id: credentialRef; Layout.fillWidth: true; placeholderText: "填写受保护文件或密钥环引用，不填写 token 值" } }
                                }
                                ComboBox { id: auth; Layout.fillWidth: true; model: ["短期 token 引用", "管理员凭据引用"] }
                                Note { text: "UI 不保存私钥、密码或明文 token。引用的权限与认证结果由 agent 校验。" }
                                Action { text: "保存节点配置"; primary: true; enabled: window.canOperate && port.acceptableInput && nodeId.text.length > 0 && nodeName.text.length > 0 && host.text.length > 0 && ca.text.length > 0 && credentialRef.text.length > 0; onClicked: {
                                    var update=rpc.profiles.some(function(p){ return p.node_id === nodeId.text })
                                    rpc.saveProfile({node_id:nodeId.text,name:nodeName.text,host:host.text,control_port:parseInt(port.text),server_name:serverName.text,ca_path:ca.text,auth_mode:["token","admin_credential"][auth.currentIndex],credential_ref:credentialRef.text,default_root_id:rootPath.text},update)
                                } }
                            }
                            Surface { Layout.fillWidth: true
                                Title { text: "连接检查" }
                                Note { text: "保存配置后依次检查网络、TLS、登录、能力和目录权限。缺少结果不会显示成功。" }
                                Repeater { model: [{key:"dns_control_port",name:"网络与控制端口"},{key:"tls_identity",name:"TLS 身份"},{key:"login",name:"身份认证"},{key:"capabilities",name:"传输能力"},{key:"directory_permission",name:"目录权限"}]
                                    BodyText { required property var modelData; text: modelData.name + "：" + (rpc.checks[modelData.key] ? rpc.checks[modelData.key].state + " · " + rpc.checks[modelData.key].message : "尚未检查") }
                                }
                                Action { text: "运行分步检查"; enabled: window.canOperate && nodeId.text.length > 0; onClicked: rpc.testNode(nodeId.text,rootPath.text) }
                            }
                        }
                        Surface { visible: window.page === 4; Layout.fillWidth: true
                            Title { text: "远端目录浏览暂不可用" }
                            Note { text: "当前 agent 未提供此能力。新建传输时可填写远端相对目录；不会在未完成授权时浏览或修改远端文件。" }
                            Action { text: "浏览目录 · 尚未提供"; enabled: false }
                        }
                        Surface { visible: window.page === 5; Layout.fillWidth: true
                            Title { text: "历史与恢复" }
                            Note { text: "历史由 agent 保存。重新打开 UI 后从 agent 恢复快照，恢复操作取决于真实 checkpoint。" }
                            Repeater { model: rpc.tasks.filter(function(t){ return window.terminal(t.state) })
                                RowLayout { required property var modelData; Layout.fillWidth: true
                                    BodyText { text: window.stateName(modelData.state) + " · " + modelData.source.path; elide: Text.ElideMiddle; maximumLineCount: 1 }
                                    Action { text: "查看结果"; onClicked: window.selectTask(modelData) }
                                }
                            }
                        }
                        Surface { visible: window.page === 6; Layout.fillWidth: true
                            Title { text: "连接与能力诊断" }
                            BodyText { text: window.canOperate ? "连接、API 协商均已就绪" : "未连接或 API 协商未完成" }
                            Note { text: "API 版本：" + (rpc.agentInfo.api_version || "未提供") + "\n暂停：当前不支持\n远端目录：当前不支持\n恢复：仅在任务和节点均返回支持证据时启用" }
                            Action { text: "刷新 agent 信息"; onClicked: rpc.refresh() }
                        }
                        Item { Layout.preferredHeight: 44 }
                    }
                }
            }
        }
    }
    Loader { id: pathPicker; active: false; source: "qrc:/ui/PathPicker.qml"; onLoaded: { item.directoryMode=window.pickerDirectory; item.selected.connect(function(path){ localPath.text=path; pathPicker.active=false }); item.open() } }
}
