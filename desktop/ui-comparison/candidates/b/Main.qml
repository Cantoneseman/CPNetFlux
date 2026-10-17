import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1440
    height: 900
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: "CPNetFlux · Mission Control"
    color: theme.canvas
    font.family: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : "Noto Sans CJK SC"
    font.pixelSize: 14

    property int page: initialPage
    property int createStep: 0
    property int presetDirection: 0
    property string selectedId: ""
    property string selectedProfileId: ""
    property bool pickerDirectory: false
    property string pickerTarget: "source"
    property var pendingRemoveTask: null
    property var pageTitles: ["任务控制台", "新建传输", "任务指挥", "节点与身份", "文件与分段", "历史任务", "能力与诊断"]
    readonly property bool canOperate: rpc.connected && rpc.apiCompatible && !rpc.stale

    function known(v) { return v !== null && v !== undefined && v !== "" }
    function value(v, unit) { return known(v) ? String(v) + (unit || "") : "未提供" }
    function bytes(v) { return known(v) ? (Number(v) / 1048576).toFixed(2) + " MiB" : "未提供" }
    function rate(v) { return known(v) ? (Number(v) / 1048576).toFixed(2) + " MiB/s" : "等待 agent 观测" }
    function seconds(v) { return known(v) ? Number(v).toFixed(3) + " s" : "未提供" }
    function progressKnown(done, total) { return known(done) && known(total) && Number(total) > 0 }
    function fraction(done, total) { return progressKnown(done, total) ? Number(done) / Number(total) : 0 }
    function terminal(state) { return ["completed", "failed", "cancelled", "recoverable"].indexOf(state) >= 0 }
    function statusLabel(state) {
        return ({queued:"排队中", scanning:"扫描中", connecting:"连接中", negotiating:"协商中",
                 transferring:"传输中", committing:"校验 / finalize", completed:"已完成",
                 failed:"失败", cancelled:"已取消", recoverable:"可恢复", paused:"已暂停"})[state] || "状态未知"
    }
    function statusTone(state) {
        if (state === "completed") return theme.good
        if (state === "failed") return theme.bad
        if (state === "cancelled" || state === "recoverable") return theme.warn
        if (["scanning", "connecting", "negotiating", "transferring", "committing"].indexOf(state) >= 0) return theme.accent
        return theme.muted
    }
    function countWhere(predicate) { return rpc.tasks.filter(predicate).length }
    function taskFor(id) {
        for (var i = 0; i < rpc.tasks.length; ++i)
            if (rpc.tasks[i].transfer_id === id) return rpc.tasks[i]
        return null
    }
    function selectedTask() {
        var chosen = taskFor(selectedId)
        return chosen || (rpc.tasks.length ? rpc.tasks[0] : null)
    }
    function selectedProfile() {
        for (var i = 0; i < rpc.profiles.length; ++i)
            if (rpc.profiles[i].node_id === selectedProfileId) return rpc.profiles[i]
        return rpc.profiles.length ? rpc.profiles[0] : null
    }
    function selectTask(task) {
        if (!task) return
        selectedId = task.transfer_id
        page = 2
        rpc.loadDetails(task.transfer_id, task.attempt ? task.attempt.attempt_id : "")
    }
    function newTransfer(direction) {
        presetDirection = direction
        createStep = 0
        page = 1
    }
    function pickPath(directory, target) {
        pickerDirectory = directory
        pickerTarget = target || "source"
        pathPicker.active = true
        if (pathPicker.status === Loader.Ready) {
            pathPicker.item.directoryMode = directory
            pathPicker.item.open()
        }
    }
    function loadProfile(profile) {
        if (!profile) return
        selectedProfileId = profile.node_id
        nodeId.text = profile.node_id
        nodeName.text = profile.name
        host.text = profile.host
        port.text = String(profile.control_port)
        rootPath.text = profile.default_root_id || ""
        caPath.text = profile.ca_path || ""
        tlsName.text = profile.server_name || ""
        credentialRef.text = profile.credential_ref || ""
        authMode.currentIndex = profile.auth_mode === "admin_credential" ? 1 : 0
    }

    QtObject {
        id: theme
        readonly property color canvas: "#0b1220"
        readonly property color sidebar: "#101a2b"
        readonly property color panel: "#142238"
        readonly property color panelRaised: "#192a42"
        readonly property color line: "#2b405b"
        readonly property color ink: "#e8f0fa"
        readonly property color muted: "#91a4bc"
        readonly property color accent: "#55e0c1"
        readonly property color blue: "#85a8ff"
        readonly property color warn: "#f0b75e"
        readonly property color bad: "#f07878"
        readonly property color good: "#78d8a7"
    }

    component Panel: Rectangle {
        default property alias contents: body.data
        color: theme.panel
        border.color: theme.line
        border.width: 1
        radius: 9
        implicitWidth: 220
        implicitHeight: Math.max(72, body.implicitHeight + 32)
        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
        }
    }
    component Note: Label {
        color: theme.muted
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component Eyebrow: Label {
        color: theme.accent
        font.family: "Consolas"
        font.pixelSize: 10
        font.letterSpacing: 1.1
    }
    component PageTitle: Label {
        color: theme.ink
        font.pixelSize: 29
        font.bold: true
        Layout.fillWidth: true
    }
    component Field: TextField {
        implicitHeight: 42
        color: theme.ink
        placeholderTextColor: "#71859d"
        selectByMouse: true
        padding: 12
        background: Rectangle {
            color: "#0e192a"
            radius: 5
            border.color: parent.activeFocus ? theme.accent : theme.line
            border.width: parent.activeFocus ? 2 : 1
        }
    }
    component ActionButton: Button {
        id: control
        property bool primary: false
        property color tint: theme.accent
        implicitHeight: 40
        horizontalPadding: 14
        contentItem: Text {
            text: control.text
            color: !control.enabled ? "#687b91" : (control.primary ? theme.canvas : theme.ink)
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.bold: control.primary || control.activeFocus
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 5
            color: !control.enabled ? "#17253a" : control.primary ? (control.down ? "#39b99d" : control.tint) : (control.hovered ? "#203550" : "#17283f")
            border.color: control.activeFocus ? theme.ink : (control.primary ? control.tint : theme.line)
            border.width: control.activeFocus ? 2 : 1
        }
    }
    component StatusTag: Rectangle {
        property string label: ""
        property color tone: theme.muted
        radius: 4
        color: Qt.rgba(tone.r, tone.g, tone.b, 0.14)
        border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.5)
        implicitWidth: tagText.implicitWidth + 18
        implicitHeight: 26
        Text {
            id: tagText
            anchors.centerIn: parent
            text: parent.label
            color: parent.tone
            font.pixelSize: 11
            font.bold: true
        }
    }
    component Meter: ProgressBar {
        id: meter
        property var done: null
        property var total: null
        visible: root.progressKnown(done, total)
        value: root.fraction(done, total)
        implicitHeight: 7
        background: Rectangle { color: "#22344b"; radius: 3 }
        contentItem: Item {
            Rectangle {
                width: meter.visualPosition * parent.width
                height: parent.height
                color: theme.accent
                radius: 3
            }
        }
    }

    Connections {
        target: rpc
        function onPageRequested(value) { root.page = value }
        function onCreateSucceeded(transferId, attemptId, revision) {
            root.selectedId = transferId
            root.page = 2
            rpc.loadDetails(transferId, attemptId)
        }
    }

    Dialog {
        id: hideDialog
        modal: true
        title: "隐藏任务"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var task: null
        width: 440
        contentItem: Label {
            text: hideDialog.task ? "从此设备的任务列表隐藏 " + hideDialog.task.transfer_id + "？不会删除 agent 任务或远端数据。" : ""
            color: theme.ink
            wrapMode: Text.WordWrap
            padding: 18
        }
        onAccepted: {
            if (task) rpc.removeTask(task.transfer_id, task.adapter || (rpc.demoAdapter ? "demo" : "real"))
            task = null
        }
        onRejected: task = null
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 224
            Layout.minimumWidth: 204
            color: theme.sidebar
            border.color: theme.line
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    spacing: 10
                    Rectangle {
                        width: 34; height: 34; radius: 8
                        color: theme.accent
                        Text { anchors.centerIn: parent; text: "CF"; color: theme.canvas; font.bold: true; font.pixelSize: 13 }
                    }
                    ColumnLayout {
                        spacing: 0
                        Label { text: "CPNetFlux"; color: theme.ink; font.pixelSize: 18; font.bold: true }
                        Label { text: "MISSION CONTROL"; color: theme.muted; font.family: "Consolas"; font.pixelSize: 9; font.letterSpacing: 1 }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: theme.line; Layout.topMargin: 9; Layout.bottomMargin: 6 }
                Eyebrow { text: "OPERATIONS" }

                Repeater {
                    model: [
                        {p:0, mark:"⌂", name:"控制台"},
                        {p:1, mark:"＋", name:"新建传输"},
                        {p:2, mark:"◎", name:"任务指挥"},
                        {p:3, mark:"⇄", name:"节点与身份"},
                        {p:4, mark:"▤", name:"文件与分段"},
                        {p:5, mark:"◷", name:"历史任务"},
                        {p:6, mark:"⌘", name:"能力与诊断"}
                    ]
                    Button {
                        id: nav
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 43
                        text: modelData.name
                        onClicked: root.page = modelData.p
                        background: Rectangle {
                            radius: 6
                            color: root.page === nav.modelData.p ? "#243b53" : (nav.hovered ? "#192b41" : "transparent")
                            border.color: root.page === nav.modelData.p ? theme.accent : "transparent"
                            border.width: root.page === nav.modelData.p ? 1 : 0
                        }
                        contentItem: RowLayout {
                            spacing: 11
                            Label {
                                text: nav.modelData.mark
                                color: root.page === nav.modelData.p ? theme.accent : theme.muted
                                font.pixelSize: 17
                            }
                            Label {
                                text: nav.text
                                Layout.fillWidth: true
                                color: root.page === nav.modelData.p ? theme.ink : "#c3d0df"
                                font.bold: root.page === nav.modelData.p
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Panel {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Eyebrow { text: "LOCAL AGENT" }
                        StatusTag { label: root.canOperate ? "API READY" : rpc.connected ? "NEGOTIATING" : "OFFLINE"; tone: root.canOperate ? theme.good : theme.warn }
                        Note { text: rpc.demoAdapter ? "设计诊断 / 合成数据" : (rpc.stale ? "快照已过期，等待重连" : "真实 agent 状态") }
                    }
                }
                Label { text: "UI 不持有私钥或明文 token"; color: theme.muted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                color: "#0e1828"
                border.color: theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    Label { text: "WORKSPACE   /   " + root.pageTitles[root.page]; color: theme.muted; font.family: "Consolas"; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    StatusTag { label: rpc.connected ? "SOCKET CONNECTED" : "SOCKET OFFLINE"; tone: rpc.connected ? theme.good : theme.bad }
                    StatusTag { label: rpc.apiCompatible ? ("API " + (rpc.agentInfo.api_version || "unknown")) : "API UNVERIFIED"; tone: rpc.apiCompatible ? theme.blue : theme.warn }
                    ActionButton { text: "刷新 agent"; enabled: true; onClicked: rpc.refresh() }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: rpc.demoAdapter ? "#392b26" : (root.canOperate ? "#17352f" : "#3a2c20")
                border.color: rpc.demoAdapter ? "#955d43" : (root.canOperate ? "#2f766d" : "#866334")
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    Label {
                        Layout.fillWidth: true
                        text: rpc.demoAdapter
                              ? "DESIGN PREVIEW · 合成数据 / 未连接 agent / 所有真实操作已隔离"
                              : root.canOperate ? "真实传输模式 · 所有任务状态和结果来自 agent"
                                           : "真实传输不可用 · " + rpc.message
                        color: rpc.demoAdapter ? "#ffc59d" : root.canOperate ? theme.good : theme.warn
                        elide: Text.ElideRight
                    }
                    Label { text: rpc.agentInfo.api_version || "协议版本未知"; color: theme.muted; font.family: "Consolas"; font.pixelSize: 10 }
                }
            }

            StackLayout {
                id: pages
                currentIndex: root.page
                Layout.fillWidth: true
                Layout.fillHeight: true

                // 0 — Mission control: launch actions, snapshot totals, and live task ledger.
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Eyebrow { text: "TRANSFER OPERATIONS  /  SNAPSHOT VIEW" }
                                PageTitle { text: "任务控制台" }
                                Note { text: "从单个文件到批量目录任务。进度只在 agent 返回对应字段时显示。" }
                            }
                            ActionButton { text: "＋ 新建传输"; primary: true; onClicked: root.newTransfer(0) }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 12
                            Repeater {
                                model: [
                                    {label:"任务总数", n:rpc.tasks.length, hint:"当前 agent 快照"},
                                    {label:"进行中", n:root.countWhere(function(t){return !root.terminal(t.state)}), hint:"排队与活跃任务"},
                                    {label:"已完成", n:root.countWhere(function(t){return t.state === "completed"}), hint:"agent 终态"},
                                    {label:"需处理", n:root.countWhere(function(t){return t.state === "failed"}), hint:"失败任务"}
                                ]
                                Panel {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Note { text: modelData.label }
                                        Label { text: modelData.n; color: theme.ink; font.pixelSize: 31; font.bold: true; font.family: "Consolas" }
                                        Label { text: modelData.hint; color: theme.muted; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }

                        Panel {
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Eyebrow { text: "QUICK LAUNCH" }
                                    Label { text: "选择一个入口"; color: theme.ink; font.pixelSize: 17; font.bold: true }
                                    Note { text: "快捷入口只设置传输类型或打开配置页；创建前仍需确认本地路径、节点与目标路径。" }
                                }
                                ActionButton { text: "↑ 上传文件 / 目录"; primary: true; onClicked: root.newTransfer(0) }
                                ActionButton { text: "↓ 下载到本机"; tint: theme.blue; onClicked: root.newTransfer(1) }
                                ActionButton { text: "节点 / TLS"; onClicked: root.page = 3 }
                                ActionButton { text: "能力中心"; onClicked: root.page = 6 }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Panel {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "传输队列"; color: theme.ink; font.pixelSize: 17; font.bold: true }
                                        StatusTag { label: rpc.tasks.length + " SNAPSHOTS"; tone: theme.blue }
                                        Item { Layout.fillWidth: true }
                                        ActionButton { text: "打开历史"; onClicked: root.page = 5 }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "任务 / 目标"; color: theme.muted; Layout.fillWidth: true }
                                        Label { text: "状态"; color: theme.muted; width: 124 }
                                        Label { text: "已完成 / 总量"; color: theme.muted; width: 220 }
                                        Label { text: "动作"; color: theme.muted; width: 116 }
                                    }
                                    Repeater {
                                        model: rpc.tasks
                                        Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            implicitHeight: 78
                                            radius: 5
                                            color: "#0f1b2c"
                                            border.color: theme.line
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 4
                                                    Label {
                                                        text: modelData.source.path.split("/").pop() || modelData.source.path
                                                        color: theme.ink; font.bold: true; elide: Text.ElideMiddle
                                                    }
                                                    Label {
                                                        text: modelData.source.path + "  →  " + modelData.destination.path
                                                        color: theme.muted; font.pixelSize: 10; elide: Text.ElideMiddle
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                                StatusTag { label: root.statusLabel(modelData.state); tone: root.statusTone(modelData.state); Layout.preferredWidth: 118 }
                                                ColumnLayout {
                                                    Layout.preferredWidth: 210
                                                    Label {
                                                        text: root.bytes(modelData.metrics.bytes_committed) + " / " + root.bytes(modelData.metrics.bytes_total)
                                                        color: theme.ink; font.family: "Consolas"; font.pixelSize: 10
                                                    }
                                                    Meter { Layout.fillWidth: true; done: modelData.metrics.bytes_committed; total: modelData.metrics.bytes_total }
                                                }
                                                ActionButton { text: "详情"; onClicked: root.selectTask(modelData) }
                                            }
                                        }
                                    }
                                    Label {
                                        visible: rpc.tasks.length === 0
                                        Layout.fillWidth: true
                                        text: "当前快照没有任务。启动 agent 后，新建任务会在这里显示。"
                                        color: theme.muted
                                        padding: 18
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 276
                                Layout.fillHeight: true
                                Panel {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Eyebrow { text: "LINK HEALTH" }
                                        Label { text: rpc.connected ? "本地 socket 已连接" : "等待本地 agent"; color: theme.ink; font.bold: true }
                                        Note { text: rpc.message }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { text: "协议"; color: theme.muted; Layout.fillWidth: true }
                                            Label { text: rpc.agentInfo.api_version || "未知"; color: theme.ink; font.family: "Consolas" }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { text: "兼容性"; color: theme.muted; Layout.fillWidth: true }
                                            StatusTag { label: rpc.apiCompatible ? "兼容" : "未确认"; tone: rpc.apiCompatible ? theme.good : theme.warn }
                                        }
                                        ActionButton { text: "重新检查连接"; Layout.fillWidth: true; onClicked: rpc.refresh() }
                                    }
                                }
                                Panel {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Eyebrow { text: "CONTROL DECK" }
                                        ActionButton { text: "选择任务"; Layout.fillWidth: true; enabled: rpc.tasks.length > 0; onClicked: root.selectTask(root.selectedTask()) }
                                        ActionButton { text: "取消选中任务"; Layout.fillWidth: true; enabled: root.canOperate && root.selectedTask() && !root.terminal(root.selectedTask().state) && !rpc.actionInFlight; onClicked: rpc.action("transfer.cancel", root.selectedTask()) }
                                        ActionButton { text: "暂停"; Layout.fillWidth: true; enabled: false; tint: theme.warn }
                                        Note { text: "暂停：agent API 当前不支持；该入口不会伪装成已执行。" }
                                    }
                                }
                            }
                        }
                    }
                }

                // 1 — Guided create flow; the three panels expose real preparation steps.
                ScrollView {
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Eyebrow { text: "TRANSFER WIZARD  /  PREPARE → REVIEW → SUBMIT" }
                                PageTitle { text: "新建传输" }
                                Note { text: "创建前逐项确认数据来源、远端节点和安全引用。" }
                            }
                            ActionButton { text: "返回控制台"; onClicked: root.page = 0 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Repeater {
                                model: ["01  数据来源", "02  节点与安全", "03  检查并提交"]
                                ActionButton {
                                    required property int index
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    primary: root.createStep === index
                                    enabled: index <= root.createStep + 1
                                    onClicked: root.createStep = index
                                }
                            }
                        }

                        Panel {
                            visible: root.createStep === 0
                            Layout.fillWidth: true
                            Eyebrow { text: "STEP 01  /  SOURCE AND DESTINATION" }
                            Label { text: "要传什么、传到哪里？"; color: theme.ink; font.pixelSize: 20; font.bold: true }
                            GridLayout {
                                columns: 2; Layout.fillWidth: true; columnSpacing: 16; rowSpacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: "传输方向"; color: theme.muted }
                                    ComboBox {
                                        id: directionChoice
                                        Layout.fillWidth: true
                                        model: ["上传到远端", "下载到本机"]
                                        currentIndex: root.presetDirection
                                        onActivated: root.presetDirection = index
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: "本地数据类型"; color: theme.muted }
                                    ComboBox {
                                        id: sourceKind
                                        Layout.fillWidth: true
                                        model: ["单个文件", "目录树"]
                                        onActivated: root.pickerDirectory = index === 1
                                    }
                                }
                                ColumnLayout {
                                    Layout.columnSpan: 2; Layout.fillWidth: true
                                    Label { text: directionChoice.currentIndex === 0 ? "本地来源路径" : "本地接收位置"; color: theme.muted }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Field { id: localPath; objectName: "localPath"; Layout.fillWidth: true; placeholderText: "绝对路径；提交时由 agent 检查可访问性" }
                                        ActionButton { text: "选文件"; onClicked: { sourceKind.currentIndex = 0; root.pickPath(false) } }
                                        ActionButton { text: "选目录"; onClicked: { sourceKind.currentIndex = 1; root.pickPath(true) } }
                                    }
                                }
                                ColumnLayout {
                                    Layout.columnSpan: 2; Layout.fillWidth: true
                                    Label { text: directionChoice.currentIndex === 0 ? "远端目标相对路径" : "远端来源相对路径"; color: theme.muted }
                                    Field { id: remotePath; Layout.fillWidth: true; placeholderText: "例如 incoming/project-data" }
                                }
                            }
                            Note { text: "UI 不预扫目录来估算进度。agent 创建任务后返回真实文件清单、总量和阶段。" }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { text: "检查本地路径"; enabled: false; tint: theme.warn }
                                Note { text: "本地权限/路径最终由 agent 的创建前校验返回；当前 RPC 没有独立本地预检接口。" }
                            }
                        }
                        Panel {
                            visible: root.createStep === 1
                            Layout.fillWidth: true
                            Eyebrow { text: "STEP 02  /  NODE AND IDENTITY" }
                            Label { text: "选择一个受控节点"; color: theme.ink; font.pixelSize: 20; font.bold: true }
                            Note { text: "节点地址、CA 和凭据引用由 agent 按保存的 profile 处理。" }
                            RowLayout {
                                Layout.fillWidth: true
                                ComboBox {
                                    id: createProfile
                                    Layout.fillWidth: true
                                    model: rpc.profiles.map(function(p) { return p.name + " · " + p.host + ":" + p.control_port })
                                    currentIndex: {
                                        for (var i = 0; i < rpc.profiles.length; ++i)
                                            if (rpc.profiles[i].node_id === root.selectedProfileId) return i
                                        return rpc.profiles.length ? 0 : -1
                                    }
                                    onActivated: {
                                        if (index >= 0 && index < rpc.profiles.length) {
                                            root.selectedProfileId = rpc.profiles[index].node_id
                                            rpc.requestCapabilities(root.selectedProfileId)
                                        }
                                    }
                                }
                                ActionButton { text: "管理节点 / TLS"; onClicked: root.page = 3 }
                                ActionButton { text: "检查节点身份"; enabled: root.canOperate && root.selectedProfile() && root.selectedProfile().default_root_id; onClicked: { var p = root.selectedProfile(); rpc.testNode(p.node_id, p.default_root_id) } }
                            }
                            Panel {
                                Layout.fillWidth: true
                                RowLayout {
                                    Layout.fillWidth: true
                                    StatusTag { label: "TLS / 认证默认要求"; tone: theme.good }
                                    Note { text: root.selectedProfile() ? ("CA: " + root.value(root.selectedProfile().ca_path) + "  ·  凭据引用: " + root.value(root.selectedProfile().credential_ref)) : "请先在节点页面配置受控节点。" }
                                }
                            }
                            Note {
                                visible: !!root.selectedProfile() && !root.selectedProfile().default_root_id
                                text: "该 profile 没有 agent 授权根目录 ID；真实任务创建已禁用。请在节点配置中填写 agent 返回的授权目录 ID。"
                                color: theme.warn
                            }
                            Note { text: "证书与 token 内容不会进入 QML；这里只引用 agent 已保存的安全配置。" }
                            ActionButton { text: "导入节点配置包"; Layout.fillWidth: true; enabled: false; tint: theme.warn }
                            Note { text: "配置包导入 API 尚未提供。可以先保存单个节点 profile。" }
                        }
                        Panel {
                            visible: root.createStep === 2
                            Layout.fillWidth: true
                            Eyebrow { text: "STEP 03  /  PREFLIGHT AND SUBMIT" }
                            Label { text: "最后核对"; color: theme.ink; font.pixelSize: 20; font.bold: true }
                            GridLayout {
                                columns: 2; Layout.fillWidth: true; columnSpacing: 12; rowSpacing: 10
                                Label { text: "方向"; color: theme.muted }
                                Label { text: root.presetDirection === 0 ? "本机 → 远端" : "远端 → 本机"; color: theme.ink }
                                Label { text: "来源"; color: theme.muted }
                                Label { text: localPath.text.length ? localPath.text : "未填写"; color: theme.ink; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { text: "远端路径"; color: theme.muted }
                                Label { text: remotePath.text.length ? remotePath.text : "未填写"; color: theme.ink; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { text: "节点"; color: theme.muted }
                                Label { text: root.selectedProfile() ? root.selectedProfile().name : "未选择"; color: theme.ink }
                                Label { text: "安全策略"; color: theme.muted }
                                Label { text: "TLS / 身份校验必需 · checksum required"; color: theme.good }
                            }
                            Note { text: "创建请求会经过 agent 幂等与路径/权限检查。若校验失败，会显示 agent 的错误，不生成假任务。" }
                            Label { visible: rpc.createStatus.length > 0; text: rpc.createStatus; color: theme.warn; wrapMode: Text.WordWrap }
                            Label { visible: rpc.createError.length > 0; text: rpc.createError; color: theme.bad; wrapMode: Text.WordWrap }
                            ActionButton {
                                objectName: "createButton"
                                Layout.fillWidth: true
                                text: rpc.createInFlight ? "正在请求 agent…" : "向 agent 创建真实任务"
                                primary: true
                                enabled: root.canOperate && !rpc.createInFlight && root.selectedProfile() && root.selectedProfile().default_root_id && localPath.text.length > 0 && remotePath.text.length > 0
                                onClicked: rpc.create(root.presetDirection === 0 ? "upload" : "download", localPath.text, remotePath.text, false, "v2", root.selectedProfileId || root.selectedProfile().node_id)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton { text: "← 上一步"; enabled: root.createStep > 0; onClicked: root.createStep = Math.max(0, root.createStep - 1) }
                            Item { Layout.fillWidth: true }
                            ActionButton { text: "下一步 →"; visible: root.createStep < 2; enabled: root.createStep === 0 ? localPath.text.length > 0 && remotePath.text.length > 0 : !!root.selectedProfile(); onClicked: root.createStep = Math.min(2, root.createStep + 1) }
                        }
                    }
                }

                // 2 — Task command: real task facts, real controls, and phase evidence.
                ScrollView {
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Eyebrow { text: "MISSION DETAIL  /  AGENT SNAPSHOT" }
                                PageTitle { text: root.selectedTask() ? root.statusLabel(root.selectedTask().state) : "请选择任务" }
                                Note { text: root.selectedTask() ? (root.selectedTask().source.path + "  →  " + root.selectedTask().destination.path) : "从控制台选择任务以查看 agent 返回的数据。" }
                            }
                            ActionButton { text: "文件 / 分段"; enabled: !!root.selectedTask(); onClicked: root.page = 4 }
                            ActionButton { text: "刷新任务快照"; enabled: root.canOperate && !!root.selectedTask(); onClicked: { var t = root.selectedTask(); rpc.loadDetails(t.transfer_id, t.attempt.attempt_id) } }
                        }
                        Panel {
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Eyebrow { text: "TRANSFER ID" }
                                    Label { text: root.selectedTask() ? root.selectedTask().transfer_id : "未选择"; color: theme.ink; font.family: "Consolas"; font.pixelSize: 16; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                    Label { text: root.selectedTask() ? ("Attempt " + root.value(root.selectedTask().attempt.attempt_id) + "  ·  revision " + root.value(root.selectedTask().revision)) : "等待任务"; color: theme.muted; font.family: "Consolas"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                }
                                StatusTag { label: root.selectedTask() ? root.statusLabel(root.selectedTask().state) : "暂无"; tone: root.selectedTask() ? root.statusTone(root.selectedTask().state) : theme.muted }
                                ActionButton { text: rpc.actionInFlight ? "请求中…" : "启动"; enabled: root.canOperate && !!root.selectedTask() && root.selectedTask().state === "queued" && !rpc.actionInFlight; primary: true; onClicked: rpc.action("transfer.start", root.selectedTask()) }
                                ActionButton { text: "取消"; enabled: root.canOperate && !!root.selectedTask() && !root.terminal(root.selectedTask().state) && !rpc.actionInFlight; tint: theme.bad; onClicked: rpc.action("transfer.cancel", root.selectedTask()) }
                                ActionButton { text: "恢复"; enabled: root.canOperate && !!rpc.capabilities.resume_supported && !!root.selectedTask() && root.selectedTask().recovery.eligibility === "eligible" && root.terminal(root.selectedTask().state) && !rpc.actionInFlight; tint: theme.blue; onClicked: rpc.action("transfer.resume", root.selectedTask()) }
                            }
                        }
                        Label { visible: rpc.actionStatus.length > 0 || rpc.actionError.length > 0; Layout.fillWidth: true; text: rpc.actionError.length > 0 ? rpc.actionError : rpc.actionStatus; color: rpc.actionError.length > 0 ? theme.bad : theme.warn; wrapMode: Text.WordWrap }
                        RowLayout {
                            Layout.fillWidth: true
                            Panel {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Eyebrow { text: "TRANSFER PHASE" }
                                    Repeater {
                                        model: [
                                            {key:"scan", name:"01  目录扫描"},
                                            {key:"prepare", name:"02  control_prepare / data_connect"},
                                            {key:"payload", name:"03  first_payload / payload_io"},
                                            {key:"finish", name:"04  complete_wait / finalize"}
                                        ]
                                        RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Rectangle { width: 9; height: 9; radius: 5; color: root.selectedTask() && ((modelData.key === "scan" && root.selectedTask().state === "scanning") || (modelData.key === "prepare" && ["connecting","negotiating"].indexOf(root.selectedTask().state)>=0) || (modelData.key === "payload" && root.selectedTask().state === "transferring") || (modelData.key === "finish" && ["committing","completed"].indexOf(root.selectedTask().state)>=0)) ? theme.accent : theme.line }
                                            Label { text: modelData.name; color: theme.ink; Layout.fillWidth: true; font.bold: true }
                                            Label { text: root.selectedTask() && root.selectedTask().backend_evidence && root.selectedTask().backend_evidence.phase_timing ? root.value(root.selectedTask().backend_evidence.phase_timing[modelData.key === "prepare" ? "control_prepare" : modelData.key === "payload" ? "payload_io" : modelData.key === "finish" ? "transfer_complete_wait" : "scan"]) : "agent 未提供"; color: theme.muted; font.family: "Consolas"; font.pixelSize: 10 }
                                        }
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 290
                                Panel {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Eyebrow { text: "BACKEND METRICS" }
                                        Label { text: root.selectedTask() ? root.bytes(root.selectedTask().metrics.bytes_committed) + " / " + root.bytes(root.selectedTask().metrics.bytes_total) : "未提供"; color: theme.ink; font.pixelSize: 17; font.family: "Consolas" }
                                        Meter { Layout.fillWidth: true; done: root.selectedTask() ? root.selectedTask().metrics.bytes_committed : null; total: root.selectedTask() ? root.selectedTask().metrics.bytes_total : null }
                                        Note { text: root.selectedTask() ? ("files " + root.value(root.selectedTask().metrics.files_committed) + " / " + root.value(root.selectedTask().metrics.files_total)) : "文件数未提供" }
                                        Note { text: root.selectedTask() ? ("速度 " + root.rate(root.selectedTask().metrics.speed_bps) + "  ·  ETA " + root.value(root.selectedTask().metrics.eta_seconds, " s")) : "" }
                                    }
                                }
                                Panel {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Eyebrow { text: "INTEGRITY" }
                                        Label { text: "manifest  ·  " + (root.selectedTask() && root.selectedTask().backend_evidence.manifest ? JSON.stringify(root.selectedTask().backend_evidence.manifest) : "未提供"); color: theme.ink; wrapMode: Text.WordWrap }
                                        Label { text: "checksum  ·  " + (root.selectedTask() && root.selectedTask().backend_evidence.checksum ? JSON.stringify(root.selectedTask().backend_evidence.checksum) : "未提供"); color: theme.ink; wrapMode: Text.WordWrap }
                                        Label { text: "finalize  ·  " + (root.selectedTask() && root.selectedTask().backend_evidence.finalize ? JSON.stringify(root.selectedTask().backend_evidence.finalize) : "未提供"); color: theme.ink; wrapMode: Text.WordWrap }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton { text: "暂停任务"; enabled: false; tint: theme.warn }
                            ActionButton { text: "复制任务"; enabled: false; tint: theme.blue }
                            ActionButton { text: "重新校验"; enabled: false; tint: theme.blue }
                            ActionButton { text: "导出报告"; enabled: false }
                            Note { text: "暂停、复制和导出操作尚无 agent RPC；界面入口已预留，当前不会产生假成功。" }
                        }
                        Panel {
                            Layout.fillWidth: true
                            Eyebrow { text: "ERROR / RECOVERY" }
                            Label {
                                text: root.selectedTask() && root.selectedTask().error ? (root.selectedTask().error.code + "  ·  " + root.selectedTask().error.message) : "当前快照没有 agent 错误"
                                color: root.selectedTask() && root.selectedTask().error ? theme.bad : theme.muted
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Note { text: root.selectedTask() && root.selectedTask().recovery ? ("恢复资格：" + root.value(root.selectedTask().recovery.eligibility) + "  ·  " + root.value(root.selectedTask().recovery.reason)) : "恢复资格未知" }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { text: "查看事件与文件"; onClicked: root.page = 4 }
                                ActionButton { text: "隐藏本机列表项"; enabled: !!root.selectedTask() && !rpc.removeInFlight; onClicked: { hideDialog.task = root.selectedTask(); hideDialog.open() } }
                                Item { Layout.fillWidth: true }
                                Label { text: rpc.detailStatus + "  ·  " + rpc.detailWatermark; color: theme.muted; font.pixelSize: 10; elide: Text.ElideRight; Layout.maximumWidth: 420 }
                            }
                        }
                    }
                }

                // 3 — Node portfolio, saved identity references, and real preflight.
                ScrollView {
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Eyebrow { text: "NODE PORTFOLIO  /  TRUST BOUNDARY" }
                                PageTitle { text: "节点与身份" }
                                Note { text: "保存连接 profile 与受保护凭据引用。UI 不接收私钥、密码或 token 内容。" }
                            }
                            ActionButton { text: "＋ 新节点"; primary: true; onClicked: { root.selectedProfileId = ""; nodeId.clear(); nodeName.clear(); host.clear(); port.clear(); rootPath.clear(); caPath.clear(); tlsName.clear(); credentialRef.clear() } }
                            ActionButton { text: "导入配置包"; enabled: false; tint: theme.warn }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Panel {
                                Layout.preferredWidth: 300
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Eyebrow { text: "SAVED PROFILES" }
                                    Repeater {
                                        model: rpc.profiles
                                        ActionButton {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            text: modelData.name + "\n" + modelData.host + ":" + modelData.control_port
                                            primary: modelData.node_id === root.selectedProfileId
                                            onClicked: root.loadProfile(modelData)
                                        }
                                    }
                                    Label { visible: rpc.profiles.length === 0; text: "尚无 profile"; color: theme.muted }
                                }
                            }
                            Panel {
                                Layout.fillWidth: true
                                Eyebrow { text: "ENDPOINT" }
                                GridLayout {
                                    columns: 2; Layout.fillWidth: true; columnSpacing: 14; rowSpacing: 8
                                    Label { text: "节点 ID"; color: theme.muted }
                                    Field { id: nodeId; placeholderText: "必填：唯一节点 ID"; Layout.fillWidth: true }
                                    Label { text: "显示名称"; color: theme.muted }
                                    Field { id: nodeName; placeholderText: "必填：便于识别的名称"; Layout.fillWidth: true }
                                    Label { text: "地址 / 主机名"; color: theme.muted }
                                    Field { id: host; placeholderText: "必填：远端主机名或 IP"; Layout.fillWidth: true }
                                    Label { text: "控制端口"; color: theme.muted }
                                    Field {
                                        id: port
                                        placeholderText: "必填：控制端口"
                                        validator: IntValidator {
                                            bottom: 1
                                            top: 65535
                                        }
                                        Layout.fillWidth: true
                                    }
                                    Label { text: "TLS server name"; color: theme.muted }
                                    Field { id: tlsName; placeholderText: "证书 SAN 对应名称"; Layout.fillWidth: true }
                                    Label { text: "授权根目录 ID"; color: theme.muted }
                                    Field { id: rootPath; placeholderText: "必填：agent 授权的目录 ID"; Layout.fillWidth: true }
                                    Label { text: "CA 文件路径"; color: theme.muted }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Field { id: caPath; placeholderText: "必填：CA 文件路径"; Layout.fillWidth: true }
                                        ActionButton { text: "选择 CA 文件"; onClicked: root.pickPath(false, "ca") }
                                    }
                                    Label { text: "凭据引用"; color: theme.muted }
                                    Field { id: credentialRef; placeholderText: "只填 OS secret store 中的受保护引用"; Layout.fillWidth: true }
                                    Label { text: "认证入口"; color: theme.muted }
                                    ComboBox { id: authMode; model: ["短期 token 引用", "管理员凭据引用"]; Layout.fillWidth: true }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    ActionButton {
                                        text: "保存 profile"
                                        primary: true
                                        enabled: root.canOperate && port.acceptableInput && nodeId.text.length > 0 && nodeName.text.length > 0 && host.text.length > 0 && caPath.text.length > 0 && credentialRef.text.length > 0
                                        onClicked: {
                                            var update = rpc.profiles.some(function(p) { return p.node_id === nodeId.text })
                                            root.selectedProfileId = nodeId.text
                                            rpc.saveProfile({node_id:nodeId.text, name:nodeName.text, host:host.text,
                                                control_port:parseInt(port.text), server_name:tlsName.text, ca_path:caPath.text,
                                                auth_mode:["token","admin_credential"][authMode.currentIndex],
                                                credential_ref:credentialRef.text, default_root_id:rootPath.text.length ? rootPath.text : null}, update)
                                        }
                                    }
                                    ActionButton { text: "TLS / 登录 / 权限检查"; enabled: root.canOperate && nodeId.text.length > 0 && rootPath.text.length > 0; onClicked: rpc.testNode(nodeId.text, rootPath.text) }
                                    ActionButton { text: "选择客户端证书"; enabled: false; tint: theme.warn }
                                    ActionButton { text: "选择私钥"; enabled: false; tint: theme.warn }
                                    ActionButton { text: "选择 token 文件"; enabled: false; tint: theme.warn }
                                }
                                Note { text: "当前 profile API 接收 CA 路径和 OS secret store 的 credential_ref。客户端证书、私钥和 token 文件导入接口尚未提供；对应入口禁用，不会把 secret 内容传给 QML。" }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true
                            Eyebrow { text: "PREFLIGHT RESULTS  /  AGENT RETURNED" }
                            Repeater {
                                model: [
                                    {key:"dns_control_port",label:"网络与控制端口"},
                                    {key:"tls_identity",label:"TLS 身份"},
                                    {key:"login",label:"登录认证"},
                                    {key:"capabilities",label:"能力协商"},
                                    {key:"directory_permission",label:"目录权限"}
                                ]
                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label { text: modelData.label; color: theme.ink; Layout.fillWidth: true }
                                    Label { text: rpc.checks[modelData.key] ? rpc.checks[modelData.key].state + " · " + rpc.checks[modelData.key].message : "未检查"; color: rpc.checks[modelData.key] ? theme.blue : theme.muted; Layout.fillWidth: true }
                                }
                            }
                            Note { text: "导入/密钥托管入口在 API 尚未支持时禁用，不存放 secret 到 profile。" }
                        }
                    }
                }

                // 4 — Paginated agent files/ranges. Directory browsing remains an explicit gap.
                ScrollView {
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Eyebrow { text: "TASK ARTIFACTS  /  IMMUTABLE SNAPSHOT PAGES" }
                                PageTitle { text: "文件与分段" }
                                Note { text: root.selectedTask() ? ("任务 " + root.selectedTask().transfer_id + "  ·  file 与 range 页来自 agent") : "选择任务后查询文件清单。" }
                            }
                            ActionButton { text: "刷新任务详情"; enabled: root.canOperate && !!root.selectedTask(); onClicked: { var t = root.selectedTask(); rpc.loadDetails(t.transfer_id, t.attempt.attempt_id) } }
                            ActionButton { text: "加载下一页文件"; enabled: root.canOperate && rpc.files.length > 0; onClicked: rpc.loadNextFiles() }
                            ActionButton { text: "加载分段"; enabled: root.canOperate && rpc.files.length > 0; tint: theme.blue; onClicked: rpc.loadNextRanges() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Panel {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "文件清单"; color: theme.ink; font.pixelSize: 18; font.bold: true }
                                        StatusTag { label: rpc.files.length + " ITEMS"; tone: theme.blue }
                                        Item { Layout.fillWidth: true }
                                        ActionButton { text: "远端目录浏览"; enabled: false; tint: theme.warn }
                                    }
                                    Repeater {
                                        model: rpc.files
                                        Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true; implicitHeight: 62; radius: 5
                                            color: "#0f1b2c"; border.color: theme.line
                                            RowLayout {
                                                anchors.fill: parent; anchors.margins: 9
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    Label { text: root.value(modelData.relative_path || modelData.path); color: theme.ink; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                                    Label { text: "file_id " + root.value(modelData.file_id) + "  ·  " + root.value(modelData.state); color: theme.muted; font.pixelSize: 10; font.family: "Consolas" }
                                                }
                                                Label { text: root.bytes(modelData.bytes_committed) + " / " + root.bytes(modelData.size_bytes); color: theme.muted; font.family: "Consolas"; font.pixelSize: 10 }
                                            }
                                        }
                                    }
                                    Label { visible: rpc.files.length === 0; text: "暂无文件页。打开任务详情后由 agent 查询 attempt.list 与 file.list。"; color: theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; padding: 12 }
                                }
                            }
                            Panel {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "Range / 分段"; color: theme.ink; font.pixelSize: 18; font.bold: true }
                                        StatusTag { label: rpc.ranges.length + " ITEMS"; tone: theme.accent }
                                    }
                                    Repeater {
                                        model: rpc.ranges
                                        Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true; implicitHeight: 52; radius: 5
                                            color: "#0f1b2c"; border.color: theme.line
                                            RowLayout {
                                                anchors.fill: parent; anchors.margins: 9
                                                Label { text: "range " + root.value(modelData.range_id); color: theme.ink; Layout.fillWidth: true }
                                                Label { text: root.value(modelData.state); color: theme.muted }
                                                Label { text: root.bytes(modelData.bytes_committed); color: theme.muted; font.family: "Consolas" }
                                            }
                                        }
                                    }
                                    Label { visible: rpc.ranges.length === 0; text: "尚未加载分段；若 backend 返回文件 ID，可查询对应 range。"; color: theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; padding: 12 }
                                    ActionButton { text: "导出文件清单"; enabled: false; Layout.fillWidth: true }
                                    Note { text: "清单导出与远端目录浏览暂未暴露独立 RPC。" }
                                }
                            }
                        }
                    }
                }

                // 5 — Persistent task history, sourced from the agent snapshots.
                ScrollView {
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        Eyebrow { text: "PERSISTENT TASKS  /  RESUME IS CAPABILITY-GATED" }
                        PageTitle { text: "历史任务" }
                        Note { text: "退出界面不会结束 agent 任务。重新连接后，列表与终态以 agent 快照为准。" }
                        Panel {
                            Layout.fillWidth: true
                            Label { text: "已结束与可恢复任务"; color: theme.ink; font.pixelSize: 18; font.bold: true }
                            Repeater {
                                model: rpc.tasks.filter(function(t) { return root.terminal(t.state) })
                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true; implicitHeight: 70; radius: 5
                                    color: "#0f1b2c"; border.color: theme.line
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 10
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 3
                                            Label { text: root.statusLabel(modelData.state) + "  ·  " + modelData.source.path; color: theme.ink; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                            Label { text: modelData.transfer_id + "  ·  恢复资格 " + root.value(modelData.recovery.eligibility); color: theme.muted; font.family: "Consolas"; font.pixelSize: 10 }
                                        }
                                        ActionButton { text: "查看结果"; onClicked: root.selectTask(modelData) }
                                        ActionButton { text: "恢复"; enabled: root.canOperate && !!rpc.capabilities.resume_supported && modelData.recovery.eligibility === "eligible" && !rpc.actionInFlight; tint: theme.blue; onClicked: rpc.action("transfer.resume", modelData) }
                                    }
                                }
                            }
                            Label { visible: root.countWhere(function(t){return root.terminal(t.state)}) === 0; text: "agent 尚未返回终态任务"; color: theme.muted }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton { text: "清除所选记录"; enabled: false; tint: theme.warn }
                            ActionButton { text: "批量恢复"; enabled: false; tint: theme.blue }
                            Note { text: "批量操作未提供 agent 幂等接口；逐项查看与恢复仍按单任务真实资格调用。" }
                        }
                    }
                }

                // 6 — Capability registry and diagnostic entry points.
                ScrollView {
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { Layout.preferredHeight: 4 }
                        Eyebrow { text: "AGENT CONTRACT  /  CAPABILITY REGISTRY" }
                        PageTitle { text: "能力与诊断" }
                        Note { text: "只有 agent 明确返回的事实才显示为已启用。能力未提供时，操作入口会保留并标明缺口。" }
                        RowLayout {
                            Layout.fillWidth: true
                            Panel {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Eyebrow { text: "CONNECTION" }
                                    Label { text: rpc.connected ? "本地 socket 在线" : "本地 socket 不可用"; color: rpc.connected ? theme.good : theme.bad; font.pixelSize: 18; font.bold: true }
                                    Note { text: "协议/API 版本：" + root.value(rpc.agentInfo.api_version) + "  ·  协商：" + (rpc.apiCompatible ? "兼容" : "未完成") }
                                    Note { text: "连接信息：" + rpc.message }
                                    ActionButton { text: "重连并重新载入任务"; primary: true; Layout.fillWidth: true; onClicked: rpc.refresh() }
                            ActionButton { text: "分步检查当前节点"; enabled: root.canOperate && root.selectedProfile() && root.selectedProfile().default_root_id; Layout.fillWidth: true; onClicked: { var p = root.selectedProfile(); rpc.testNode(p.node_id, p.default_root_id) } }
                                }
                            }
                            Panel {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Eyebrow { text: "AGENT CAPABILITIES" }
                                    Label { text: "Resume"; color: theme.ink; font.bold: true }
                                    Note { text: rpc.capabilities.resume_supported === true ? "agent 声明支持；具体任务仍需有效 checkpoint。" : "未声明支持；恢复入口保持禁用。" }
                                    Label { text: "Pause"; color: theme.ink; font.bold: true }
                                    Note { text: "当前 agent API 不支持暂停。" }
                                    Label { text: "远端目录浏览 / profile 导入 / 批量操作"; color: theme.ink; font.bold: true }
                                    Note { text: "相关 RPC 尚未提供；预留入口不发送请求。" }
                                }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true
                            Eyebrow { text: "EXTENSION POINTS" }
                            GridLayout {
                                columns: 3; Layout.fillWidth: true; columnSpacing: 10; rowSpacing: 10
                                ActionButton { text: "查看 agent 日志"; enabled: false; Layout.fillWidth: true }
                                ActionButton { text: "导出诊断包"; enabled: false; Layout.fillWidth: true }
                                ActionButton { text: "校验安装依赖"; enabled: false; Layout.fillWidth: true }
                                ActionButton { text: "查看服务端目录"; enabled: false; Layout.fillWidth: true }
                                ActionButton { text: "管理客户端证书"; enabled: false; Layout.fillWidth: true }
                                ActionButton { text: "调整并发 / 队列策略"; enabled: false; Layout.fillWidth: true }
                            }
                            Note { text: "这些扩展点在当前 schema/API 尚无对应接口前明确禁用；不会写入本地状态或显示成功。" }
                        }
                        Panel {
                            Layout.fillWidth: true
                            Eyebrow { text: "NEGOTIATED FACTS" }
                            Label {
                                Layout.fillWidth: true
                                text: JSON.stringify(rpc.capabilities, null, 2)
                                color: theme.muted
                                font.family: "Consolas"
                                font.pixelSize: 10
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: pathPicker
        active: false
        source: "qrc:/ui/PathPicker.qml"
        onLoaded: {
            item.directoryMode = root.pickerDirectory
            item.selected.connect(function(path) {
                if (root.pickerTarget === "ca") caPath.text = path
                else localPath.text = path
                pathPicker.active = false
            })
            item.open()
        }
    }
}

