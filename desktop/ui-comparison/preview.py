"""Run the actual QML with an isolated, visibly synthetic design facade."""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=["original", "a", "b"], default="original")
    parser.add_argument("--scenario", choices=["tasks", "scanning", "completed", "failed", "offline", "empty"], default="tasks")
    parser.add_argument("--page", type=int, choices=range(7), default=0)
    parser.add_argument("--create-step", type=int, choices=range(3), default=0)
    parser.add_argument("--width", type=int, default=1366)
    parser.add_argument("--height", type=int, default=768)
    parser.add_argument("--scale", choices=["1", "1.25", "1.5"], default="1")
    parser.add_argument("--smoke-ms", type=int, default=0)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--offscreen", action="store_true")
    args = parser.parse_args()
    qml = HERE.parent / "ui/Main.qml" if args.variant == "original" else HERE / "candidates" / args.variant / "Main.qml"
    if not qml.is_file():
        parser.error(f"候选 {args.variant} 尚未生成；不会自动使用其他方案：{qml}")
    os.environ["QT_SCALE_FACTOR"] = args.scale
    os.environ["QT_QUICK_CONTROLS_STYLE"] = "Fusion"
    os.environ["QT_QUICK_BACKEND"] = "software"
    if args.offscreen:
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
    from PySide6.QtCore import QTimer, QUrl, qInstallMessageHandler, qVersion
    from PySide6.QtGui import QGuiApplication, QFontDatabase
    from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent
    from PySide6.QtQuick import QQuickItem
    import shiboken6
    from fixture_rpc import FixtureRpc

    errors = []

    def message_handler(kind, context, message):
        print(message, file=sys.stderr)
        if any(x in message for x in ("TypeError", "ReferenceError", "Cannot assign", "Binding loop", "is not installed", "failed to load")):
            errors.append(message)

    qInstallMessageHandler(message_handler)
    app = QGuiApplication([sys.argv[0]])
    app.setApplicationName("CPNetFlux Design Preview")
    if sys.platform == "win32":
        for name in ("msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "seguisym.ttf", "bahnschrift.ttf", "consola.ttf"):
            font_path = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / name
            if font_path.is_file():
                QFontDatabase.addApplicationFont(str(font_path))
    rpc = FixtureRpc(args.scenario)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("rpc", rpc)
    engine.rootContext().setContextProperty("initialPage", args.page)
    engine.rootContext().setContextProperty("designPreview", True)
    engine.load(QUrl.fromLocalFile(str(qml)))
    if not engine.rootObjects():
        return 2
    window = engine.rootObjects()[0]
    window.setWidth(args.width)
    window.setHeight(args.height)
    if args.variant == "b":
        window.setProperty("createStep", args.create_step)
    window.setTitle("[设计预览 · 未连接真实 agent] CPNetFlux · " + args.variant)
    # Add the same unmistakable watermark without changing the original QML.
    component = QQmlComponent(engine)
    component.setData(b'''import QtQuick
Rectangle { width: parent ? parent.width : 0; height: 26; y: parent ? parent.height-height : 0; z: 10000; color: "#7c2d12"
Text { anchors.centerIn: parent; text: "DESIGN PREVIEW | synthetic fixture | no real agent / transfer"; color: "white"; font.pixelSize: 12 }
}''', QUrl())
    watermark = component.create()
    if watermark is None:
        raise RuntimeError(str(component.errors()))
    watermark.setParentItem(window.contentItem())
    watermark.setParent(window)

    def capture():
        if args.screenshot:
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            image = window.grabWindow()
            if image.isNull() or not image.save(str(args.screenshot)):
                errors.append("screenshot capture failed")
        print(json.dumps(dict(variant=args.variant, scenario=args.scenario, page=args.page,
            create_step=args.create_step if args.variant == "b" else 0,
            window=[window.width(), window.height()], scale=args.scale, qt=qVersion(),
            real_agent_connected=rpc.connected, fixture_tasks=len(rpc.tasks), qml_errors=errors), ensure_ascii=False))
        app.exit(1 if errors else 0)

    if args.smoke_ms:
        QTimer.singleShot(args.smoke_ms, capture)
    result = app.exec()
    # Explicitly destroy QML before its context facade to avoid shutdown warnings.
    shiboken6.delete(engine)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
