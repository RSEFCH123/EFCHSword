#include "ThemeManager.h"

#include <QApplication>
#include <QStyleFactory>
#include <QSettings>
#include <QDebug>
#include <QTimer>

#include <windows.h>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager inst;
    return inst;
}

ThemeManager::ThemeManager() : QObject(nullptr) {}

ThemeManager::~ThemeManager() = default;

bool ThemeManager::detectSystemDarkMode()
{
    // 读取注册表: HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme
    // 0 = 深色模式, 1 = 浅色模式
    QSettings settings(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)"),
                       QSettings::NativeFormat);
    QVariant value = settings.value(QStringLiteral("AppsUseLightTheme"), 1);
    return value.toInt() == 0;
}

void ThemeManager::initialize(QApplication* app)
{
    m_app = app;

    // 使用 Fusion 风格作为基础，它对样式表支持最好
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // 检测系统主题
    m_theme = detectSystemDarkMode() ? Dark : Light;
    applyTheme(m_theme);

    // 监听系统主题变化
    startThemeWatcher();
}

void ThemeManager::startThemeWatcher()
{
    // 通过定时器轮询注册表检测主题变化
    // Windows 没有直接的主题变化通知 API（WM_SETTINGCHANGE 不总是触发）
    QTimer* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, [this]() {
        bool dark = detectSystemDarkMode();
        Theme newTheme = dark ? Dark : Light;
        if (newTheme != m_theme) {
            setTheme(newTheme);
        }
    });
    timer->start();
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    applyTheme(theme);
    emit themeChanged(theme);
}

void ThemeManager::applyTheme(Theme theme)
{
    if (m_app) {
        m_app->setStyleSheet(stylesheet(theme));
    }
}

QString ThemeManager::stylesheet(Theme theme) const
{
    // Win11 Fluent Design + VSCode 风格样式表
    if (theme == Dark) {
        // 深色主题 - VSCode Dark+ 风格
        return QStringLiteral(R"(
            /* 全局字体 */
            * {
                font-family: "Segoe UI Variable", "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;
                font-size: 13px;
            }

            /* 主窗口 */
            QMainWindow {
                background: #1e1e1e;
            }

            /* 菜单栏 */
            QMenuBar {
                background: #2d2d30;
                color: #cccccc;
                border: none;
                padding: 2px;
                min-height: 26px;
            }
            QMenuBar::item {
                background: transparent;
                padding: 4px 10px;
                border-radius: 4px;
            }
            QMenuBar::item:selected {
                background: #3d3d40;
            }
            QMenuBar::item:pressed {
                background: #094771;
            }
            QMenu {
                background: #252526;
                color: #cccccc;
                border: 1px solid #3d3d40;
                border-radius: 6px;
                padding: 4px;
            }
            QMenu::item {
                padding: 6px 24px 6px 16px;
                border-radius: 4px;
            }
            QMenu::item:selected {
                background: #094771;
            }
            QMenu::separator {
                height: 1px;
                background: #3d3d40;
                margin: 4px 8px;
            }

            /* 左侧导航栏 */
            QListWidget#navList {
                background: #252526;
                color: #cccccc;
                border: none;
                border-right: 1px solid #3d3d40;
                font-size: 13px;
                outline: none;
            }
            QListWidget#navList::item {
                padding: 10px 16px;
                border-left: 3px solid transparent;
                border-radius: 0px;
            }
            QListWidget#navList::item:selected {
                background: #37373d;
                border-left: 3px solid #007acc;
                color: #ffffff;
            }
            QListWidget#navList::item:hover {
                background: #2d2d30;
            }

            /* 状态栏 */
            QStatusBar {
                background: #007acc;
                color: #ffffff;
                border: none;
                min-height: 24px;
                font-size: 12px;
            }
            QStatusBar::item {
                border: none;
            }
            QStatusBar QLabel {
                color: #ffffff;
                padding: 0px 8px;
            }

            /* 表格 */
            QTableWidget {
                background: #1e1e1e;
                alternate-background-color: #252526;
                color: #d4d4d4;
                gridline-color: #3d3d40;
                border: none;
                selection-background-color: #094771;
                selection-color: #ffffff;
                outline: none;
            }
            QTableWidget::item {
                padding: 4px 8px;
                border: none;
            }
            QTableWidget::item:selected {
                background: #094771;
                color: #ffffff;
            }
            QHeaderView::section {
                background: #2d2d30;
                color: #cccccc;
                padding: 6px 8px;
                border: none;
                border-right: 1px solid #3d3d40;
                border-bottom: 1px solid #3d3d40;
                font-weight: 500;
            }
            QTableCornerButton::section {
                background: #2d2d30;
                border: none;
                border-bottom: 1px solid #3d3d40;
            }
            QTableWidget QTableCornerButton::section {
                background: #2d2d30;
            }

            /* 按钮 - Win11 Fluent 风格 */
            QPushButton {
                background: #3d3d40;
                color: #ffffff;
                border: 1px solid #3d3d40;
                border-radius: 4px;
                padding: 5px 16px;
                min-height: 20px;
            }
            QPushButton:hover {
                background: #4d4d50;
                border-color: #565659;
            }
            QPushButton:pressed {
                background: #2d2d30;
            }
            QPushButton:default {
                background: #007acc;
                border-color: #007acc;
            }
            QPushButton:default:hover {
                background: #1a8ad4;
            }

            /* 输入框 */
            QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                background: #2d2d30;
                color: #d4d4d4;
                border: 1px solid #3d3d40;
                border-radius: 4px;
                padding: 4px 8px;
                selection-background-color: #094771;
            }
            QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus {
                border-color: #007acc;
            }
            QComboBox::drop-down {
                border: none;
                width: 20px;
            }
            QComboBox QAbstractItemView {
                background: #252526;
                color: #cccccc;
                border: 1px solid #3d3d40;
                selection-background-color: #094771;
                selection-color: #ffffff;
                outline: none;
            }

            /* 复选框/单选框 */
            QCheckBox, QRadioButton {
                color: #d4d4d4;
                spacing: 6px;
            }
            QCheckBox::indicator, QRadioButton::indicator {
                width: 16px;
                height: 16px;
                border: 1px solid #565659;
                background: #2d2d30;
                border-radius: 3px;
            }
            QCheckBox::indicator:checked {
                background: #007acc;
                border-color: #007acc;
                image: none;
            }
            QRadioButton::indicator {
                border-radius: 8px;
            }
            QRadioButton::indicator:checked {
                background: #007acc;
                border-color: #007acc;
            }

            /* 标签 */
            QLabel {
                color: #d4d4d4;
                background: transparent;
            }

            /* 分组框 */
            QGroupBox {
                color: #cccccc;
                border: 1px solid #3d3d40;
                border-radius: 6px;
                margin-top: 8px;
                padding-top: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }

            /* 标签页 */
            QTabWidget::pane {
                border: 1px solid #3d3d40;
                border-radius: 4px;
                background: #1e1e1e;
            }
            QTabBar::tab {
                background: #2d2d30;
                color: #cccccc;
                padding: 6px 14px;
                border: 1px solid #3d3d40;
                border-bottom: none;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
                margin-right: 2px;
            }
            QTabBar::tab:selected {
                background: #1e1e1e;
                color: #ffffff;
                border-bottom: 2px solid #007acc;
            }
            QTabBar::tab:hover:!selected {
                background: #37373d;
            }

            /* 滚动条 */
            QScrollBar:vertical {
                background: #1e1e1e;
                width: 12px;
                border: none;
            }
            QScrollBar::handle:vertical {
                background: #424245;
                border-radius: 6px;
                min-height: 30px;
                margin: 2px;
            }
            QScrollBar::handle:vertical:hover {
                background: #565659;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0px;
            }
            QScrollBar:horizontal {
                background: #1e1e1e;
                height: 12px;
                border: none;
            }
            QScrollBar::handle:horizontal {
                background: #424245;
                border-radius: 6px;
                min-width: 30px;
                margin: 2px;
            }
            QScrollBar::handle:horizontal:hover {
                background: #565659;
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                width: 0px;
            }

            /* 工具提示 */
            QToolTip {
                background: #2d2d30;
                color: #d4d4d4;
                border: 1px solid #565659;
                border-radius: 4px;
                padding: 4px 8px;
            }

            /* 对话框 */
            QDialog {
                background: #1e1e1e;
            }

            /* 消息框 */
            QMessageBox {
                background: #252526;
            }
            QMessageBox QLabel {
                color: #d4d4d4;
            }

            /* 进度条 */
            QProgressBar {
                background: #2d2d30;
                border: none;
                border-radius: 4px;
                text-align: center;
                color: #d4d4d4;
            }
            QProgressBar::chunk {
                background: #007acc;
                border-radius: 4px;
            }

            /* 树形控件 */
            QTreeWidget {
                background: #1e1e1e;
                alternate-background-color: #252526;
                color: #d4d4d4;
                border: none;
                outline: none;
            }
            QTreeWidget::item {
                padding: 3px 0px;
                border: none;
            }
            QTreeWidget::item:selected {
                background: #094771;
                color: #ffffff;
            }
            QTreeWidget::item:hover {
                background: #2d2d30;
            }
        )");
    } else {
        // 浅色主题 - Win11 Fluent 风格
        return QStringLiteral(R"(
            /* 全局字体 */
            * {
                font-family: "Segoe UI Variable", "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;
                font-size: 13px;
            }

            /* 主窗口 */
            QMainWindow {
                background: #f3f3f3;
            }

            /* 菜单栏 */
            QMenuBar {
                background: #fafafa;
                color: #1e1e1e;
                border: none;
                border-bottom: 1px solid #e5e5e5;
                padding: 2px;
                min-height: 26px;
            }
            QMenuBar::item {
                background: transparent;
                padding: 4px 10px;
                border-radius: 4px;
            }
            QMenuBar::item:selected {
                background: #e5e5e5;
            }
            QMenuBar::item:pressed {
                background: #c7e0f4;
            }
            QMenu {
                background: #ffffff;
                color: #1e1e1e;
                border: 1px solid #e5e5e5;
                border-radius: 6px;
                padding: 4px;
            }
            QMenu::item {
                padding: 6px 24px 6px 16px;
                border-radius: 4px;
            }
            QMenu::item:selected {
                background: #c7e0f4;
            }
            QMenu::separator {
                height: 1px;
                background: #e5e5e5;
                margin: 4px 8px;
            }

            /* 左侧导航栏 */
            QListWidget#navList {
                background: #fafafa;
                color: #1e1e1e;
                border: none;
                border-right: 1px solid #e5e5e5;
                font-size: 13px;
                outline: none;
            }
            QListWidget#navList::item {
                padding: 10px 16px;
                border-left: 3px solid transparent;
            }
            QListWidget#navList::item:selected {
                background: #e8e8e8;
                border-left: 3px solid #0078d4;
                color: #000000;
            }
            QListWidget#navList::item:hover {
                background: #f0f0f0;
            }

            /* 状态栏 */
            QStatusBar {
                background: #0078d4;
                color: #ffffff;
                border: none;
                min-height: 24px;
                font-size: 12px;
            }
            QStatusBar::item {
                border: none;
            }
            QStatusBar QLabel {
                color: #ffffff;
                padding: 0px 8px;
            }

            /* 表格 */
            QTableWidget {
                background: #ffffff;
                alternate-background-color: #f7f7f7;
                color: #1e1e1e;
                gridline-color: #e5e5e5;
                border: none;
                selection-background-color: #c7e0f4;
                selection-color: #000000;
                outline: none;
            }
            QTableWidget::item {
                padding: 4px 8px;
                border: none;
            }
            QTableWidget::item:selected {
                background: #c7e0f4;
                color: #000000;
            }
            QHeaderView::section {
                background: #fafafa;
                color: #1e1e1e;
                padding: 6px 8px;
                border: none;
                border-right: 1px solid #e5e5e5;
                border-bottom: 1px solid #e5e5e5;
                font-weight: 500;
            }
            QTableCornerButton::section {
                background: #fafafa;
                border: none;
                border-bottom: 1px solid #e5e5e5;
            }

            /* 按钮 - Win11 Fluent 风格 */
            QPushButton {
                background: #ffffff;
                color: #1e1e1e;
                border: 1px solid #d1d1d1;
                border-radius: 4px;
                padding: 5px 16px;
                min-height: 20px;
            }
            QPushButton:hover {
                background: #f5f5f5;
                border-color: #b3b3b3;
            }
            QPushButton:pressed {
                background: #e5e5e5;
            }
            QPushButton:default {
                background: #0078d4;
                color: #ffffff;
                border-color: #0078d4;
            }
            QPushButton:default:hover {
                background: #1a86d9;
            }

            /* 输入框 */
            QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                background: #ffffff;
                color: #1e1e1e;
                border: 1px solid #d1d1d1;
                border-radius: 4px;
                padding: 4px 8px;
                selection-background-color: #c7e0f4;
                selection-color: #000000;
            }
            QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus {
                border-color: #0078d4;
            }
            QComboBox::drop-down {
                border: none;
                width: 20px;
            }
            QComboBox QAbstractItemView {
                background: #ffffff;
                color: #1e1e1e;
                border: 1px solid #d1d1d1;
                selection-background-color: #c7e0f4;
                selection-color: #000000;
                outline: none;
            }

            /* 复选框/单选框 */
            QCheckBox, QRadioButton {
                color: #1e1e1e;
                spacing: 6px;
            }
            QCheckBox::indicator, QRadioButton::indicator {
                width: 16px;
                height: 16px;
                border: 1px solid #8a8a8a;
                background: #ffffff;
                border-radius: 3px;
            }
            QCheckBox::indicator:checked {
                background: #0078d4;
                border-color: #0078d4;
            }
            QRadioButton::indicator {
                border-radius: 8px;
            }
            QRadioButton::indicator:checked {
                background: #0078d4;
                border-color: #0078d4;
            }

            /* 标签 */
            QLabel {
                color: #1e1e1e;
                background: transparent;
            }

            /* 分组框 */
            QGroupBox {
                color: #1e1e1e;
                border: 1px solid #e5e5e5;
                border-radius: 6px;
                margin-top: 8px;
                padding-top: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }

            /* 标签页 */
            QTabWidget::pane {
                border: 1px solid #e5e5e5;
                border-radius: 4px;
                background: #ffffff;
            }
            QTabBar::tab {
                background: #fafafa;
                color: #1e1e1e;
                padding: 6px 14px;
                border: 1px solid #e5e5e5;
                border-bottom: none;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
                margin-right: 2px;
            }
            QTabBar::tab:selected {
                background: #ffffff;
                color: #000000;
                border-bottom: 2px solid #0078d4;
            }
            QTabBar::tab:hover:!selected {
                background: #f0f0f0;
            }

            /* 滚动条 */
            QScrollBar:vertical {
                background: transparent;
                width: 12px;
                border: none;
            }
            QScrollBar::handle:vertical {
                background: #c1c1c1;
                border-radius: 6px;
                min-height: 30px;
                margin: 2px;
            }
            QScrollBar::handle:vertical:hover {
                background: #a1a1a1;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0px;
            }
            QScrollBar:horizontal {
                background: transparent;
                height: 12px;
                border: none;
            }
            QScrollBar::handle:horizontal {
                background: #c1c1c1;
                border-radius: 6px;
                min-width: 30px;
                margin: 2px;
            }
            QScrollBar::handle:horizontal:hover {
                background: #a1a1a1;
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                width: 0px;
            }

            /* 工具提示 */
            QToolTip {
                background: #ffffff;
                color: #1e1e1e;
                border: 1px solid #d1d1d1;
                border-radius: 4px;
                padding: 4px 8px;
            }

            /* 对话框 */
            QDialog {
                background: #f3f3f3;
            }

            /* 消息框 */
            QMessageBox {
                background: #ffffff;
            }
            QMessageBox QLabel {
                color: #1e1e1e;
            }

            /* 进度条 */
            QProgressBar {
                background: #e5e5e5;
                border: none;
                border-radius: 4px;
                text-align: center;
                color: #1e1e1e;
            }
            QProgressBar::chunk {
                background: #0078d4;
                border-radius: 4px;
            }

            /* 树形控件 */
            QTreeWidget {
                background: #ffffff;
                alternate-background-color: #f7f7f7;
                color: #1e1e1e;
                border: none;
                outline: none;
            }
            QTreeWidget::item {
                padding: 3px 0px;
                border: none;
            }
            QTreeWidget::item:selected {
                background: #c7e0f4;
                color: #000000;
            }
            QTreeWidget::item:hover {
                background: #f0f0f0;
            }
        )");
    }
}
