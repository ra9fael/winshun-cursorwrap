pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import WinShun

Window {
    id: window

    required property QtObject tuning
    required property QtObject look
    required property WindowFrame windowFrame
    readonly property var values: tuning.snapshot
    readonly property bool chinese: values.chinese
    readonly property var colors: look.colors

    width: 500
    height: 744
    minimumWidth: 456
    minimumHeight: 380
    title: chinese ? "回绕调参" : "Cursor wrap tuning"
    flags: Qt.Window | Qt.FramelessWindowHint
    // Inactive Mica falls back to grey: the window covers it, as the host's own do.
    color: tuning.backdrop && window.active ? "transparent" : colors.background

    TitleBar {
        id: titleBar
        z: 1
        width: parent.width
        frame: window.windowFrame
        title: window.title

        // Where Windows puts an app's icon: the glyph this plugin gives
        // WinShun in plugin.json, the one its settings page shows.
        Glyph {
            glyph: "\uE8DE"
            size: 16
            color: titleBar.ink
            x: titleBar.iconX
            y: Math.round((titleBar.height - size) / 2)
        }
    }

    function tr(zh: string, en: string): string {
        return chinese ? zh : en
    }

    component Caption: Label {
        color: window.colors.subtext
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    component TuningButton: FlatButton {
        implicitHeight: 30
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.onPressAction: clicked()
    }

    component Group: ColumnLayout {
        id: group
        required property string heading
        default property alias rows: stack.data
        spacing: 6

        Label {
            text: group.heading
            color: window.colors.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            Layout.leftMargin: 2
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: stack.implicitHeight + 2
            color: window.colors.layer
            border.color: window.colors.divider
            radius: window.look.cardRadius

            ColumnLayout {
                id: stack
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
            }
        }
    }

    component Divider: Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        implicitHeight: 1
        color: window.colors.divider
    }

    component NumberRow: ColumnLayout {
        id: row

        required property string settingKey
        required property string caption
        required property string description
        required property string unit
        property bool acceleration: false
        property bool preparingAcceleration: false
        readonly property var setting: window.values[settingKey]
        readonly property int saved: setting.value
        readonly property int minimum: setting.minimum
        readonly property bool off: acceleration && saved === 0 && !preparingAcceleration
        property bool applied: false
        property bool invalid: false

        Layout.fillWidth: true
        spacing: 0

        function apply() {
            invalid = !window.tuning.applyNumber(settingKey, entry.text)
            if (!invalid) {
                preparingAcceleration = false
                entry.text = String(saved)
                applied = true
                feedback.restart()
            }
        }
        onSavedChanged: {
            entry.text = String(saved)
            preparingAcceleration = false
            invalid = false
        }
        Component.onCompleted: entry.text = String(saved)
        Connections {
            target: window
            function onVisibleChanged() {
                if (!window.visible) {
                    row.preparingAcceleration = false
                    entry.text = String(row.saved)
                    row.invalid = false
                    row.applied = false
                }
            }
        }
        Timer {
            id: feedback
            interval: 1200
            onTriggered: row.applied = false
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.bottomMargin: row.acceleration || row.invalid ? 6 : 12
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: row.caption
                    color: window.colors.text
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
                Caption {
                    Layout.fillWidth: true
                    text: row.description
                }
            }

            InputBox {
                id: entry
                objectName: row.settingKey + "Input"
                Layout.preferredWidth: 76
                Layout.preferredHeight: 30
                enabled: !row.off
                horizontalAlignment: TextInput.AlignRight
                maximumLength: 9
                Accessible.role: Accessible.EditableText
                Accessible.name: row.caption
                error: row.invalid
                onAccepted: row.apply()
                onTextChanged: {
                    row.invalid = false
                    row.applied = false
                }
            }

            Caption {
                text: row.unit
                Layout.preferredWidth: 34
            }

            TuningButton {
                objectName: row.settingKey + "Apply"
                Layout.preferredWidth: 78
                text: row.applied ? window.tr("已应用", "Applied") : window.tr("应用", "Apply")
                enabled: !row.off
                onClicked: row.apply()
            }
        }

        CheckBox {
            id: disable
            objectName: row.acceleration ? "accelerationOff" : ""
            visible: row.acceleration
            Layout.leftMargin: 12
            Layout.bottomMargin: 6
            text: window.tr("关闭加速度条件", "Disable acceleration requirement")
            checked: row.off
            onToggled: {
                if (checked) {
                    row.preparingAcceleration = false
                    window.tuning.disableAcceleration()
                } else {
                    row.preparingAcceleration = true
                    entry.text = ""
                    entry.takeFocus()
                }
            }
            contentItem: Label {
                text: disable.text
                color: window.colors.subtext
                leftPadding: disable.indicator.width + disable.spacing
                verticalAlignment: Text.AlignVCenter
            }
        }

        Caption {
            objectName: row.settingKey + "Error"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            visible: row.invalid
            color: window.colors.danger
            text: window.tr("请输入 %1–%2 范围内的整数。", "Enter a whole number from %1 to %2.")
                .arg(row.minimum).arg(row.setting.maximum)
        }
    }

    component SwitchRow: RowLayout {
        id: row
        required property string settingKey
        required property string caption
        required property string description

        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: row.caption
                color: window.colors.text
                font.pixelSize: 14
            }
            Caption {
                Layout.fillWidth: true
                text: row.description
            }
        }
        ToggleSwitch {
            objectName: row.settingKey
            Layout.rightMargin: 12
            checked: window.values[row.settingKey]
            word: false
            Accessible.role: Accessible.CheckBox
            Accessible.name: row.caption
            Accessible.checked: checked
            Accessible.onToggleAction: toggled(!checked)
            onToggled: on => window.tuning.setSwitch(row.settingKey, on)
        }
    }

    ScrollView {
        id: scroll
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 14
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                implicitHeight: hostSummary.implicitHeight + 20
                color: window.colors.layer
                border.color: window.colors.divider
                radius: window.look.cardRadius

                Caption {
                    id: hostSummary
                    anchors.fill: parent
                    anchors.margins: 10
                    text: window.values.summary
                }
            }

            Group {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                heading: window.tr("测量", "Measurement")

                NumberRow {
                    settingKey: "impactSpeedPps"
                    caption: window.tr("撞击速度", "Impact speed")
                    description: window.tr("到达边缘时的速度门槛", "Speed required at the edge")
                    unit: "px/s"
                }
                Divider {}
                NumberRow {
                    settingKey: "impactBandPx"
                    caption: window.tr("测量带宽", "Measuring band")
                    description: window.tr("只用这段边缘区间内的样本测速", "Measure speed within this edge strip")
                    unit: "px"
                }
            }

            Group {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                heading: window.tr("门槛与判定", "Edge detection")

                NumberRow {
                    settingKey: "wrapGuardPx"
                    caption: window.tr("防震半径", "Wrap guard")
                    description: window.tr("抑制落点附近的重复回绕", "Suppress repeated wraps near the landing point")
                    unit: "px"
                }
                Divider {}
                NumberRow {
                    settingKey: "edgeThresholdPx"
                    caption: window.tr("边缘阈值", "Edge threshold")
                    description: window.tr("判定到达外边缘的距离", "Distance at which the cursor reaches the edge")
                    unit: "px"
                }
                Divider {}
                NumberRow {
                    settingKey: "adjacencyTolerancePx"
                    caption: window.tr("邻接容差", "Adjacency tolerance")
                    description: window.tr("判定两块屏幕相邻的容差", "Tolerance for detecting adjacent displays")
                    unit: "px"
                }
                Divider {}
                NumberRow {
                    settingKey: "taskbarGuardPx"
                    caption: window.tr("任务栏保护带宽", "Taskbar guard band")
                    description: window.tr("任务栏及其周围这段距离内不回绕；0 = 仅任务栏本身", "No wrap over the taskbar and this far beside it; 0 = the taskbar only")
                    unit: "px"
                }
            }

            Group {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                heading: window.tr("加速度", "Acceleration")

                NumberRow {
                    settingKey: "impactAccelPps2"
                    acceleration: true
                    caption: window.tr("撞击加速度", "Impact acceleration")
                    description: window.tr("撞击模式下，要求光标到边时仍在加速", "Require acceleration at the edge in impact mode")
                    unit: "px/s²"
                }
            }

            Group {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                heading: window.tr("可视化", "Visualization")

                SwitchRow {
                    settingKey: "tuningOverlay"
                    caption: window.tr("调参可视化", "Tuning overlay")
                    description: window.tr("显示边缘测量带和速度读数", "Show the measuring band and speed at the edge")
                }
                Divider {}
                SwitchRow {
                    settingKey: "impactFlash"
                    caption: window.tr("撞击光效", "Impact glow")
                    description: window.tr("撞击回绕成功时点亮边缘", "Light the edge after an impact wrap")
                }
                Divider {}
                SwitchRow {
                    settingKey: "dataWindow"
                    caption: window.tr("数据窗口", "Data window")
                    description: window.tr("查看到达记录、拒绝原因和钩子健康", "Inspect arrivals, skip reasons and hook health")
                }
            }

            Group {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 14
                heading: window.tr("建议门槛", "Suggested gate")

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8

                    Caption {
                        Layout.fillWidth: true
                        text: window.values.adviceFits
                            ? window.tr("当前门槛已能区分慢移和快甩。", "The current gate separates slow drifts from fast flicks.")
                            : window.values.advicePps > 0
                                ? window.tr("%1 px/s · 基于 %2 条到达记录", "%1 px/s · Based on %2 arrivals")
                                    .arg(window.values.advicePps).arg(window.values.arrivals)
                                : window.tr("向边缘快甩几次，再慢移几次，以获得建议。",
                                    "Flick to an edge a few times, then drift to it to get a suggestion.")
                    }
                    RowLayout {
                        spacing: 8
                        TuningButton {
                            objectName: "adoptAdvice"
                            highlighted: true
                            enabled: window.values.advicePps > 0 && !window.values.adviceFits
                            text: window.tr("采用建议值", "Use suggestion")
                            onClicked: window.tuning.applyAdvice()
                        }
                        TuningButton {
                            objectName: "resetTuning"
                            text: window.tr("恢复默认调参", "Reset tuning")
                            onClicked: window.tuning.resetDefaults()
                        }
                    }
                }
            }
        }
    }
}
