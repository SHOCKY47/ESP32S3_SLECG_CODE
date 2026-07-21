"""Application-wide light and black-green monitor themes."""

DARK_STYLE = """
QWidget { background-color: #07100c; color: #eafff0; font-family: "Inter", "SF Pro Display", "PingFang SC", sans-serif; font-size: 13px; }
QMainWindow, QStatusBar { background-color: #030806; }
QLabel { background-color: transparent; }
QFrame#appHeader { background-color: #0a1711; border: 1px solid #245c39; border-radius: 10px; }
QLabel#appTitle { color: #ffffff; font-size: 22px; font-weight: 700; letter-spacing: 1px; }
QLabel#appSubtitle { color: #75b58a; font-size: 11px; }
QLabel#systemBadge { background-color: #09261a; color: #35f29a; border: 1px solid #17664d; border-radius: 11px; padding: 5px 11px; font-weight: 650; }
QLabel#heartRateBadge { background-color: #0d2a1b; color: #ffffff; border: 1px solid #2f8250; border-radius: 11px; padding: 5px 12px; font-size: 15px; font-weight: 750; }
QSplitter#plotSplitter::handle { background-color: #173b26; height: 5px; }
QGroupBox { background-color: #0a1711; border: 1px solid #245c39; border-radius: 9px; margin-top: 11px; padding: 10px 10px 8px 10px; font-weight: 650; color: #9ce8b5; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 7px; }
QPushButton, QComboBox { background-color: #0c1d14; border: 1px solid #2f8250; border-radius: 6px; padding: 7px 14px; color: #f4fff7; }
QPushButton:hover { background-color: #123822; border-color: #00e676; }
QPushButton:pressed { background-color: #00a846; color: #001c0b; }
QPushButton:disabled { color: #466451; border-color: #203d2a; background-color: #09140e; }
QPushButton#languageButton, QPushButton#themeButton { color: #8fffb6; font-weight: 700; }
QComboBox QAbstractItemView { background-color: #07100c; color: #ffffff; selection-background-color: #125d31; }
QRadioButton { color: #e4f8ea; spacing: 7px; }
QRadioButton::indicator { width: 13px; height: 13px; border-radius: 7px; border: 1px solid #548b65; }
QRadioButton::indicator:checked { background-color: #00e676; border: 2px solid #b8ffd0; }
QStatusBar { color: #75b58a; border-top: 1px solid #173b26; }
QToolTip { background-color: #0c1d14; color: #ffffff; border: 1px solid #2f8250; }
"""

LIGHT_STYLE = """
QWidget { background-color: #f4f6f8; color: #17202a; font-family: "Inter", "SF Pro Display", "PingFang SC", sans-serif; font-size: 13px; }
QMainWindow, QStatusBar { background-color: #edf0f3; }
QLabel { background-color: transparent; }
QFrame#appHeader { background-color: #ffffff; border: 1px solid #d5dce3; border-radius: 10px; }
QLabel#appTitle { color: #111820; font-size: 22px; font-weight: 700; letter-spacing: 1px; }
QLabel#appSubtitle { color: #687684; font-size: 11px; }
QLabel#systemBadge { background-color: #e7f7ed; color: #087a3e; border: 1px solid #9bd5b2; border-radius: 11px; padding: 5px 11px; font-weight: 650; }
QLabel#heartRateBadge { background-color: #fff3e6; color: #9a4b00; border: 1px solid #efb777; border-radius: 11px; padding: 5px 12px; font-size: 15px; font-weight: 750; }
QSplitter#plotSplitter::handle { background-color: #d7dde3; height: 5px; }
QGroupBox { background-color: #ffffff; border: 1px solid #d5dce3; border-radius: 9px; margin-top: 11px; padding: 10px 10px 8px 10px; font-weight: 650; color: #34404c; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 7px; }
QPushButton, QComboBox { background-color: #ffffff; border: 1px solid #b7c2cc; border-radius: 6px; padding: 7px 14px; color: #17202a; }
QPushButton:hover { background-color: #fff4e8; border-color: #e88324; }
QPushButton:pressed { background-color: #e88324; color: #ffffff; }
QPushButton:disabled { color: #9aa4ad; border-color: #d9dee3; background-color: #f1f3f5; }
QPushButton#languageButton, QPushButton#themeButton { color: #9a4b00; font-weight: 700; }
QComboBox QAbstractItemView { background-color: #ffffff; color: #17202a; selection-background-color: #ffe0bf; }
QRadioButton { color: #25313c; spacing: 7px; }
QRadioButton::indicator { width: 13px; height: 13px; border-radius: 7px; border: 1px solid #929da7; }
QRadioButton::indicator:checked { background-color: #e88324; border: 2px solid #ffd3a6; }
QStatusBar { color: #5f6c78; border-top: 1px solid #d5dce3; }
QToolTip { background-color: #ffffff; color: #17202a; border: 1px solid #b7c2cc; }
"""
