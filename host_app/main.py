#!/usr/bin/env python3
"""SLECG host application entry point."""

import logging
import sys

from PyQt6.QtWidgets import QApplication

from slecg_host.logging_config import setup_logging
from slecg_host.ui.main_window import MainWindow
from slecg_host.ui.theme import DARK_STYLE

logger = logging.getLogger(__name__)

_DARK_MONITOR_STYLE = DARK_STYLE


def main() -> int:
    log_path = setup_logging(logging.INFO)
    logger.info("SLECG Host 启动 (log=%s)", log_path)
    app = QApplication(sys.argv)
    app.setApplicationName("SLECG Host")
    app.setStyleSheet(_DARK_MONITOR_STYLE)
    window = MainWindow()
    window.show()
    logger.info("主窗口已显示，进入事件循环")
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
