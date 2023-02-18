#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: M17 data streamer
# Author: SP5WWP
# Copyright: M17 Project, Dec 2022
# GNU Radio version: 3.10.5.1

from packaging.version import Version as StrictVersion

if __name__ == '__main__':
    import ctypes
    import sys
    if sys.platform.startswith('linux'):
        try:
            x11 = ctypes.cdll.LoadLibrary('libX11.so')
            x11.XInitThreads()
        except:
            print("Warning: failed to XInitThreads()")

from gnuradio import blocks
from gnuradio import gr
from gnuradio.filter import firdes
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import m17



from gnuradio import qtgui

class m17_streamer(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "M17 data streamer", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("M17 data streamer")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except:
            pass
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("GNU Radio", "m17_streamer")

        try:
            if StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
                self.restoreGeometry(self.settings.value("geometry").toByteArray())
            else:
                self.restoreGeometry(self.settings.value("geometry"))
        except:
            pass

        ##################################################
        # Blocks
        ##################################################

        self.m17_m17_decoder_0 = m17.m17_decoder()
        self.m17_m17_coder_0 = m17.m17_coder()
        self.blocks_vector_source_x_0_2_0 = blocks.vector_source_b((0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), True, 1, [])
        self.blocks_vector_source_x_0_2 = blocks.vector_source_b((0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), True, 1, [])
        self.blocks_vector_source_x_0_1 = blocks.vector_source_b((0x00, 0x05), True, 1, [])
        self.blocks_vector_source_x_0_0 = blocks.vector_source_b((0x00, 0x00, 0x1F, 0x24, 0x5D, 0x51), True, 1, [])
        self.blocks_vector_source_x_0 = blocks.vector_source_b((0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF), True, 1, [])
        self.blocks_throttle_0 = blocks.throttle(gr.sizeof_char*1, ((6+6+2+14+16)*8*(1000/40)),True)
        self.blocks_stream_mux_0 = blocks.stream_mux(gr.sizeof_char*1, (6, 6, 2, 14, 16))
        self.blocks_null_sink_0 = blocks.null_sink(gr.sizeof_char*1)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.blocks_stream_mux_0, 0), (self.blocks_throttle_0, 0))
        self.connect((self.blocks_throttle_0, 0), (self.m17_m17_coder_0, 0))
        self.connect((self.blocks_vector_source_x_0, 0), (self.blocks_stream_mux_0, 0))
        self.connect((self.blocks_vector_source_x_0_0, 0), (self.blocks_stream_mux_0, 1))
        self.connect((self.blocks_vector_source_x_0_1, 0), (self.blocks_stream_mux_0, 2))
        self.connect((self.blocks_vector_source_x_0_2, 0), (self.blocks_stream_mux_0, 4))
        self.connect((self.blocks_vector_source_x_0_2_0, 0), (self.blocks_stream_mux_0, 3))
        self.connect((self.m17_m17_coder_0, 0), (self.m17_m17_decoder_0, 0))
        self.connect((self.m17_m17_decoder_0, 0), (self.blocks_null_sink_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("GNU Radio", "m17_streamer")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()




def main(top_block_cls=m17_streamer, options=None):

    if StrictVersion("4.5.0") <= StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
        style = gr.prefs().get_string('qtgui', 'style', 'raster')
        Qt.QApplication.setGraphicsSystem(style)
    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
