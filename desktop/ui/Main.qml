import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1180; height: 780; minimumWidth: 900; minimumHeight: 650
    visible: true
    title: "CPNetFlux · Linux 桌面端"
    color: "#f3f5fa"
    font.family: "Noto Sans CJK SC"
    font.pixelSize: 14
    property int page: initialPage
    property string selectedId: ""
    property var selectedTask: {
        for (var i=0;i<rpc.tasks.length;i++) if(rpc.tasks[i].transfer_id === selectedId) return rpc.tasks[i]
        return rpc.tasks.length ? rpc.tasks[0] : null
    }
    property var pageTitles: ["传输任务", "新建传输", "任务详情", "节点设置", "远端目录", "历史与恢复", "高级设置"]
    function stateName(s) {
        return ({queued:"排队中", scanning:"扫描中", connecting:"连接中", negotiating:"协商中", transferring:"传输中", committing:"提交中", completed:"演示完成", failed:"演示失败", cancelled:"演示已取消", recoverable:"可恢复", paused:"已暂停"})[s] || "未知"
    }
    function value(v, unit) { return v === null || v === undefined ? "未知" : v + (unit || "") }
    function bytes(v) { return v === null || v === undefined ? "未知" : (v/1048576).toFixed(2)+" MiB" }
    function fraction(done,total) { return done === null || total === null || !total ? 0 : done/total }
    function terminal(s) { return ["completed","failed","cancelled","recoverable"].indexOf(s)>=0 }
    function selectTask(t) { selectedId=t.transfer_id; page=2 }
    function loadProfile(p) {
        nodeId.text=p.node_id; nodeName.text=p.name; host.text=p.host; port.text=p.control_port
        rootPath.text=p.default_root_id || "home"; ca.text=p.ca_path; serverName.text=p.server_name
        credentialRef.text=p.credential_ref || ""; auth.currentIndex=p.auth_mode==="token"?0:p.auth_mode==="admin_credential"?1:2
    }
    Connections { target: rpc; function onPageRequested(p) { window.page=p } }

    component Card: Rectangle {
        color: "white"; radius: 12; border.color: "#e2e7f0"
        implicitHeight: 100
    }
    component Muted: Label { color: "#657389"; wrapMode: Text.WordWrap }
    component Heading: Label { font.pixelSize: 21; font.bold: true; color: "#14233b" }

    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 195; color: "#15253d"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 12
                Label { text: "CPNetFlux"; color: "white"; font.pixelSize: 25; font.bold: true }
                Label { text: "Linux 数据传输"; color: "#a9bed9"; font.pixelSize: 12 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#31435c"; Layout.topMargin: 14 }
                Repeater {
                    model: ["任务首页", "新建传输", "任务详情", "节点设置", "远端目录", "历史 / 恢复", "高级设置"]
                    Button {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true; text: modelData; flat: true
                        highlighted: window.page===index
                        onClicked: window.page=index
                    }
                }
                Item { Layout.fillHeight: true }
                Label { Layout.fillWidth:true; text: "DESKTOP SHELL 01\n独立本地任务代理"; color:"#a9bed9"; font.pixelSize:11; wrapMode:Text.WordWrap }
            }
        }
        ColumnLayout {
            Layout.fillHeight:true; Layout.fillWidth:true; Layout.margins:28; spacing:16
            RowLayout {
                Layout.fillWidth:true
                Heading { text: window.pageTitles[window.page] }
                Item { Layout.fillWidth:true }
                Label { text: rpc.connected ? "● 本地 agent 已连接" : "● agent 离线 / 重连中"; color:rpc.connected?"#267a5a":"#a64b31" }
                Button { text:"刷新"; enabled:rpc.connected; onClicked:rpc.refresh() }
            }
            Rectangle {
                Layout.fillWidth:true; implicitHeight:64; radius:8; color:"#fff1d7"
                Label { anchors.fill:parent; anchors.margins:12; text:"演示数据 · 不读取、不发送任何真实文件。完成、失败、取消和恢复均为演示状态。\n真实传输 adapter、服务器探测与安全凭据尚未接入。"; color:"#83551e"; wrapMode:Text.WordWrap }
            }
            Muted { Layout.fillWidth:true; text:rpc.message; maximumLineCount:2; elide:Text.ElideRight }
            StackLayout {
                currentIndex: window.page; Layout.fillWidth:true; Layout.fillHeight:true
                // 0: tasks
                ScrollView {
                    clip:true
                    ColumnLayout {
                        width:parent.width; spacing:14
                        RowLayout {
                            Button { text:"＋ 新建演示上传 / 下载"; highlighted:true; onClicked:window.page=1 }
                            Label { text:rpc.tasks.length+" 个本地任务"; color:"#657389" }
                        }
                        Card {
                            visible:rpc.tasks.length===0; Layout.fillWidth:true; implicitHeight:160
                            ColumnLayout { anchors.fill:parent; anchors.margins:24
                                Heading { text:"开始体验一次目录传输" }
                                Muted { Layout.fillWidth:true; text:"创建演示任务后点击开始。关闭窗口再打开，agent 中的演示任务仍会继续。实际节点需要先部署受控服务端。" }
                                Button { text:"创建第一项演示任务"; enabled:rpc.connected && rpc.capabilities.adapter==="demo"; onClicked:rpc.demo() }
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
                                        Label { text:"演示数据"; color:"#a46a1b" }
                                        Button { text:"查看详情"; onClicked:window.selectTask(modelData) }
                                    }
                                    Muted { Layout.fillWidth:true; text:modelData.source.path+" → "+modelData.destination.path; elide:Text.ElideMiddle; maximumLineCount:1 }
                                    ProgressBar { Layout.fillWidth:true; value:window.fraction(modelData.metrics.bytes_committed,modelData.metrics.bytes_total); indeterminate:modelData.metrics.bytes_total===null && !window.terminal(modelData.state) }
                                    RowLayout { Layout.fillWidth:true
                                        Label { text:window.bytes(modelData.metrics.bytes_committed)+" / "+window.bytes(modelData.metrics.bytes_total); color:"#44556e" }
                                        Item { Layout.fillWidth:true }
                                        Label { text:"文件 "+window.value(modelData.metrics.files_committed)+" / "+window.value(modelData.metrics.files_total); color:"#44556e" }
                                        Label { text:"实际模式 "+(modelData.actual_mode==="unknown"?"未知":modelData.actual_mode.toUpperCase()+"（模拟）"); color:"#44556e" }
                                    }
                                }
                            }
                        }
                    }
                }
                // 1: create
                ScrollView { clip:true
                    ColumnLayout { width:parent.width; spacing:14
                        Card { Layout.fillWidth:true; implicitHeight:410
                            ColumnLayout { anchors.fill:parent; anchors.margins:24; spacing:12
                                Heading { text:"新建演示传输" }
                                Muted { Layout.fillWidth:true; text:"当前只连接演示节点。真实节点不会被访问，填写的路径不会被读取。" }
                                GridLayout { columns:2; Layout.fillWidth:true; columnSpacing:18; rowSpacing:12
                                    Label { text:"方向" }
                                    ComboBox { id:direction; objectName:"direction"; model:["本机 → 远端（上传）","远端 → 本机（下载）"]; Layout.fillWidth:true }
                                    Label { text:"远端节点" }
                                    ComboBox { model:["演示节点 · 不访问网络"]; Layout.fillWidth:true }
                                    Label { text:"本地目录" }
                                    TextField { id:localPath; objectName:"localPath"; text:"/home/student/data"; Layout.fillWidth:true; placeholderText:"Linux绝对路径" }
                                    Label { text:"远端相对目录" }
                                    TextField { id:remotePath; text:"dataset"; Layout.fillWidth:true }
                                    Label { text:"请求引擎" }
                                    ComboBox { id:mode; model:["自动选择","V1（模拟）","请求V2 · 展示V1回退"]; Layout.fillWidth:true }
                                    Label { text:"演示场景" }
                                    ComboBox { id:scenario; model:["正常完成","中途失败，可恢复"]; Layout.fillWidth:true }
                                }
                                Muted { Layout.fillWidth:true; text:"请求：校验 CRC32C、控制 TLS 验证、数据 TLS 必需。演示不代表这些能力已实现；真实执行前必须协商。V2 引擎与 V3 调度分别表示。" }
                                Button { objectName:"createButton"; text:"创建演示任务（排队）"; highlighted:true; enabled:rpc.connected && rpc.capabilities.adapter==="demo"
                                    onClicked:{ rpc.create(direction.currentIndex===0?"upload":"download",localPath.text,remotePath.text,scenario.currentIndex===1,["auto","v1","v2"][mode.currentIndex]); window.page=0 }
                                }
                            }
                        }
                    }
                }
                // 2: detail
                ScrollView { clip:true
                    ColumnLayout { width:parent.width; spacing:14
                        Muted { visible:!window.selectedTask; text:"尚无任务，请先创建演示传输。" }
                        Card { visible:!!window.selectedTask; Layout.fillWidth:true; implicitHeight:480
                            ColumnLayout { anchors.fill:parent; anchors.margins:22; spacing:12
                                Heading { text:window.selectedTask?window.stateName(window.selectedTask.state):"无任务" }
                                Muted { Layout.fillWidth:true; text:window.selectedTask ? (window.selectedTask.direction==="upload"?"上传":"下载")+" · "+window.selectedTask.source.path+" → "+window.selectedTask.destination.path : "" }
                                Label { text:window.selectedTask?"请求引擎 "+window.selectedTask.policy.requested_mode+" / 实际引擎 "+window.selectedTask.actual_mode+"（模拟）":""; color:"#274970" }
                                Muted { Layout.fillWidth:true; text:window.selectedTask?"V3 调度请求 "+window.selectedTask.policy.scheduler+" / 实际 "+window.selectedTask.actual_scheduler+"；range策略 "+window.selectedTask.actual_range_policy:"" }
                                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:"#a56b20"; text:window.selectedTask && window.selectedTask.fallback_reason ? "回退原因："+window.selectedTask.fallback_reason.message : "回退原因：尚无 / 未协商" }
                                Label { text:window.selectedTask?"文件已提交 "+window.value(window.selectedTask.metrics.files_committed)+" / "+window.value(window.selectedTask.metrics.files_total):"" }
                                ProgressBar { Layout.fillWidth:true; value:window.selectedTask?window.fraction(window.selectedTask.metrics.files_committed,window.selectedTask.metrics.files_total):0 }
                                Label { text:window.selectedTask?"字节已传输 "+window.bytes(window.selectedTask.metrics.bytes_transferred)+" · 已提交 "+window.bytes(window.selectedTask.metrics.bytes_committed)+" / "+window.bytes(window.selectedTask.metrics.bytes_total):"" }
                                ProgressBar { Layout.fillWidth:true; value:window.selectedTask?window.fraction(window.selectedTask.metrics.bytes_committed,window.selectedTask.metrics.bytes_total):0 }
                                Muted { text:window.selectedTask?"实时速度 "+window.value(window.selectedTask.metrics.speed_bps," B/s")+" · 平均 "+window.value(window.selectedTask.metrics.average_speed_bps," B/s")+" · ETA "+window.value(window.selectedTask.metrics.eta_seconds," 秒"):"" }
                                Muted { text:window.selectedTask?"活动通道 "+window.value(window.selectedTask.metrics.channel_count_active)+" · 数据连接 "+window.value(window.selectedTask.metrics.data_connections)+" · 队列 "+window.value(window.selectedTask.metrics.queue_depth_current)+" · 阶段耗时 未知":"" }
                                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:"#b34236"; text:window.selectedTask && window.selectedTask.error ? window.selectedTask.error.message : ""; visible:text.length>0 }
                                Muted { text:window.selectedTask?"恢复资格："+({unknown:"未知",eligible:"可恢复（演示）",ineligible:"不可恢复"})[window.selectedTask.recovery.eligibility]+" · attempt "+window.selectedTask.attempt.attempt_number:"" }
                                RowLayout {
                                    Button { objectName:"startButton"; text:"开始演示"; highlighted:true; enabled:rpc.connected && window.selectedTask && window.selectedTask.state==="queued"; onClicked:rpc.action("transfer.start",window.selectedTask) }
                                    Button { text:"取消演示"; enabled:rpc.connected && window.selectedTask && !window.terminal(window.selectedTask.state); onClicked:rpc.action("transfer.cancel",window.selectedTask) }
                                    Button { text:"暂停 · 不支持"; enabled:rpc.connected && !!rpc.capabilities.pause_supported && window.selectedTask && !window.terminal(window.selectedTask.state); onClicked:rpc.action("transfer.pause",window.selectedTask) }
                                    Button { text:"恢复演示"; enabled:rpc.connected && !!rpc.capabilities.resume_supported && window.selectedTask && window.selectedTask.recovery.eligibility==="eligible" && window.terminal(window.selectedTask.state); onClicked:rpc.action("transfer.resume",window.selectedTask) }
                                }
                            }
                        }
                    }
                }
                // 3: profiles
                ScrollView { clip:true
                    ColumnLayout { width:parent.width; spacing:14
                        Card { Layout.fillWidth:true; implicitHeight:580
                            ColumnLayout { anchors.fill:parent; anchors.margins:22; spacing:10
                                Heading { text:"节点连接配置" }
                                Muted { Layout.fillWidth:true; text:"接收节点必须先运行受控CPNetFlux服务端。安装两个GUI不等于能互传。当前只保存配置和凭据引用，不接收密码或token值。" }
                                ComboBox { Layout.fillWidth:true; model:rpc.profiles; textRole:"name"; displayText:currentIndex>=0 && currentIndex<rpc.profiles.length ? rpc.profiles[currentIndex].name : "已保存节点（选择以编辑）"; onActivated:window.loadProfile(rpc.profiles[currentIndex]) }
                                GridLayout { columns:4; Layout.fillWidth:true; columnSpacing:12; rowSpacing:8
                                    Label { text:"节点ID" } TextField { id:nodeId; text:"lab-node"; Layout.fillWidth:true }
                                    Label { text:"名称" } TextField { id:nodeName; text:"实验室节点"; Layout.fillWidth:true }
                                    Label { text:"地址" } TextField { id:host; text:"node.example"; Layout.fillWidth:true }
                                    Label { text:"控制端口" } TextField { id:port; text:"22310"; validator:IntValidator { bottom:1; top:65535 } Layout.fillWidth:true }
                                    Label { text:"默认root" } TextField { id:rootPath; text:"home"; Layout.fillWidth:true }
                                    Label { text:"TLS主机名" } TextField { id:serverName; text:host.text; Layout.fillWidth:true }
                                    Label { text:"CA文件" } TextField { id:ca; text:"/home/student/.config/cpnetflux/ca.pem"; Layout.fillWidth:true; Layout.columnSpan:3 }
                                    Label { text:"身份方式" } ComboBox { id:auth; model:["短期token引用","管理员凭据引用","anonymous（仅测试）"]; Layout.fillWidth:true }
                                    Label { text:"凭据引用" } TextField { id:credentialRef; text:"secret-service:cpnetflux/lab-node"; placeholderText:"仅引用，禁止填写凭据值"; Layout.fillWidth:true }
                                }
                                RowLayout {
                                    Button { text:"保存配置"; enabled:rpc.connected && port.acceptableInput; onClicked:{
                                        var update=false; for(var i=0;i<rpc.profiles.length;i++) if(rpc.profiles[i].node_id===nodeId.text) update=true
                                        rpc.saveProfile({node_id:nodeId.text,name:nodeName.text,host:host.text,control_port:parseInt(port.text),server_name:serverName.text,ca_path:ca.text,auth_mode:["token","admin_credential","anonymous"][auth.currentIndex],credential_ref:auth.currentIndex===2?null:credentialRef.text,default_root_id:rootPath.text},update)
                                    } }
                                    Button { text:"分步连接检查"; enabled:rpc.connected; onClicked:rpc.testNode(nodeId.text,rootPath.text) }
                                }
                                Repeater { model:[{key:"dns_control_port",name:"① DNS / 控制端口"},{key:"tls_identity",name:"② TLS服务端身份"},{key:"login",name:"③ 登录"},{key:"capabilities",name:"④ 能力协商"},{key:"directory_permission",name:"⑤ 目录读写授权"},{key:"data_port",name:"协商数据端口"}]
                                    Label { required property var modelData; text:modelData.name+"："+(rpc.checks[modelData.key]?"未接入（unsupported）":"尚未检查 / 未接入"); color:"#657389" }
                                }
                            }
                        }
                    }
                }
                // 4: directory
                Card { ColumnLayout { anchors.fill:parent; anchors.margins:24
                    Heading { text:"远端目录浏览" }
                    Muted { Layout.fillWidth:true; text:"真实目录 adapter 尚未接入。后续将在经验证TLS、登录和root授权后展示相对目录；不会扫描或暴露服务器宿主路径。" }
                    TextField { placeholderText:"root-relative 路径"; Layout.fillWidth:true; enabled:false }
                    Button { text:"浏览 · 未接入"; enabled:false }
                    Item { Layout.fillHeight:true }
                } }
                // 5: history
                ScrollView { clip:true
                    ColumnLayout { width:parent.width; spacing:12
                        Muted { Layout.fillWidth:true; text:"历史由agent持久保存。agent重启后运行任务标记可恢复，需手动恢复；演示checkpoint不代表真实文件已校验。" }
                        Repeater { model:rpc.tasks
                            Card { required property var modelData; Layout.fillWidth:true; implicitHeight:95; visible:window.terminal(modelData.state)
                                RowLayout { anchors.fill:parent; anchors.margins:18
                                    ColumnLayout { Layout.fillWidth:true
                                        Label { text:window.stateName(modelData.state)+" · 演示数据"; font.bold:true }
                                        Muted { text:modelData.source.path; elide:Text.ElideMiddle; Layout.fillWidth:true }
                                    }
                                    Label { text:modelData.recovery.eligibility==="eligible"?"可恢复":"不可恢复"; color:"#657389" }
                                    Button { text:"查看"; onClicked:window.selectTask(modelData) }
                                }
                            }
                        }
                    }
                }
                // 6: advanced
                Card { ColumnLayout { anchors.fill:parent; anchors.margins:24; spacing:16
                    Heading { text:"引擎与调度能力" }
                    Muted { Layout.fillWidth:true; text:"V2负责传输事务；V3负责有界动态环状队列及自适应range调度。此切片不模拟V3范围调度，不宣称V2可靠性可用。" }
                    Label { text:"演示支持：V1生命周期 / 文件级策略 / 单模拟通道" }
                    Label { text:"V2 checksum / resume / data TLS：未接入"; color:"#a46a1b" }
                    Label { text:"V3动态队列 / range striping：未接入"; color:"#a46a1b" }
                    ComboBox { model:["通道自动选择（请求）"]; enabled:false }
                    ComboBox { model:["range policy 自动（请求）"]; enabled:false }
                    Muted { Layout.fillWidth:true; text:"速度、ETA和阶段耗时未观测时显示未知。暂停不支持；恢复按钮只用于agent实际实现的demo checkpoint。真实安全策略须由权威底层协商后回读。" }
                    Item { Layout.fillHeight:true }
                } }
            }
        }
    }
}
