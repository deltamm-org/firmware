// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DeltaMMSim

ApplicationWindow {
    id: window
    width: 1000
    height: 700
    visible: true
    title: qsTr("DeltaMM simulator")
    color: "#1e1f22"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // The radio: status LEDs above a 160 x 128 display shown at 2x.
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 12

            Row {
                spacing: 12
                Repeater {
                    model: [
                        { name: qsTr("Red"), lit: SimBridge.redOn, color: "#ff3b30" },
                        { name: qsTr("Green"), lit: SimBridge.greenOn, color: "#34c759" },
                        { name: qsTr("Blue"), lit: SimBridge.blueOn, color: "#0a84ff" }
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 6
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            color: modelData.lit ? modelData.color : "#3a3b3f"
                            border.color: "#55575c"
                        }
                        Label {
                            text: modelData.name
                            color: "#c8c9cc"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }

            Rectangle {
                width: 324
                height: 260
                color: "#000000"
                border.color: "#55575c"
                SimScreen {
                    anchors.centerIn: parent
                    width: 320
                    height: 256
                }
                // The mouse wheel over the screen turns the channel knob.
                WheelHandler {
                    onWheel: (event) => SimBridge.turnKnob(event.angleDelta.y > 0 ? 1 : -1)
                }
            }

            // Keys in panel order; each is down while the mouse holds it.
            Grid {
                columns: 6
                spacing: 4
                Repeater {
                    // Indexes into Key: PTT, Side1-3, Front1-2, Menu, Up, Down,
                    // Exit, *, #, 1-9, 0.
                    model: [21, 18, 19, 20, 16, 17, 12, 14, 15, 13, 10, 11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0]
                    delegate: Button {
                        required property int modelData
                        width: 50
                        height: 28
                        text: SimBridge.keyNames[modelData]
                        onPressedChanged: SimBridge.setKey(modelData, pressed)
                    }
                }
            }

            RowLayout {
                spacing: 8
                Button { text: qsTr("Knob ◀"); onClicked: SimBridge.turnKnob(-1) }
                Button { text: qsTr("Knob ▶"); onClicked: SimBridge.turnKnob(1) }
                Label { text: qsTr("Volume"); color: "#c8c9cc" }
                Slider {
                    from: 0
                    to: 1000
                    value: 500
                    Layout.preferredWidth: 100
                    onMoved: SimBridge.setVolume(value)
                }
            }

            RowLayout {
                spacing: 8
                Label { text: qsTr("Battery"); color: "#c8c9cc" }
                Slider {
                    id: batterySlider
                    from: 5600
                    to: 8400
                    value: 7800
                    stepSize: 10
                    Layout.preferredWidth: 180
                    onMoved: SimBridge.setBatteryMillivolts(value)
                }
                Label {
                    text: (batterySlider.value / 1000).toFixed(2) + " V"
                    color: "#c8c9cc"
                }
            }
        }

        // Log console and the debug console input below it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#111214"
                border.color: "#3a3b3f"

                ListView {
                    id: logView
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    model: ListModel { id: logModel }
                    delegate: Text {
                        required property string line
                        text: line
                        color: "#d4d4d4"
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }

            // Debug console input, as typed on the radio's log port.
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Console command (help, light 0-5, pattern, logview, ...)")
                onAccepted: {
                    SimBridge.sendCommand(text)
                    text = ""
                }
            }
        }
    }

    Connections {
        target: SimBridge
        function onLogLine(line) {
            logModel.append({ line: line })
            if (logModel.count > 2000)
                logModel.remove(0)
            logView.positionViewAtEnd()
        }
    }
}
