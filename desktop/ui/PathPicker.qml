import QtQuick
import QtQuick.Dialogs

Item {
    id: root
    property bool directoryMode: false
    signal selected(string path)
    function open() { if (directoryMode) folder.open(); else file.open() }
    FileDialog {
        id: file
        title: "选择本地文件"
        fileMode: FileDialog.OpenFile
        onAccepted: root.selected(selectedFile.toString().replace(/^file:\/\//, ""))
    }
    FolderDialog {
        id: folder
        title: "选择本地目录"
        onAccepted: root.selected(selectedFolder.toString().replace(/^file:\/\//, ""))
    }
}
