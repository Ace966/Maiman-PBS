#include "GuiFactory.h"
#include "XmlConfigLoader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray profile() {
    QFile file(QFINDTESTDATA("../config/device_gui.xml"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read GUI test profile");
    return file.readAll();
}

std::optional<DeviceConfiguration> model(quint16 id = 0x1700) {
    return XmlConfigLoader::loadModel(QFINDTESTDATA("../config/device_db.xml"), id);
}

std::optional<DeviceWidgetDesc> load(const QByteArray& xml, const DeviceConfiguration& configuration,
                                   QString* error = nullptr) {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("gui-fixture-XXXXXX")));
    if (!directory.isValid()) qFatal("Cannot create GUI test directory");
    QFile file(directory.filePath(QStringLiteral("fixture.xml")));
    if (!file.open(QIODevice::WriteOnly) || file.write(xml) != xml.size()) qFatal("Cannot write GUI fixture");
    file.close();
    return GuiFactory::create(file.fileName(), configuration, error);
}
}

class GuiFactoryTests : public QObject {
    Q_OBJECT
private slots:
    void catalog_data();
    void catalog();
    void configurationDriven();
    void failures_data();
    void failures();
    void lifetimeAndRetry();
};

void GuiFactoryTests::catalog_data() {
    QTest::addColumn<quint16>("id");
    QTest::addColumn<int>("count");
    QTest::newRow("PIC-8") << quint16(0x1700) << 8;
    QTest::newRow("PIC-16") << quint16(0x1701) << 16;
}

void GuiFactoryTests::catalog() {
    QFETCH(quint16, id);
    QFETCH(int, count);
    const auto configuration = model(id);
    QVERIFY(configuration);
    QString error = QStringLiteral("previous error");
    const auto gui = GuiFactory::create(QFINDTESTDATA("../config/device_gui.xml"), *configuration, &error);
    QVERIFY2(gui, qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(gui->id, id);
    QCOMPARE(gui->title, configuration->modelName());
    QVERIFY(!gui->description.isEmpty());
    QCOMPARE(gui->info.size(), 4);
    QCOMPARE(gui->info[0].code, quint16(0x0701));
    QCOMPARE(gui->info[0].title, QStringLiteral("Serial number"));
    QCOMPARE(gui->info[1].code, quint16(0x0710));
    QCOMPARE(gui->info[2].code, quint16(0x0408));
    QCOMPARE(gui->info[3].code, quint16(0x0AF4));
    QCOMPARE(gui->cards.size(), count);
    QCOMPARE(gui->limits.size(), count);
    // Check every channel, including 1, 2, 8 and (for PIC-16) 16 independently of expansion code.
    for (int index = 0; index < count; ++index) {
        const auto& card = gui->cards[index];
        const quint16 base = quint16(0x0011 + index * 0x0010);
        QCOMPARE(card.channel, quint16(index + 1));
        QCOMPARE(card.title, QStringLiteral("Channel %1").arg(index + 1));
        QCOMPARE(card.state, base);
        QCOMPARE(card.currentSet, quint16(base + 1));
        QCOMPARE(card.current, quint16(base + 4));
        QCOMPARE(card.voltageSet, quint16(base + 6));
        QCOMPARE(card.voltage, quint16(base + 9));
        QCOMPARE(card.runMask, quint16(0x0002));
        QCOMPARE(card.modeMask, quint16(0x0004));
        QCOMPARE(card.startCode, quint16(0x0008));
        QCOMPARE(card.stopCode, quint16(0x0010));
        QCOMPARE(card.ccCode, quint16(0x0020));
        QCOMPARE(card.cvCode, quint16(0x0040));
        QCOMPARE(card.overcurrent.command, quint16(0x0801));
        QCOMPARE(card.overvoltage.command, quint16(0x0802));
        QCOMPARE(card.overcurrent.mask, quint16(quint32(1) << index));
        QCOMPARE(card.overvoltage.mask, quint16(quint32(1) << index));
        QCOMPARE(card.overcurrent.message, QStringLiteral("Channel %1: overcurrent protection").arg(index + 1));
        const auto& limits = gui->limits[index];
        QCOMPARE(limits.channel, card.channel);
        QCOMPARE(limits.title, card.title);
        QCOMPARE(limits.currentMaximum.code, quint16(base + 2));
        QCOMPARE(limits.voltageMaximum.code, quint16(base + 7));
        QCOMPARE(limits.currentProtection.code, quint16(base + 5));
        QCOMPARE(limits.voltageProtection.code, quint16(base + 0xA));
        for (const auto* control : {&limits.currentMaximum, &limits.voltageMaximum,
                                    &limits.currentProtection, &limits.voltageProtection}) {
            QCOMPARE(control->minValue, 0.0);
            QVERIFY(!control->title.isEmpty());
            QVERIFY(control->maxCode);
        }
        QCOMPARE(*limits.currentMaximum.maxCode, quint16(base + 3));
        QCOMPARE(*limits.currentProtection.maxCode, quint16(base + 3));
        QCOMPARE(*limits.voltageMaximum.maxCode, quint16(base + 8));
        QCOMPARE(*limits.voltageProtection.maxCode, quint16(base + 8));
    }
}

void GuiFactoryTests::configurationDriven() {
    const auto pic = model();
    QVERIFY(pic);
    DeviceConfiguration generic(0x2222, QStringLiteral("Generic model"));
    for (auto it = pic->parameters().cbegin(); it != pic->parameters().cend(); ++it)
        QVERIFY(generic.addParameter(it.key(), it.value()));
    auto xml = profile();
    xml.replace(R"(id="1700")", R"(id="2222")");
    xml.replace(R"(first="1" count="8" base="0011")", R"(first="3" count="2" base="0031")");
    xml.replace(R"(runMask="0002")", R"(runMask="0040")");
    xml.replace(R"(modeMask="0004")", R"(modeMask="0080")");
    xml.replace(R"(start="0008")", R"(start="0000")");
    xml.replace(R"(cc="0020")", R"(cc="ABCD")");
    xml.replace(R"(cv="0040")", R"(cv="1234")");
    xml.replace(R"(mask="{bit}")", R"(mask="0005")");
    xml.replace(R"( maxOffset="3")", "");
    QString error;
    const auto gui = load(xml, generic, &error);
    QVERIFY2(gui, qPrintable(error));
    QCOMPARE(gui->id, quint16(0x2222));
    QCOMPARE(gui->title, QStringLiteral("Generic model"));
    QCOMPARE(gui->cards.size(), 2);
    QCOMPARE(gui->cards[0].channel, quint16(3));
    QCOMPARE(gui->cards[1].channel, quint16(4));
    QCOMPARE(gui->cards[0].state, quint16(0x0031));
    QCOMPARE(gui->cards[1].state, quint16(0x0041));
    QCOMPARE(gui->cards[0].runMask, quint16(0x0040));
    QCOMPARE(gui->cards[0].modeMask, quint16(0x0080));
    QCOMPARE(gui->cards[0].startCode, quint16(0));
    QCOMPARE(gui->cards[0].ccCode, quint16(0xABCD));
    QCOMPARE(gui->cards[0].cvCode, quint16(0x1234));
    QCOMPARE(gui->cards[0].overcurrent.mask, quint16(5));
    QVERIFY(!gui->limits[0].currentMaximum.maxCode);
    QVERIFY(!gui->limits[0].currentProtection.maxCode);
    QVERIFY(gui->limits[0].voltageMaximum.maxCode);
    // {bit} uses the physical channel number, not the repeat index.
    xml.replace(R"(mask="0005")", R"(mask="{bit}")");
    const auto shifted = load(xml, generic);
    QVERIFY(shifted);
    QCOMPARE(shifted->cards[0].overcurrent.mask, quint16(4));
    QCOMPARE(shifted->cards[1].overcurrent.mask, quint16(8));
}

void GuiFactoryTests::failures_data() {
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<QString>("diagnostic");
    const auto row = [](const char* name, const QByteArray& xml, const char* diagnostic) {
        QTest::newRow(name) << xml << QString::fromLatin1(diagnostic);
    };
    const auto changed = [](const char* before, const char* after) {
        return profile().replace(before, after);
    };
    row("malformed-xml", "<GUI><Device></GUI>", "XML at line");
    row("unknown-model", changed(R"(id="1700")", R"(id="2222")"), "Unknown GUI model");
    row("unknown-template", changed(R"(template="pic-channel")", R"(template="missing")"), "Unknown template");
    row("invalid-count", changed(R"(count="8")", R"(count="1.5")"), "invalid count");
    row("unknown-register", changed(R"(stateOffset="0")", R"(stateOffset="0200")"), "nonexistent register");
    row("duplicate-channel", changed("</Device>",
        R"(<Use template="pic-channel" first="1" count="1" base="0011" stride="0010"/></Device>)"),
        "Duplicate channel");
    row("address-overflow", changed(R"(stateOffset="0")", R"(stateOffset="FFFF")"), "overflow");
}

void GuiFactoryTests::failures() {
    QFETCH(QByteArray, xml);
    QFETCH(QString, diagnostic);
    const auto configuration = model();
    QVERIFY(configuration);
    QString error = QStringLiteral("previous error");
    QVERIFY(!load(xml, *configuration, &error));
    QVERIFY2(error.contains(diagnostic), qPrintable(error));
    QVERIFY(error.contains(QStringLiteral("fixture.xml")));
    QVERIFY(error != QStringLiteral("previous error"));
}

void GuiFactoryTests::lifetimeAndRetry() {
    const auto configuration = model();
    QVERIFY(configuration);
    QString error;
    QVERIFY(!GuiFactory::create(QStringLiteral("nonexistent-gui-definition.xml"), *configuration, &error));
    QVERIFY(!error.isEmpty());
    auto gui = load(profile(), *configuration, &error); // Temporary file and TreeItem are already destroyed.
    QVERIFY2(gui, qPrintable(error));
    QVERIFY(error.isEmpty());
    auto independent = *gui;
    independent.cards[0].title = QStringLiteral("Changed copy");
    QCOMPARE(gui->cards[0].title, QStringLiteral("Channel 1"));
    QVERIFY(!load("<GUI/>", *configuration));
    QVERIFY(load(profile(), *configuration));
}

QTEST_GUILESS_MAIN(GuiFactoryTests)
#include "tst_guifactory.moc"
