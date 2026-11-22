#include "HypervisorPage.h"
#include "AppContext.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>

namespace {
QString msrText(const char* name, quint64 v)
{
    return QStringLiteral("%1 = 0x%2").arg(QString::fromLatin1(name))
        .arg(v, 16, 16, QLatin1Char('0'));
}
QString describeCtl(quint64 v)
{
    // 显示 must-be-1(低32) / must-be-0(高32)
    return QStringLiteral("must1=0x%1 must0=0x%2")
        .arg((quint32)(v & 0xFFFFFFFF), 8, 16, QLatin1Char('0'))
        .arg((quint32)(v >> 32), 8, 16, QLatin1Char('0'));
}
}

HypervisorPage::HypervisorPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);

    // ---- 状态行 + 按钮
    auto* topRow = new QHBoxLayout();
    m_stateLabel = new QLabel(QStringLiteral("状态: 未知"), this);
    m_stateLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    topRow->addWidget(m_stateLabel);
    m_vendorLabel = new QLabel(this);
    topRow->addWidget(m_vendorLabel);
    topRow->addStretch();

    m_refreshBtn = new QPushButton(QStringLiteral("刷新状态"), this);
    m_startBtn = new QPushButton(QStringLiteral("启动 Hypervisor"), this);
    m_stopBtn = new QPushButton(QStringLiteral("停止 Hypervisor"), this);
    m_parityBtn = new QPushButton(QStringLiteral("Parity Error"), this);
    m_parityBtn->setToolTip(QStringLiteral("主动制造三重故障，CPU 立即复位（VM 直接重启进 BIOS 80x25）。整台机器会断电式重启，未保存数据全丢。"));
    m_stopBtn->setEnabled(false);
    connect(m_parityBtn, &QPushButton::clicked, this, &HypervisorPage::onParityNow);
    for (auto* b : { m_refreshBtn, m_startBtn, m_stopBtn, m_parityBtn }) {
        b->setMaximumHeight(28);
        topRow->addWidget(b);
    }
    root->addLayout(topRow);

    // ---- 功能选项
    auto* optRow = new QHBoxLayout();
    optRow->addWidget(new QLabel(QStringLiteral("CPUID 拦截功能:"), this));
    m_hideHvCheck = new QCheckBox(QStringLiteral("隐藏虚拟化痕迹 (CPUID.1:ECX[31]=0, 0x40000000 置空)"), this);
    m_spoofVendorCheck = new QCheckBox(QStringLiteral("伪造 0x40000000 厂商串为 EFCHSwordNeo"), this);
    optRow->addWidget(m_hideHvCheck);
    optRow->addWidget(m_spoofVendorCheck);
    optRow->addStretch();
    root->addLayout(optRow);

    // ---- 新增虚拟化能力开关
    auto* optRow2 = new QHBoxLayout();
    optRow2->addWidget(new QLabel(QStringLiteral("虚拟化增强:"), this));
    m_nestedVmxCheck = new QCheckBox(QStringLiteral("嵌套 VMX (客户机内再跑 VT-x)"), this);
    m_evmcsCheck = new QCheckBox(QStringLiteral("eVMCS (Enlightened VMCS)"), this);
    m_veCheck = new QCheckBox(QStringLiteral("VE (VM-Exit 信息事件, 需 EPT)"), this);
    m_vmfuncCheck = new QCheckBox(QStringLiteral("VMFUNC (EPTP 切换)"), this);
    for (auto* c : { m_nestedVmxCheck, m_evmcsCheck, m_veCheck, m_vmfuncCheck }) {
        optRow2->addWidget(c);
    }
    optRow2->addStretch();
    root->addLayout(optRow2);

    auto* warn = new QLabel(
        QStringLiteral("启动后驱动会对全部 CPU 启用硬件虚拟化（Intel VT-x / AMD SVM，不使用 EPT，客户机即当前操作系统本身）。"
                       "在 VMware/VirtualBox 中必须开启嵌套虚拟化；若系统启用了 VBS/HVCI（基于 Hyper-V 的内核隔离），宿主侧无法再启动 VT-x。"
                       "启动失败不会影响系统，但请在虚拟机中优先测试。"), this);
    warn->setStyleSheet(QStringLiteral("color: gray;"));
    warn->setWordWrap(true);
    root->addWidget(warn);

    m_statsLabel = new QLabel(QStringLiteral("VM-exit 统计: -"), this);
    root->addWidget(m_statsLabel);

    // ---- Ring-1 内存读写
    auto* r1Group = new QGroupBox(QStringLiteral("Ring-1 内存读写（页表遍历 + 物理映射，不 attach 进程）"), this);
    auto* r1Layout = new QGridLayout(r1Group);
    r1Layout->addWidget(new QLabel(QStringLiteral("PID (0=物理地址):"), r1Group), 0, 0);
    m_r1PidEdit = new QLineEdit(r1Group);
    m_r1PidEdit->setMaximumWidth(80);
    m_r1PidEdit->setPlaceholderText(QStringLiteral("0"));
    r1Layout->addWidget(m_r1PidEdit, 0, 1);
    r1Layout->addWidget(new QLabel(QStringLiteral("地址(hex):"), r1Group), 0, 2);
    m_r1AddrEdit = new QLineEdit(r1Group);
    m_r1AddrEdit->setMaximumWidth(150);
    m_r1AddrEdit->setPlaceholderText(QStringLiteral("7FF600000000"));
    r1Layout->addWidget(m_r1AddrEdit, 0, 3);
    r1Layout->addWidget(new QLabel(QStringLiteral("大小:"), r1Group), 0, 4);
    m_r1SizeEdit = new QLineEdit(r1Group);
    m_r1SizeEdit->setMaximumWidth(70);
    m_r1SizeEdit->setPlaceholderText(QStringLiteral("64"));
    r1Layout->addWidget(m_r1SizeEdit, 0, 5);
    m_r1ReadBtn = new QPushButton(QStringLiteral("读取"), r1Group);
    m_r1WriteBtn = new QPushButton(QStringLiteral("写入 (十六进制)"), r1Group);
    r1Layout->addWidget(m_r1ReadBtn, 0, 6);
    r1Layout->addWidget(m_r1WriteBtn, 0, 7);
    m_r1DataEdit = new QPlainTextEdit(r1Group);
    m_r1DataEdit->setReadOnly(false);
    m_r1DataEdit->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_r1DataEdit->setPlaceholderText(QStringLiteral("读出的数据以十六进制显示；写入时在文本框中粘贴十六进制字节"));
    m_r1DataEdit->setMaximumHeight(110);
    r1Layout->addWidget(m_r1DataEdit, 1, 0, 1, 8);
    m_r1StatusLabel = new QLabel(QStringLiteral("提示: 支持 PID=0 直接物理地址读写；虚拟地址按目标进程页表翻译。"), r1Group);
    m_r1StatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    r1Layout->addWidget(m_r1StatusLabel, 2, 0, 1, 8);
    root->addWidget(r1Group);

    // ---- 能力 + exit 统计
    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_capabilityTree = new QTreeWidget(splitter);
    m_capabilityTree->setHeaderLabels({ QStringLiteral("VMX 能力"), QStringLiteral("值 / 说明") });
    m_capabilityTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    m_exitTable = new QTableWidget(splitter);
    m_exitTable->setColumnCount(2);
    m_exitTable->setHorizontalHeaderLabels({ QStringLiteral("CPU"), QStringLiteral("VM-exit 次数") });
    m_exitTable->verticalHeader()->setVisible(false);
    m_exitTable->horizontalHeader()->setStretchLastSection(true);
    m_exitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    splitter->addWidget(m_capabilityTree);
    splitter->addWidget(m_exitTable);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(m_refreshBtn, &QPushButton::clicked, this, &HypervisorPage::refreshStatus);
    connect(m_startBtn, &QPushButton::clicked, this, &HypervisorPage::startHypervisor);
    connect(m_stopBtn, &QPushButton::clicked, this, &HypervisorPage::stopHypervisor);
    connect(m_r1ReadBtn, &QPushButton::clicked, this, &HypervisorPage::onRing1Read);
    connect(m_r1WriteBtn, &QPushButton::clicked, this, &HypervisorPage::onRing1Write);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &HypervisorPage::refreshStatus);
    m_timer->start();
    refreshStatus();
}

HypervisorPage::~HypervisorPage() = default;

void HypervisorPage::onParityNow()
{
    auto ret = QMessageBox::warning(this, QStringLiteral("EFCHSword"),
        QStringLiteral("确定现在进 Parity Error 吗？CPU 会立即三重故障复位，虚拟机/整机断电式重启，未保存数据全丢！"),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    auto ret2 = QMessageBox::critical(this, QStringLiteral("EFCHSword"),
        QStringLiteral("最后确认：这是不可恢复的硬复位。继续吗？"),
        QMessageBox::Yes | QMessageBox::No);
    if (ret2 != QMessageBox::Yes) return;
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        ctx.driver().hvParityNow();
    }
    // 正常情况下走不到这里：CPU 已经复位。
}

void HypervisorPage::refreshStatus()
{
    auto& ctx = AppContext::instance();
    KernelDriver::HvQueryInfo info;
    if (!ctx.driverLoaded() || !ctx.driver().hvQuery(info)) {
        m_stateLabel->setText(QStringLiteral("状态: 查询失败（驱动未加载或不支持 0xB06）"));
        return;
    }
    updateUi(info);
}

void HypervisorPage::updateUi(const KernelDriver::HvQueryInfo& info)
{
    // 本驱动的常驻才算"运行中"：inHypervisorEnv 只说明本系统本身跑在
    // 别人的 hypervisor 之下（VMware/Hyper-V 嵌套），不能当启动状态用。
    const bool running = info.activeCpus != 0;
    const QString vendorName = info.vendor == 2 ? QStringLiteral("AMD (SVM)")
        : info.vendor == 1 ? QStringLiteral("Intel (VT-x)") : QStringLiteral("未知");
    m_stateLabel->setText(running
        ? QStringLiteral("状态: Hypervisor 运行中 (%1/%2 CPU)").arg(info.activeCpus).arg(info.cpuCount)
        : QStringLiteral("状态: 未运行  (平台: %1, 支持: %2%3)")
            .arg(vendorName)
            .arg(info.supported ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(info.inHypervisorEnv ? QStringLiteral(", 已处于外部 hypervisor 之下") : QStringLiteral("")));
    m_stateLabel->setStyleSheet(running ? QStringLiteral("font-weight: bold; color: green;")
                                       : QStringLiteral("font-weight: bold;"));
    m_vendorLabel->setText(QStringLiteral("当前 hypervisor 厂商串: %1")
        .arg(QString::fromLatin1(info.cpuidVendor).isEmpty() ? QStringLiteral("(空)") : QString::fromLatin1(info.cpuidVendor)));

    m_startBtn->setEnabled(info.supported && !running);
    m_stopBtn->setEnabled(running);

    // exit 速率
    const quint64 total = info.totalExits;
    const quint64 rate = total >= m_lastTotalExits ? total - m_lastTotalExits : 0;
    m_lastTotalExits = total;
    m_statsLabel->setText(QStringLiteral("VM-exit 统计: 总计 %1 次 (每秒约 %2 次)")
        .arg(total).arg(rate));

    // 能力树
    m_capabilityTree->clear();
    auto add = [this](const QString& k, const QString& v) {
        auto* it = new QTreeWidgetItem(m_capabilityTree, { k, v });
        Q_UNUSED(it);
    };
    if (info.vendor == 2) {
        // AMD: MsrFeatureControl 字段复用为 VM_CR
        add(QStringLiteral("平台"), QStringLiteral("AMD (AMD-V / SVM)"));
        add(QStringLiteral("VM_CR"), QStringLiteral("0x%1  SVMDIS=%2 Lock=%3")
            .arg((quint32)info.msrFeatureControl, 8, 16, QLatin1Char('0'))
            .arg((info.msrFeatureControl >> 4) & 1).arg(info.msrFeatureControl & 1));
        add(QStringLiteral("拦截项"), QStringLiteral("CPUID / VMMCALL / VMRUN（#VMEXIT）"));
    }
    else {
        add(QStringLiteral("平台"), QStringLiteral("Intel (VT-x)"));
        add(QStringLiteral("IA32_FEATURE_CONTROL"), describeCtl(info.msrFeatureControl) +
            (info.msrFeatureControl & 1
                ? (info.msrFeatureControl & (1ULL << 2) ? QStringLiteral(" (已锁定, 允许 VMX)") : QStringLiteral(" (已锁定, 禁止 VMX!)"))
                : QStringLiteral(" (未锁定)")));
        add(QStringLiteral("IA32_VMX_BASIC"), QStringLiteral("0x%1 rev=%2")
            .arg(info.msrVmxBasic, 16, 16, QLatin1Char('0')).arg((quint32)(info.msrVmxBasic & 0x7FFFFFFF)));
        add(QStringLiteral("IA32_VMX_PINBASED_CTLS"), describeCtl(info.msrVmxPin));
        add(QStringLiteral("IA32_VMX_PROCBASED_CTLS"), describeCtl(info.msrVmxProc));
        add(QStringLiteral("IA32_VMX_PROCBASED_CTLS2"), describeCtl(info.msrVmxProc2));
        add(QStringLiteral("IA32_VMX_EXIT_CTLS"), describeCtl(info.msrVmxExit));
        add(QStringLiteral("IA32_VMX_ENTRY_CTLS"), describeCtl(info.msrVmxEntry));
        const bool ept = (info.msrVmxProc2 >> 32) & (1ULL << 1);
        add(QStringLiteral("EPT 支持"), ept ? QStringLiteral("是 (本驱动未使用 EPT)") : QStringLiteral("否"));
    }
    add(QStringLiteral("CPUID.0x40000000 厂商"),
        QString::fromLatin1(info.cpuidVendor).isEmpty() ? QStringLiteral("(无)") : QString::fromLatin1(info.cpuidVendor));

    // 每 CPU exit 表
    m_exitTable->setRowCount(0);
    for (quint32 i = 0; i < info.cpuCount && i < 64; ++i) {
        const int r = m_exitTable->rowCount();
        m_exitTable->insertRow(r);
        m_exitTable->setItem(r, 0, new QTableWidgetItem(QStringLiteral("CPU %1").arg(i)));
        m_exitTable->setItem(r, 1, new QTableWidgetItem(QString::number(info.exitCount[i])));
    }

    // 失败诊断
    QString failText;
    for (quint32 i = 0; i < info.cpuCount && i < 64; ++i) {
        if (info.failStage[i] == 0) continue;
        QString stage;
                switch (info.failStage[i]) {
                case 1: stage = QStringLiteral("vmxon 失败"); break;
                case 2: stage = QStringLiteral("vmclear/vmptrld 失败"); break;
                case 3: stage = QStringLiteral("vmlaunch 失败 (code=原始EFLAGS: bit0=VMfailInvalid, bit6=VMfailValid)"); break;
                case 4: stage = QStringLiteral("内核 CET 影子栈开启，暂不支持"); break;
                default: stage = QStringLiteral("阶段 %1").arg(info.failStage[i]); break;
                }
        failText += QStringLiteral("CPU%1: %2 (code=0x%3)\n")
            .arg(i).arg(stage).arg(info.failCode[i], 8, 16, QLatin1Char('0'));
    }
    if (!failText.isEmpty()) {
        m_statsLabel->setText(m_statsLabel->text() + QStringLiteral("\n启动失败诊断:\n") + failText);
    }
}

void HypervisorPage::startHypervisor()
{
    quint32 options = 0x1;   // 拦截 CPUID
    if (m_hideHvCheck->isChecked()) options |= 0x2;
    if (m_spoofVendorCheck->isChecked()) options |= 0x4;
    if (m_nestedVmxCheck->isChecked()) options |= 0x10;
    if (m_evmcsCheck->isChecked()) options |= 0x20;
    if (m_veCheck->isChecked()) options |= 0x40;
    if (m_vmfuncCheck->isChecked()) options |= 0x80;

    const auto ret = QMessageBox::warning(this, QStringLiteral("启动 Hypervisor"),
        QStringLiteral("将在全部 CPU 上启动 VT-x Hypervisor（拦截 CPUID）。\n"
                       "若处理器/虚拟机不支持嵌套虚拟化可能启动失败；启动后系统行为可能出现细微变化。\n\n确定继续?"),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("Hypervisor"), QStringLiteral("驱动未加载"));
        return;
    }
    if (!ctx.driver().hvStart(options)) {
        KernelDriver::HvQueryInfo diag;
        QString diagText;
        quint32 lastStatus = 0;
        if (ctx.driver().hvQuery(diag)) {
            lastStatus = diag.lastStartStatus;
            diagText += QStringLiteral("\n启动 NTSTATUS = 0x%1").arg(diag.lastStartStatus, 8, 16, QLatin1Char('0'));
            if (diag.lastStartPartial) {
                diagText += QStringLiteral("\n部分启动: %1 个 CPU 失败").arg(diag.lastStartPartial);
            }
            for (quint32 i = 0; i < diag.cpuCount && i < 64; ++i) {
                if (diag.failStage[i] == 0) continue;
                QString stage;
                switch (diag.failStage[i]) {
                case 1: stage = QStringLiteral("vmxon 失败 (CPUID/BIOS 层面 VMX 不可用)"); break;
                case 2: stage = QStringLiteral("vmclear/vmptrld 失败"); break;
                case 3: {
                    if (diag.failCode[i] & 0x40) {   // VMfailValid
                        switch (diag.vmInstrError[i]) {
                        case 6: stage = QStringLiteral("vmlaunch 失败: 控制字段非法 (VM-instruction error 6)"); break;
                        case 7: stage = QStringLiteral("vmlaunch 失败: 宿主状态字段非法 (VM-instruction error 7)"); break;
                        default:
                            stage = QStringLiteral("vmlaunch 失败: VM-instruction error %1")
                                        .arg(diag.vmInstrError[i] & 0x7FFFFFFF);
                            break;
                        }
                    } else {
                        stage = QStringLiteral("vmlaunch 失败: VMfailInvalid (VMCS 无效)");
                    }
                    break;
                }
                case 4: stage = QStringLiteral("内核 CET 影子栈开启，暂不支持"); break;
                case 5: stage = QStringLiteral("自校验发现非法宿主字段: 0x%1")
                            .arg(diag.vmInstrError[i] & 0x7FFFFFFF, 0, 16); break;
                default: stage = QStringLiteral("阶段 %1").arg(diag.failStage[i]); break;
                }
                diagText += QStringLiteral("\nCPU%1: %2 (EFLAGS=0x%3, vmInstrErr=%4, vmWriteBad=0x%5)")
                    .arg(i).arg(stage)
                    .arg(diag.failCode[i], 8, 16, QLatin1Char('0'))
                    .arg(diag.vmInstrError[i])
                    .arg(diag.vmWriteBadField[i], 0, 16);
            }
        }
        QMessageBox::warning(this, QStringLiteral("Hypervisor"),
            QStringLiteral("启动失败 (Win32 错误 %1)。%2\n\n常见原因: 未开启嵌套虚拟化 / BIOS 关闭 VT-x / 已被其他 hypervisor (Hyper-V、VBS) 占用。\n请把上面的 NTSTATUS 与 CPU 诊断行反馈给开发者。")
                .arg(ctx.driver().lastIoError()).arg(diagText));
    }
    refreshStatus();
}

void HypervisorPage::stopHypervisor()
{
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) ctx.driver().hvStop();
    refreshStatus();
}

// ---------------------------------------------------------------------------
// Ring-1 内存读写
// ---------------------------------------------------------------------------
QString HypervisorPage::hexDump(const QByteArray& data)
{
    QString out;
    for (int i = 0; i < data.size(); i += 16) {
        out += QStringLiteral("%1  ").arg(i, 4, 16, QLatin1Char('0'));
        QString ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < data.size()) {
                const quint8 b = quint8(data[i + j]);
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
            } else {
                out += QStringLiteral("   ");
            }
            if (j == 7) out += QChar(' ');
        }
        out += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    return out;
}

QByteArray HypervisorPage::parseHex(const QString& text)
{
    QByteArray out;
    QString t;
    for (QChar c : text) {
        if (c.isDigit() || (c.toLower() >= QChar('a') && c.toLower() <= QChar('f'))) t += c;
    }
    if (t.size() % 2) t.chop(1);
    for (int i = 0; i + 1 < t.size(); i += 2)
        out.append(char(t.mid(i, 2).toInt(nullptr, 16)));
    return out;
}

void HypervisorPage::onRing1Read()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) { m_r1StatusLabel->setText(QStringLiteral("驱动未加载")); return; }
    bool ok = false;
    const quint32 pid = m_r1PidEdit->text().toUInt(&ok);
    if (!ok) { m_r1StatusLabel->setText(QStringLiteral("PID 无效")); return; }
    const quint64 addr = m_r1AddrEdit->text().toULongLong(&ok, 16);
    if (!ok) { m_r1StatusLabel->setText(QStringLiteral("地址无效 (需要十六进制)")); return; }
    quint32 size = m_r1SizeEdit->text().toUInt(&ok);
    if (!ok || size == 0) size = 64;
    size = qMin<quint32>(size, 0x8000);

    QByteArray data;
    data.resize(int(size));
    if (!ctx.driver().hvReadMem(pid, addr, data)) {
        m_r1StatusLabel->setText(QStringLiteral("读取失败 (Win32 错误 %1)").arg(ctx.driver().lastIoError()));
        return;
    }
    m_r1DataEdit->setPlainText(hexDump(data));
    m_r1StatusLabel->setText(QStringLiteral("已读取 %1 字节").arg(size));
}

void HypervisorPage::onRing1Write()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) { m_r1StatusLabel->setText(QStringLiteral("驱动未加载")); return; }
    bool ok = false;
    const quint32 pid = m_r1PidEdit->text().toUInt(&ok);
    if (!ok) { m_r1StatusLabel->setText(QStringLiteral("PID 无效")); return; }
    const quint64 addr = m_r1AddrEdit->text().toULongLong(&ok, 16);
    if (!ok) { m_r1StatusLabel->setText(QStringLiteral("地址无效 (需要十六进制)")); return; }
    const QByteArray data = parseHex(m_r1DataEdit->toPlainText());
    if (data.isEmpty()) { m_r1StatusLabel->setText(QStringLiteral("请先在文本框中填入要写入的十六进制字节")); return; }
    const auto ret = QMessageBox::warning(this, QStringLiteral("Ring-1 写入"),
        QStringLiteral("确定把 %1 字节写入 %2 (PID=%3)?\n此操作绕过进程边界，写错地址可能直接蓝屏。")
            .arg(data.size()).arg(QStringLiteral("0x%1").arg(addr, 0, 16)).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    if (!ctx.driver().hvWriteMem(pid, addr, data)) {
        m_r1StatusLabel->setText(QStringLiteral("写入失败 (Win32 错误 %1)").arg(ctx.driver().lastIoError()));
        return;
    }
    m_r1StatusLabel->setText(QStringLiteral("已写入 %1 字节").arg(data.size()));
}
