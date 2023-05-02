import sys

from PySide6.QtCore import Qt, QSize
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDateEdit,
    QDateTimeEdit,
    QDial,
    QDoubleSpinBox,
    QFontComboBox,
    QLabel,
    QLCDNumber,
    QLineEdit,
    QMainWindow,
    QProgressBar,
    QPushButton,
    QRadioButton,
    QSlider,
    QSpinBox,
    QTimeEdit,
    QVBoxLayout,
    QWidget,
)
from PySide6.QtGui import QWindow
from PySide6.QtQuick import QQuickWindow

from compushady import Swapchain, Texture2D, Compute
from compushady.formats import B8G8R8A8_UNORM
from compushady.shaders import hlsl

from threading import Thread


def renderer(swapchain):
    target = Texture2D(int(512 * 1.25), int(512 * 1.25), B8G8R8A8_UNORM)
    shader = hlsl.compile(
        """

RWTexture2D<float4> target : register(u0);

[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    float4 color = float4(1, 0, 0, 1);
    target[tid.xy] = color;
}
"""
    )

    compute = Compute(shader, uav=[target])
    while True:
        compute.dispatch(target.width // 8, target.height // 8, 1)
        swapchain.present(target)


# Subclass QMainWindow to customize your application's main window
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("Widgets App")

        layout = QVBoxLayout()
        widgets = [
            QCheckBox,
            QComboBox,
            QDateEdit,
            QDateTimeEdit,
            QDial,
            QDoubleSpinBox,
            QFontComboBox,
            QLCDNumber,
            QLabel,
            QLineEdit,
            QProgressBar,
            QPushButton,
            QRadioButton,
            QSlider,
            QSpinBox,
            QTimeEdit,
        ]

        for w in widgets:
            layout.addWidget(w())

        compushady_widget = QWidget.createWindowContainer(QWindow())
        compushady_widget.setMinimumSize(QSize(512, 512))
        compushady_widget.setMaximumSize(QSize(512, 512))

        layout.addWidget(compushady_widget)

        self.swapchain = Swapchain(compushady_widget.winId(), format=B8G8R8A8_UNORM)

        self.renderer = Thread(target=renderer, args=(self.swapchain,), daemon=True)
        self.renderer.start()

        widget = QWidget()
        widget.setLayout(layout)

        # Set the central widget of the Window. Widget will expand
        # to take up all the space in the window by default.
        self.setCentralWidget(widget)


app = QApplication(sys.argv)

print(app.screens()[0].devicePixelRatio())

window = MainWindow()
window.show()

app.exec()
