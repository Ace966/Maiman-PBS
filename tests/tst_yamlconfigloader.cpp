#include "YamlConfigLoader.h"
#include "DeviceRepository.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray minimal() {
    return "device:\n  model: PIC-8\nparameters:\n"
           "  - id: \"0017\"\n    name: \"Voltage setting\"\n    channel: 1\n"
           "    unit: V\n    scale: 0.01\n    access: rw\n";
}

std::optional<DeviceConfiguration> loadText(const QByteArray& yaml, QString* error,
                                          std::optional<quint16> modelId = std::nullopt) {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("yaml-fixture-XXXXXX")));
    if (!directory.isValid()) qFatal("Cannot create temporary fixture directory");
    QFile file(directory.filePath(QString::fromUtf8("configuration-\xc4\x8d.yaml")));
    if (!file.open(QIODevice::WriteOnly))
        qFatal("Cannot open YAML fixture %s: %s", qPrintable(file.fileName()), qPrintable(file.errorString()));
    if (file.write(yaml) != yaml.size()) qFatal("Cannot write YAML fixture: %s", qPrintable(file.errorString()));
    file.close();
    return modelId ? YamlConfigLoader::loadModel(file.fileName(), *modelId, error)
                   : YamlConfigLoader::loadFile(file.fileName(), error);
}

QByteArray catalog() {
    return "models:\n  \"1700\": {model: PIC-8, parameters: []}\n"
           "  \"1701\": {model: PIC-16, parameters: []}\ncommon:\n"
           "  - id: \"0017\"\n    name: \"Voltage setting\"\n    channel: 1\n    access: rw\n";
}
}

class YamlConfigLoaderTests : public QObject {
    Q_OBJECT
private slots:
    void pic8();
    void pic16();
    void repositoryIntegration();
    void failure_data();
    void failure();
    void missingFile();
    void optionalErrorOutput();
    void finiteScale_data();
    void finiteScale();
    void emptyParameterList();
    void sharedCatalog_data();
    void sharedCatalog();
    void commonDefinitionsMatch();
    void documentedMetadata_data();
    void documentedMetadata();
    void unknownModel_data();
    void unknownModel();
    void catalogRuntime_data();
    void catalogRuntime();
    void catalogFailure_data();
    void catalogFailure();
    void explicitSelectionRequired();
    void unselectedDefinitionsNotLoaded();
};

void YamlConfigLoaderTests::pic8() {
    QString error = QStringLiteral("old error");
    const auto result = YamlConfigLoader::loadFile(QFINDTESTDATA("fixtures/pic8.yaml"), &error);
    QVERIFY2(result.has_value(), qPrintable(error)); QVERIFY(error.isEmpty());
    QCOMPARE(result->model(), PICModel::PIC8); QCOMPARE(result->channelCount(), 8);
    QCOMPARE(result->parameters().size(), 5);
    const auto setting = result->parameters().value(0x0017);
    QCOMPARE(setting.name, QStringLiteral("Channel 1 voltage setting"));
    QCOMPARE(setting.channel.value(), quint8(1)); QCOMPARE(setting.unit, QStringLiteral("V"));
    QCOMPARE(setting.scale, 0.01); QCOMPARE(setting.access, ParameterAccess::ReadWrite);
    const auto measured = result->parameters().value(0x001A);
    QCOMPARE(measured.channel.value(), quint8(1)); QCOMPARE(measured.unit, QStringLiteral("V"));
    QCOMPARE(measured.scale, 0.01); QCOMPARE(measured.access, ParameterAccess::ReadOnly);
    const auto state = result->parameters().value(0x0021);
    QVERIFY(state.unit.isEmpty()); QCOMPARE(state.scale, 1.0);
    QCOMPARE(state.access, ParameterAccess::ReadWrite);
    QCOMPARE(result->parameters().value(0x0001).channel.value(), quint8(0));
    QCOMPARE(result->parameters().value(0x0001).access, ParameterAccess::WriteOnly);
    const auto input = result->parameters().value(0x0408);
    QVERIFY(!input.channel.has_value()); QCOMPARE(input.scale, 0.1);
    QCOMPARE(input.access, ParameterAccess::ReadOnly);
}

void YamlConfigLoaderTests::pic16() {
    QString error;
    const auto result = YamlConfigLoader::loadFile(QFINDTESTDATA("fixtures/pic16.yaml"), &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->model(), PICModel::PIC16); QCOMPARE(result->channelCount(), 16);
    const auto measured = result->parameters().value(0x010A);
    QCOMPARE(measured.name, QStringLiteral("Channel 16 measured voltage"));
    QCOMPARE(measured.channel.value(), quint8(16)); QCOMPARE(measured.unit, QStringLiteral("V"));
    QCOMPARE(measured.scale, 0.01); QCOMPARE(measured.access, ParameterAccess::ReadOnly);
}

void YamlConfigLoaderTests::repositoryIntegration() {
    QString error;
    const auto result = loadText(minimal(), &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    DeviceRepository repository(*result);
    QVERIFY(!repository.value(0x0017).has_value());
    QVERIFY(repository.update(0x0017, 356)); QCOMPARE(repository.value(0x0017).value(), quint16(356));
    repository.clear(); QVERIFY(!repository.value(0x0017).has_value());
    QCOMPARE(repository.configuration().channelCount(), 8);
    QCOMPARE(repository.configuration().parameters().value(0x0017).name, QStringLiteral("Voltage setting"));
    QCOMPARE(repository.configuration().parameters().value(0x0017).scale, 0.01);
}

void YamlConfigLoaderTests::failure_data() {
    QTest::addColumn<QByteArray>("yaml"); QTest::addColumn<QString>("field");
    const auto changed = [](const char* before, const char* after) { return minimal().replace(before, after); };
    const auto row = [](const char* name, const QByteArray& yaml, const char* field) {
        QTest::newRow(name) << yaml << QString::fromLatin1(field);
    };
    row("syntax", "device: [", "YAML");
    row("empty", "", "YAML");
    row("multiple-documents", minimal() + "---\n" + minimal(), "YAML");
    row("root-sequence", "[]", "YAML");
    row("missing-device", "parameters: []", "device");
    row("device-type", "device: []\nparameters: []", "device");
    row("missing-model", "device: {}\nparameters: []", "device.model");
    row("unsupported-model", changed("PIC-8", "PIC-32"), "device.model");
    row("model-type", changed("PIC-8", "[]"), "device.model");
    row("missing-parameters", "device: {model: PIC-8}", "parameters");
    row("parameters-type", "device: {model: PIC-8}\nparameters: {}", "parameters");
    row("parameter-type", "device: {model: PIC-8}\nparameters: [7]", "parameters[0]");
    row("missing-id", changed("id: \"0017\"", "other: 7"), ".id");
    row("unquoted-id", changed("\"0017\"", "0017"), ".id");
    row("lowercase-id", changed("0017", "001a"), ".id");
    row("short-id", changed("0017", "017"), ".id");
    row("large-id", changed("0017", "10017"), ".id");
    row("nonhex-id", changed("0017", "00G7"), ".id");
    row("prefix-id", changed("0017", "0x17"), ".id");
    row("id-type", changed("\"0017\"", "[]"), ".id");
    row("empty-name", changed("Voltage setting", ""), ".name");
    row("missing-name", changed("name:", "other:"), ".name");
    row("name-type", changed("\"Voltage setting\"", "[]"), ".name");
    row("numeric-name", changed("\"Voltage setting\"", "123"), ".name");
    row("boolean-name", changed("\"Voltage setting\"", "true"), ".name");
    row("negative-channel", changed("channel: 1", "channel: -1"), ".channel");
    row("channel-outside-model", changed("channel: 1", "channel: 9"), "ID 0017");
    row("channel-narrowing", changed("channel: 1", "channel: 256"), ".channel");
    row("channel-overflow", changed("channel: 1", "channel: 4294967296"), ".channel");
    row("fractional-channel", changed("channel: 1", "channel: 1.5"), ".channel");
    row("quoted-channel", changed("channel: 1", "channel: \"1\""), ".channel");
    row("null-channel", changed("channel: 1", "channel: null"), ".channel");
    row("unit-type", changed("unit: V", "unit: []"), ".unit");
    row("unsupported-access", changed("access: rw", "access: read"), ".access");
    row("missing-access", changed("access:", "other:"), ".access");
    row("access-type", changed("access: rw", "access: {}"), ".access");
    row("scale-nan", changed("scale: 0.01", "scale: .nan"), ".scale");
    row("scale-inf", changed("scale: 0.01", "scale: .inf"), ".scale");
    row("scale-overflow", changed("scale: 0.01", "scale: 1e9999"), ".scale");
    row("scale-nonnumeric", changed("scale: 0.01", "scale: invalid"), ".scale");
    row("scale-type", changed("scale: 0.01", "scale: []"), ".scale");
    row("quoted-scale", changed("scale: 0.01", "scale: \"0.01\""), ".scale");
    row("duplicate-id", minimal() + minimal().mid(minimal().indexOf("  - id:")), "ID 0017");
    row("duplicate-field", minimal() + "    id: \"001A\"\n", "parameters[0]");
    row("duplicate-model", changed("model: PIC-8", "model: PIC-8\n  model: PIC-16"), "device");
    row("duplicate-root-key", minimal() + "parameters: []\n", "YAML");
}

void YamlConfigLoaderTests::failure() {
    QFETCH(QByteArray, yaml); QFETCH(QString, field);
    QString error = QStringLiteral("old error");
    const auto result = loadText(yaml, &error);
    QVERIFY(!result.has_value()); QVERIFY(error != QStringLiteral("old error"));
    QVERIFY2(error.contains(field), qPrintable(error));
}

void YamlConfigLoaderTests::missingFile() {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("yaml-missing-XXXXXX")));
    QVERIFY(directory.isValid());
    QString error;
    QVERIFY(!YamlConfigLoader::loadFile(directory.filePath("missing.yaml"), &error));
    QVERIFY(error.startsWith(QStringLiteral("file:")));
}

void YamlConfigLoaderTests::optionalErrorOutput() {
    QVERIFY(loadText(minimal(), nullptr).has_value());
    QVERIFY(!loadText("[", nullptr).has_value());
}

void YamlConfigLoaderTests::finiteScale_data() {
    QTest::addColumn<QByteArray>("scale"); QTest::addColumn<double>("expected");
    QTest::newRow("zero") << QByteArray("0") << 0.0;
    QTest::newRow("negative") << QByteArray("-0.5") << -0.5;
    QTest::newRow("exponent") << QByteArray("1e-2") << 0.01;
}

void YamlConfigLoaderTests::finiteScale() {
    QFETCH(QByteArray, scale); QFETCH(double, expected);
    QString error;
    const auto result = loadText(minimal().replace("scale: 0.01", "scale: " + scale), &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->parameters().value(0x0017).scale, expected);
}

void YamlConfigLoaderTests::emptyParameterList() {
    QString error;
    const auto result = loadText("device: {model: PIC-8}\nparameters: []\nunused: ignored\n", &error);
    QVERIFY2(result.has_value(), qPrintable(error)); QVERIFY(result->parameters().isEmpty());
}

void YamlConfigLoaderTests::sharedCatalog_data() {
    QTest::addColumn<quint16>("id"); QTest::addColumn<int>("channels"); QTest::addColumn<int>("count");
    QTest::newRow("PIC-8-hex-1700") << quint16(0x1700) << 8 << 97;
    QTest::newRow("PIC-16-hex-1701") << quint16(0x1701) << 16 << 185;
}

void YamlConfigLoaderTests::sharedCatalog() {
    QFETCH(quint16, id); QFETCH(int, channels); QFETCH(int, count);
    QString error = QStringLiteral("old error");
    const auto result = YamlConfigLoader::loadModel(QFINDTESTDATA("../config/devices/pic.yaml"), id, &error);
    QVERIFY2(result.has_value(), qPrintable(error)); QVERIFY(error.isEmpty());
    QCOMPARE(result->channelCount(), channels); QCOMPARE(result->parameters().size(), count);
    QCOMPARE(result->model(), channels == 8 ? PICModel::PIC8 : PICModel::PIC16);
    QCOMPARE(result->parameters().contains(0x0091), channels == 16);
    QCOMPARE(result->parameters().contains(0x010A), channels == 16);
    for (const auto& definition : result->parameters())
        QVERIFY(!definition.channel || *definition.channel <= channels);
}

void YamlConfigLoaderTests::commonDefinitionsMatch() {
    const auto filename = QFINDTESTDATA("../config/devices/pic.yaml");
    const auto pic8 = YamlConfigLoader::loadModel(filename, 0x1700);
    const auto pic16 = YamlConfigLoader::loadModel(filename, 0x1701);
    QVERIFY(pic8.has_value()); QVERIFY(pic16.has_value());
    for (auto it = pic8->parameters().cbegin(); it != pic8->parameters().cend(); ++it) {
        QVERIFY(pic16->parameters().contains(it.key()));
        const auto other = pic16->parameters().value(it.key());
        QCOMPARE(other.name, it->name); QCOMPARE(other.channel, it->channel);
        QCOMPARE(other.unit, it->unit); QCOMPARE(other.scale, it->scale); QCOMPARE(other.access, it->access);
    }
}

void YamlConfigLoaderTests::documentedMetadata_data() {
    QTest::addColumn<quint16>("id"); QTest::addColumn<QString>("name"); QTest::addColumn<int>("channel");
    QTest::addColumn<QString>("unit"); QTest::addColumn<double>("scale"); QTest::addColumn<int>("access");
    const auto row = [](const char* label, quint16 id, const char* name, int channel,
                        const char* unit, double scale, ParameterAccess access) {
        QTest::newRow(label) << id << QString::fromLatin1(name) << channel << QString::fromLatin1(unit) << scale << int(access);
    };
    row("serial-not-model", 0x0701, "Serial number", -1, "", 1, ParameterAccess::ReadOnly);
    row("firmware", 0x0710, "Firmware version", -1, "", 1, ParameterAccess::ReadOnly);
    row("usb-address-storage-not-endpoint", 0x0720, "RS-485 address storage (USB parameter only)", -1, "", 1, ParameterAccess::ReadWrite);
    row("general-lock", 0x0800, "General lock status bitmask", -1, "", 1, ParameterAccess::ReadOnly);
    row("virtual-zero", 0x0001, "Channel zero state and memory commands", 0, "", 1, ParameterAccess::ReadWrite);
    row("current-limit", 0x0014, "Channel 1 current maximum limit", 1, "mA", 0.1, ParameterAccess::ReadOnly);
    row("voltage-setting", 0x0017, "Channel 1 voltage setting", 1, "V", 0.01, ParameterAccess::ReadWrite);
    row("state-command-and-readback", 0x0021, "Channel 2 state", 2, "", 1, ParameterAccess::ReadWrite);
    row("channel8-protection", 0x008B, "Channel 8 voltage protection value", 8, "V", 0.01, ParameterAccess::ReadWrite);
    row("channel9-measured", 0x0095, "Channel 9 measured current", 9, "mA", 0.1, ParameterAccess::ReadOnly);
    row("channel16-measured", 0x010A, "Channel 16 measured voltage", 16, "V", 0.01, ParameterAccess::ReadOnly);
    row("temperature-unit-unspecified", 0x0AF4, "PCB temperature", -1, "", 0.1, ParameterAccess::ReadOnly);
    row("input-voltage", 0x0408, "Measured input voltage", -1, "V", 0.1, ParameterAccess::ReadOnly);
}

void YamlConfigLoaderTests::documentedMetadata() {
    QFETCH(quint16, id); QFETCH(QString, name); QFETCH(int, channel);
    QFETCH(QString, unit); QFETCH(double, scale); QFETCH(int, access);
    const auto result = YamlConfigLoader::loadModel(QFINDTESTDATA("../config/devices/pic.yaml"), 0x1701);
    QVERIFY(result.has_value()); QVERIFY(result->parameters().contains(id));
    const auto definition = result->parameters().value(id);
    QCOMPARE(definition.name, name); QCOMPARE(definition.channel.has_value(), channel >= 0);
    if (channel >= 0) QCOMPARE(definition.channel.value(), quint8(channel));
    QCOMPARE(definition.unit, unit); QCOMPARE(definition.scale, scale); QCOMPARE(int(definition.access), access);
}

void YamlConfigLoaderTests::unknownModel_data() {
    QTest::addColumn<quint16>("id");
    QTest::newRow("zero") << quint16(0);
    QTest::newRow("decimal1700-is-not-hex1700") << quint16(1700);
    QTest::newRow("decimal1701-is-not-hex1701") << quint16(1701);
    QTest::newRow("unknown") << quint16(0x1702);
    QTest::newRow("serial-register-is-not-model-id") << quint16(0x0701);
}

void YamlConfigLoaderTests::unknownModel() {
    QFETCH(quint16, id); QString error;
    QVERIFY(!YamlConfigLoader::loadModel(QFINDTESTDATA("../config/devices/pic.yaml"), id, &error));
    QVERIFY2(error.contains(QStringLiteral("unsupported model ID")), qPrintable(error));
    const auto key = QStringLiteral("%1").arg(id, 4, 16, QLatin1Char('0')).toUpper();
    QVERIFY(error.contains(key));
}

void YamlConfigLoaderTests::catalogRuntime_data() { sharedCatalog_data(); }

void YamlConfigLoaderTests::catalogRuntime() {
    QFETCH(quint16, id); QFETCH(int, channels); QFETCH(int, count);
    const auto result = YamlConfigLoader::loadModel(QFINDTESTDATA("../config/devices/pic.yaml"), id);
    QVERIFY(result.has_value()); DeviceRepository repository(*result), other(*result);
    QVERIFY(!repository.value(0x0017).has_value());
    QVERIFY(repository.update(0x0017, 0)); QCOMPARE(repository.value(0x0017).value(), quint16(0));
    QVERIFY(repository.update(0x001A, 356)); QCOMPARE(repository.value(0x001A).value(), quint16(356));
    QVERIFY(!other.value(0x001A).has_value());
    repository.clear(); QVERIFY(!repository.value(0x001A).has_value());
    QCOMPARE(repository.configuration().channelCount(), channels);
    QCOMPARE(repository.configuration().parameters().size(), count);
    QCOMPARE(repository.configuration().parameters().value(0x001A).scale, 0.01);
}

void YamlConfigLoaderTests::catalogFailure_data() {
    QTest::addColumn<QByteArray>("yaml"); QTest::addColumn<QString>("field");
    const auto row = [](const char* name, const QByteArray& yaml, const char* field) {
        QTest::newRow(name) << yaml << QString::fromLatin1(field);
    };
    row("missing-models", "common: []", "models");
    row("models-type", "models: []\ncommon: []", "models");
    row("unquoted-model-id", catalog().replace("\"1700\"", "1700"), "models");
    row("short-model-id", catalog().replace("1700", "700"), "models");
    row("duplicate-model-id", catalog().replace("1701", "1700"), "models");
    row("selected-model-type", catalog().replace("PIC-8", "PIC-32"), ".model");
    row("common-type", catalog().left(catalog().indexOf("common:")) + "common: {}", "common");
    row("missing-common", catalog().left(catalog().indexOf("common:")), "common");
    row("selected-parameters-type", catalog().replace("PIC-8, parameters: []", "PIC-8, parameters: {}"), ".parameters");
    row("cross-list-duplicate", catalog().replace("PIC-8, parameters: []", "PIC-8, parameters: [{id: \"0017\", name: Duplicate, access: r}]"), "ID 0017");
    row("common-channel-out-of-model", catalog().replace("channel: 1", "channel: 9"), "ID 0017");
    row("selected-channel-out-of-model", catalog().replace("PIC-8, parameters: []", "PIC-8, parameters: [{id: \"009A\", name: Invalid, channel: 9, access: r}]"), "ID 009A");
}

void YamlConfigLoaderTests::catalogFailure() {
    QFETCH(QByteArray, yaml); QFETCH(QString, field); QString error;
    QVERIFY(!loadText(yaml, &error, quint16(0x1700)).has_value());
    QVERIFY2(error.contains(field), qPrintable(error));
}

void YamlConfigLoaderTests::explicitSelectionRequired() {
    QString error;
    QVERIFY(!YamlConfigLoader::loadFile(QFINDTESTDATA("../config/devices/pic.yaml"), &error));
    QVERIFY(error.contains(QStringLiteral("device")));
}

void YamlConfigLoaderTests::unselectedDefinitionsNotLoaded() {
    const auto yaml = catalog().replace("PIC-16, parameters: []",
        "PIC-16, parameters: [{id: \"010A\", name: Unselected, channel: 99, access: r}]");
    QString error;
    const auto result = loadText(yaml, &error, quint16(0x1700));
    QVERIFY2(result.has_value(), qPrintable(error)); QCOMPARE(result->parameters().size(), 1);
    QVERIFY(!result->parameters().contains(0x010A));
}

QTEST_GUILESS_MAIN(YamlConfigLoaderTests)
#include "tst_yamlconfigloader.moc"
