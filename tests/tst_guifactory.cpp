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

std::optional<DeviceWidgetDesc> load(const QByteArray& xml, const DeviceConfiguration& config,
                                     QString* error = nullptr) {
    QTemporaryDir dir(QDir::current().filePath(QStringLiteral("gui-fixture-XXXXXX")));
    if (!dir.isValid()) qFatal("Cannot create GUI test directory");
    QFile file(dir.filePath(QStringLiteral("fixture.xml")));
    if (!file.open(QIODevice::WriteOnly) || file.write(xml) != xml.size())
        qFatal("Cannot write GUI fixture");
    file.close();
    return GuiFactory::create(file.fileName(), config, error);
}
} // namespace

class GuiFactoryTests : public QObject {
    Q_OBJECT
private slots:
    void catalog_data();
    void catalog();
    void explicitConfiguration();
    void invalidConfiguration_data();
    void invalidConfiguration();
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
    const auto config = model(id);
    QVERIFY(config);
    QString error;
    const auto gui = GuiFactory::create(QFINDTESTDATA("../config/device_gui.xml"), *config, &error);
    QVERIFY2(gui, qPrintable(error));
    QCOMPARE(gui->id, id);
    QVERIFY(!gui->description.isEmpty());
    QCOMPARE(gui->header.size(), 1);
    QCOMPARE(gui->header[0].real, quint16(0x0AF4));
    QCOMPARE(gui->controls.size(), 4);
    QCOMPARE(gui->controls[0].real, quint16(0x0701));
    QCOMPARE(gui->controls[2].real, quint16(0x0408));
    QCOMPARE(gui->leds.size(), 5);
    QCOMPARE(gui->leds[0].masks[0].code, quint16(0x0800));
    QCOMPARE(gui->leds[0].masks[0].mask, quint16(0x0010));
    QCOMPARE(gui->leds[3].masks[0].code, quint16(0x0801));
    QCOMPARE(gui->leds[3].masks[0].mask, quint16(count == 8 ? 0x00FF : 0xFFFF));
    QCOMPARE(gui->cards.size(), count);
    QCOMPARE(gui->limits.size(), count);
    for (int i = 0; i < count; ++i) {
        const auto& card = gui->cards[i];
        const quint16 base = quint16(0x0011 + 0x0010 * i);
        QCOMPARE(card.channel, quint16(i + 1));
        QCOMPARE(card.title, QStringLiteral("Channel #%1").arg(i + 1));
        QCOMPARE(card.state, base);
        QCOMPARE(card.currentSet, quint16(base + 1));
        QCOMPARE(card.currentMax, quint16(base + 2));
        QCOMPARE(card.current, quint16(base + 4));
        QCOMPARE(card.voltageSet, quint16(base + 6));
        QCOMPARE(card.voltageMax, quint16(base + 7));
        QCOMPARE(card.voltage, quint16(base + 9));
        QCOMPARE(card.runMask, quint16(0x0002));
        QCOMPARE(card.modeMask, quint16(0x0004));
        QCOMPARE(card.startCode, quint16(0x0008));
        QCOMPARE(card.stopCode, quint16(0x0010));
        QCOMPARE(card.ccCode, quint16(0x0020));
        QCOMPARE(card.cvCode, quint16(0x0040));
        QCOMPARE(card.overcurrent.command, quint16(0x0801));
        QCOMPARE(card.overvoltage.command, quint16(0x0802));
        QCOMPARE(card.overcurrent.mask, quint16(1u << i));
        QCOMPARE(card.overvoltage.mask, quint16(1u << i));
        const auto& limits = gui->limits[i];
        QCOMPARE(limits.channel, card.channel);
        QCOMPARE(limits.controls.size(), 4);
        const quint16 codes[] = {quint16(base+2), quint16(base+7), quint16(base+5), quint16(base+10)};
        const quint16 maximums[] = {quint16(base+3), quint16(base+8), quint16(base+3), quint16(base+8)};
        for (int j = 0; j < 4; ++j) {
            QCOMPARE(limits.controls[j].code, codes[j]);
            QCOMPARE(limits.controls[j].minValue, 0.0);
            QVERIFY(limits.controls[j].maxCode);
            QCOMPARE(*limits.controls[j].maxCode, maximums[j]);
        }
    }
}

void GuiFactoryTests::explicitConfiguration() {
    const auto config = model();
    QVERIFY(config);
    auto xml = profile();
    QVERIFY(!xml.contains("<Use "));
    QVERIFY(!xml.contains("<Template "));
    QVERIFY(xml.contains("<ParamControls>"));
    QVERIFY(xml.contains("<Leds>"));
    QVERIFY(xml.contains("<Cards>"));
    QVERIFY(xml.contains("<Limits>"));
    // An edit to one explicitly described channel affects just that card.
    xml.replace(R"(current="0015")", R"(current="0016")");
    const auto gui = load(xml, *config);
    QVERIFY(gui);
    QCOMPARE(gui->cards[0].current, quint16(0x0016));
    QCOMPARE(gui->cards[1].current, quint16(0x0025));
    xml.replace(R"(currentMax="0013")", R"(currentMax="0014")");
    xml.replace(R"(voltageMax="0018")", R"(voltageMax="0019")");
    const auto changedMaximums = load(xml, *config);
    QVERIFY(changedMaximums);
    QCOMPARE(changedMaximums->cards[0].currentMax, quint16(0x0014));
    QCOMPARE(changedMaximums->cards[0].voltageMax, quint16(0x0019));
    QCOMPARE(changedMaximums->cards[1].currentMax, quint16(0x0023));
    QCOMPARE(changedMaximums->cards[1].voltageMax, quint16(0x0028));
}

void GuiFactoryTests::invalidConfiguration_data() {
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<QString>("message");
    const auto changed = [](const char* before, const char* after) {
        return profile().replace(before, after);
    };
    QTest::newRow("malformed") << QByteArray("<GUI><Device></GUI>") << QStringLiteral("XML at line");
    QTest::newRow("unknown-model") << changed(R"(id="1700")", R"(id="2200")") << QStringLiteral("Unknown GUI model");
    QTest::newRow("missing-current-maximum") << changed(R"( currentMax="0013")", "") << QStringLiteral("missing currentMax");
    QTest::newRow("missing-voltage-maximum") << changed(R"( voltageMax="0018")", "") << QStringLiteral("missing voltageMax");
    QTest::newRow("invalid-current-maximum") << changed(R"(currentMax="0013")", R"(currentMax="F000")") << QStringLiteral("nonexistent register");
    QTest::newRow("invalid-voltage-maximum") << changed(R"(voltageMax="0018")", R"(voltageMax="F000")") << QStringLiteral("nonexistent register");
    QTest::newRow("invalid-register") << changed(R"(current="0015")", R"(current="F000")") << QStringLiteral("nonexistent register");
    QTest::newRow("duplicate-channel") << changed(R"(channel="2" title="Channel #2")", R"(channel="1" title="Channel #2")") << QStringLiteral("Duplicate channel");
    QTest::newRow("invalid-mask") << changed(R"(mask="0001")", R"(mask="10000")") << QStringLiteral("invalid mask");
}

void GuiFactoryTests::invalidConfiguration() {
    QFETCH(QByteArray, xml);
    QFETCH(QString, message);
    const auto config = model();
    QVERIFY(config);
    QString error;
    QVERIFY(!load(xml, *config, &error));
    QVERIFY2(error.contains(message), qPrintable(error));
}

QTEST_GUILESS_MAIN(GuiFactoryTests)
#include "tst_guifactory.moc"
