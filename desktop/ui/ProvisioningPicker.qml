import QtQuick
import QtQuick.Dialogs

Item {
    signal selected(string path)
    function open() { file.open() }
    FileDialog {
        id: file
        title: "导入 CPNetFlux 节点包"
        fileMode: FileDialog.OpenFile
        nameFilters: ["CPNetFlux 节点包 (*.cpnetflux *.json)", "所有文件 (*)"]
        onAccepted: selected(selectedFile.toString())
    }
}
