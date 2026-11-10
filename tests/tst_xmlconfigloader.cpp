#include "XmlConfigLoader.h"
#include "DeviceRepository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray minimal() {
    return "<DB><Device id=\"2222\" name=\"Example\"><Commands>"
           "<Command code=\"0042\" unit=\"V\" divider=\"100\" tol=\"2\"/>"
           "</Commands></Device></DB>";
}

std::optional<DeviceConfiguration> loadText(const QByteArray& bytes, QString* error = nullptr,
                                           quint16 modelId = 0x2222) {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("xml-fixture-XXXXXX")));
    if (!directory.isValid()) qFatal("Cannot create XML fixture directory");
    QFile file(directory.filePath(QString::fromUtf8("definition-\xc4\x8d.xml")));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Cannot write XML fixture: %s", qPrintable(file.errorString()));
    file.close();
    return XmlConfigLoader::loadModel(file.fileName(), modelId, error);
}

QByteArray metadataHash(const DeviceConfiguration& configuration) {
    QByteArray canonical;
    for (auto it = configuration.parameters().cbegin(); it != configuration.parameters().cend(); ++it) {
        canonical += (QStringList{QStringLiteral("%1").arg(it.key(), 4, 16, QLatin1Char('0')).toUpper(),
            it->unit, QString::number(it->scale, 'g', 17), QString::number(it->tolerance)}
            .join(QLatin1Char('\t')) + QLatin1Char('\n')).toUtf8();
    }
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
}
}

class XmlConfigLoaderTests : public QObject {
    Q_OBJECT
private slots:
    void completeCatalog_data();
    void completeCatalog();
    void genericModelAndRepository();
    void optionalMetadata();
    void hexIdsAndSignedDivider();
    void numericBoundaries();
    void treeFieldRepresentation();
    void unknownFields();
    void syntaxLocation();
    void failure_data();
    void failure();
    void unknownModel();
    void missingFile();
    void errorOutputAndRetry();
};

void XmlConfigLoaderTests::completeCatalog_data() {
    QTest::addColumn<quint16>("id"); QTest::addColumn<QString>("name");
    QTest::addColumn<int>("count"); QTest::addColumn<QByteArray>("hash");
    // SHA256 of (ID,unit,scale,tolerance), sorted by ID, independently projected
    // from the reviewed Task 04 catalog before removing its PBS-only attributes.
    QTest::newRow("PIC-8") << quint16(0x1700) << QStringLiteral("PIC-8") << 97
        << QByteArray("85927a7fa0704568baccd74631edc3c9ba030e5dfa7e10668ad1f475d6c73128");
    QTest::newRow("PIC-16") << quint16(0x1701) << QStringLiteral("PIC-16") << 185
        << QByteArray("6ed5d73690357087555bedfe9cf56755212a646b522dcdb705872d59d8b27d21");
}

void XmlConfigLoaderTests::completeCatalog() {
    QFETCH(quint16, id); QFETCH(QString, name); QFETCH(int, count); QFETCH(QByteArray, hash);
    QString error = QStringLiteral("previous error");
    const auto result = XmlConfigLoader::loadModel(QFINDTESTDATA("../config/device_db.xml"), id, &error);
    QVERIFY2(result.has_value(), qPrintable(error)); QVERIFY(error.isEmpty());
    QCOMPARE(result->modelId(), id); QCOMPARE(result->modelName(), name);
    QCOMPARE(result->parameters().size(), count);
    QCOMPARE(metadataHash(*result), hash);
    for (const auto& parameter : result->parameters())
        QCOMPARE(parameter.tolerance, quint8(parameter.scale == 0.01 ? 2 : parameter.scale == 0.1 ? 1 : 0));
    QCOMPARE(result->parameters().value(0x0017).scale * 356, 3.56);
    QCOMPARE(result->parameters().value(0x0017).scale * 900, 9.0);
    QVERIFY(result->parameters().contains(0x0701));
    QVERIFY(result->parameters().contains(0x0001));
    QCOMPARE(result->parameters().contains(0x0091), id == 0x1701);
    QCOMPARE(result->parameters().contains(0x010A), id == 0x1701);
}

void XmlConfigLoaderTests::genericModelAndRepository() {
    auto xml = minimal().replace("Example", "PIC-8");
    const auto result = loadText(xml);
    QVERIFY(result.has_value());
    QCOMPARE(result->modelId(), quint16(0x2222));
    QCOMPARE(result->modelName(), QStringLiteral("PIC-8"));
    const auto renamed = loadText(xml.replace("PIC-8", "Other label"));
    QVERIFY(renamed.has_value()); QCOMPARE(renamed->modelId(), result->modelId());
    QCOMPARE(renamed->modelName(), QStringLiteral("Other label"));
    const auto makeRepository = [] {
        auto configuration = loadText(minimal());
        if (!configuration) qFatal("Invalid generic XML fixture");
        DeviceRepository repository(*configuration);
        if (!configuration->addParameter(0x0043, {QStringLiteral("mA"), 0.1}))
            qFatal("Invalid generic parameter fixture");
        return repository;
    };
    auto first = makeRepository(); auto second = first;
    QCOMPARE(first.configuration().modelName(), QStringLiteral("Example"));
    QVERIFY(!first.configuration().parameters().contains(0x0043));
    QVERIFY(!first.value(0x0042).has_value());
    QVERIFY(first.update(0x0042, 65535)); QVERIFY(second.update(0x0042, 0));
    QCOMPARE(first.value(0x0042).value(), quint16(65535)); QCOMPARE(second.value(0x0042).value(), quint16(0));
    QVERIFY(!first.update(0x7777, 10)); first.clear();
    QVERIFY(!first.value(0x0042)); QCOMPARE(second.value(0x0042).value(), quint16(0));
    QCOMPARE(first.configuration().modelId(), quint16(0x2222));
}

void XmlConfigLoaderTests::optionalMetadata() {
    const auto result = loadText(minimal().replace(" unit=\"V\" divider=\"100\" tol=\"2\"", ""));
    QVERIFY(result);
    const auto definition = result->parameters().value(0x0042);
    QVERIFY(definition.unit.isEmpty());
    QCOMPARE(definition.scale, 1.0); QCOMPARE(definition.tolerance, quint8(0));
}

void XmlConfigLoaderTests::hexIdsAndSignedDivider() {
    const auto xml = minimal().replace("id=\"2222\"", "id=\"10\"").replace("code=\"0042\"", "code=\"a\"")
        .replace("unit=\"V\"", "unit=\"mV\"").replace("divider=\"100\"", "divider=\"-10\"")
        .replace("<Device", "<BaudRate value=\"115200\"/><CommonIDDevices/><Device");
    const auto result = loadText(xml, nullptr, 0x10);
    QVERIFY(result); QCOMPARE(result->modelId(), quint16(16));
    const auto definition = result->parameters().value(0x000a);
    QCOMPARE(definition.unit, QStringLiteral("mV")); QCOMPARE(definition.scale, -0.1);
    QCOMPARE(definition.tolerance, quint8(2));
}

void XmlConfigLoaderTests::numericBoundaries() {
    const auto xml = minimal().replace("id=\"2222\"", "id=\"FFFF\"").replace("code=\"0042\"", "code=\"FFFF\"")
        .replace("tol=\"2\"", "tol=\"255\"");
    const auto result = loadText(xml, nullptr, 0xffff);
    QVERIFY(result); QCOMPARE(result->modelId(), quint16(0xffff));
    QCOMPARE(result->parameters().size(), 1);
    QCOMPARE(result->parameters().value(0xffff).tolerance, quint8(255));
}

void XmlConfigLoaderTests::treeFieldRepresentation() {
    // Attributes and same-named leaf elements have the same MultiBench tree shape.
    const auto result = loadText(
        "<DB><Device><id>2222</id><name>Example</name><Commands><Command>"
        "<code>0042</code><unit>V</unit><divider>100</divider><tol>2</tol>"
        "</Command></Commands></Device></DB>");
    QVERIFY(result);
    QCOMPARE(metadataHash(*result), metadataHash(*loadText(minimal())));
    QCOMPARE(result->modelName(), QStringLiteral("Example"));
}

void XmlConfigLoaderTests::unknownFields() {
    const auto xml = minimal().replace("<DB>", "<DB pbsMetadataVersion=\"obsolete\">")
        .replace("name=\"Example\"", "name=\"Example\" pbsChannelCount=\"ignored\"")
        .replace("code=\"0042\"", "code=\"0042\" pbsName=\"ignored\" pbsAccess=\"ignored\" pbsChannel=\"ignored\"")
        .replace("<Commands>", "<Unknown><Commands/></Unknown><Commands>")
        .replace("<Command code", "<Unknown><Command code=\"BAD\"/></Unknown><Command code");
    const auto result = loadText(xml);
    QVERIFY(result);
    QCOMPARE(result->parameters().size(), 1);
    QCOMPARE(metadataHash(*result), metadataHash(*loadText(minimal())));
}

void XmlConfigLoaderTests::syntaxLocation() {
    QString error;
    QVERIFY(!loadText("<DB>\n<Device></DB>", &error));
    QVERIFY2(error.contains(QStringLiteral("XML at line 2, column ")), qPrintable(error));
}

void XmlConfigLoaderTests::failure_data() {
    QTest::addColumn<QByteArray>("xml"); QTest::addColumn<QString>("diagnostic");
    const auto row = [](const char* name, const QByteArray& xml, const char* diagnostic) {
        QTest::newRow(name) << xml << QString::fromLatin1(diagnostic);
    };
    const auto changed = [](const char* before, const char* after) { return minimal().replace(before, after); };
    row("empty", "", "XML at line"); row("malformed", "<DB", "XML at line");
    row("wrong-root", "<Devices/>", "DB");
    row("missing-model-id", changed(" id=\"2222\"", ""), "id");
    row("invalid-model-id", changed("id=\"2222\"", "id=\"00G0\""), "id");
    row("zero-model-id", changed("id=\"2222\"", "id=\"0\""), "id");
    row("negative-model-id", changed("id=\"2222\"", "id=\"-1\""), "id");
    row("overflow-model-id", changed("id=\"2222\"", "id=\"10000\""), "id");
    row("empty-name", changed("name=\"Example\"", "name=\"\""), "name");
    row("missing-name", changed(" name=\"Example\"", ""), "name");
    row("missing-command-code", changed(" code=\"0042\"", ""), "code");
    row("invalid-command-code", changed("code=\"0042\"", "code=\"G\""), "code");
    row("overflow-command-code", changed("code=\"0042\"", "code=\"10000\""), "code");
    row("zero-command-code", changed("code=\"0042\"", "code=\"0\""), "code");
    row("negative-command-code", changed("code=\"0042\"", "code=\"-1\""), "code");
    row("fractional-command-code", changed("code=\"0042\"", "code=\"1.5\""), "code");
    row("zero-divider", changed("divider=\"100\"", "divider=\"0\""), "divider");
    row("invalid-divider", changed("divider=\"100\"", "divider=\"wrong\""), "divider");
    row("nan-divider", changed("divider=\"100\"", "divider=\"nan\""), "divider");
    row("infinite-divider", changed("divider=\"100\"", "divider=\"inf\""), "divider");
    row("overflow-divider", changed("divider=\"100\"", "divider=\"1e9999\""), "divider");
    row("overflow-reciprocal", changed("divider=\"100\"", "divider=\"1e-320\""), "divider");
    row("negative-tol", changed("tol=\"2\"", "tol=\"-1\""), "tol");
    row("overflow-tol", changed("tol=\"2\"", "tol=\"256\""), "tol");
    row("fractional-tol", changed("tol=\"2\"", "tol=\"1.5\""), "tol");
    const auto commands = minimal().mid(minimal().indexOf("<Commands>"), minimal().indexOf("</Commands>") - minimal().indexOf("<Commands>") + 11);
    const auto device = minimal().mid(minimal().indexOf("<Device"), minimal().indexOf("</Device>") - minimal().indexOf("<Device") + 9);
    row("duplicate-device", minimal().replace("</DB>", QByteArray(device).replace("2222", "02222") + "</DB>"), "duplicate Device");
    row("duplicate-command", minimal().replace("</Commands>", "<Command code=\"42\"/></Commands>"), "duplicate code");
    row("duplicate-commands-list", minimal().replace("</Device>", commands + "</Device>"), "duplicate Commands");
    row("empty-commands", minimal().replace(commands, "<Commands/>"), "Commands");
    row("missing-commands", minimal().replace(commands, ""), "Commands");
    row("duplicate-attribute", changed("code=\"0042\"", "code=\"0042\" code=\"43\""), "");
    row("truncated-document", minimal().chopped(5), "");
    row("trailing-garbage", minimal() + "garbage", "");
    row("second-root", minimal() + "<DB/>", "");
    row("invalid-unselected-model", minimal().replace("</DB>", QByteArray(device).replace("2222", "1701").replace("divider=\"100\"", "divider=\"0\"") + "</DB>"), "divider");
}

void XmlConfigLoaderTests::failure() {
    QFETCH(QByteArray, xml); QFETCH(QString, diagnostic); QString error = QStringLiteral("previous error");
    QVERIFY(!loadText(xml, &error)); QVERIFY(!error.isEmpty()); QVERIFY(error != QStringLiteral("previous error"));
    QVERIFY2(error.contains(diagnostic), qPrintable(error));
    QVERIFY(error.contains(QLatin1Char(':'))); // Every failure includes the filename; syntax errors also include location.
}

void XmlConfigLoaderTests::unknownModel() {
    QString error;
    QVERIFY(!loadText(minimal(), &error, 0x1700));
    QVERIFY(error.contains(QStringLiteral("unknown model ID 1700")));
}

void XmlConfigLoaderTests::missingFile() {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("xml-missing-XXXXXX")));
    QVERIFY(directory.isValid()); QString error;
    QVERIFY(!XmlConfigLoader::loadModel(directory.filePath("missing.xml"), 0x1700, &error));
    QVERIFY(!error.isEmpty());
}

void XmlConfigLoaderTests::errorOutputAndRetry() {
    QVERIFY(!loadText("<", nullptr));
    QString error = QStringLiteral("previous error");
    QVERIFY(loadText(minimal(), &error)); QVERIFY(error.isEmpty());
    QVERIFY(loadText(minimal(), nullptr));
}

QTEST_GUILESS_MAIN(XmlConfigLoaderTests)
#include "tst_xmlconfigloader.moc"
