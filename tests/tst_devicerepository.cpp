#include "DeviceRepository.h"
#include "PICTextProtocol.h"

#include <QtTest>

namespace {
// USB identifiers/access/units: v1.1.0 pp16-17,23; not a complete register table.
DeviceConfiguration configuration(PICModel model) {
    DeviceConfiguration result(model);
    const auto add = [&](quint16 id, const ParameterDefinition& definition) {
        if (!result.addParameter(id, definition)) qFatal("Invalid configuration fixture");
    };
    add(0x0017, {QStringLiteral("Channel 1 voltage setting"), quint8(1), QStringLiteral("V"), 0.01, ParameterAccess::ReadWrite});
    // W state commands (p17) and R state bitmask (p23/p24) share the same ID.
    add(0x0021, {QStringLiteral("Channel 2 state"), quint8(2), {}, 1.0, ParameterAccess::ReadWrite});
    add(0x0015, {QStringLiteral("Channel 1 measured current"), quint8(1), QStringLiteral("mA"), 0.1, ParameterAccess::ReadOnly});
    add(0x001A, {QStringLiteral("Channel 1 measured voltage"), quint8(1), QStringLiteral("V"), 0.01, ParameterAccess::ReadOnly});
    add(0x0408, {QStringLiteral("Measured input voltage"), std::nullopt, QStringLiteral("V"), 0.1, ParameterAccess::ReadOnly});
    if (model == PICModel::PIC16)
        add(0x010A, {QStringLiteral("Channel 16 measured voltage"), quint8(16), QStringLiteral("V"), 0.01, ParameterAccess::ReadOnly});
    return result;
}
}

class DeviceRepositoryTests : public QObject {
    Q_OBJECT
private slots:
    void models_data();
    void models();
    void representativeDefinitions();
    void valuesInitiallyUnknown();
    void updateAndRetrieve_data();
    void updateAndRetrieve();
    void independentParameters();
    void undefinedParameterRejected();
    void clearPreservesConfiguration();
    void independentRepositories();
    void configurationSnapshotLifetime();
    void duplicateDefinitionsRejected();
    void channelAssociations();
    void readOnlyValuesStayRaw();
    void decodedValueCompatibility();
    void emptyConfiguration();
};

void DeviceRepositoryTests::models_data() {
    QTest::addColumn<int>("model"); QTest::addColumn<int>("channels");
    QTest::newRow("PIC-8") << int(PICModel::PIC8) << 8;
    QTest::newRow("PIC-16") << int(PICModel::PIC16) << 16;
}

void DeviceRepositoryTests::models() {
    QFETCH(int, model); QFETCH(int, channels);
    DeviceRepository repository(configuration(PICModel(model)));
    QCOMPARE(repository.configuration().model(), PICModel(model));
    QCOMPARE(repository.configuration().channelCount(), channels);
    QCOMPARE(repository.configuration().parameters().contains(0x010A), channels == 16);
}

void DeviceRepositoryTests::representativeDefinitions() {
    const auto config = configuration(PICModel::PIC8);
    const auto setting = config.parameters().value(0x0017);
    QCOMPARE(setting.name, QStringLiteral("Channel 1 voltage setting"));
    QCOMPARE(setting.channel.value(), quint8(1)); QCOMPARE(setting.unit, QStringLiteral("V"));
    QCOMPARE(setting.scale, 0.01); QCOMPARE(setting.access, ParameterAccess::ReadWrite);
    const auto state = config.parameters().value(0x0021);
    QCOMPARE(state.channel.value(), quint8(2)); QCOMPARE(state.scale, 1.0);
    QVERIFY(state.unit.isEmpty()); QCOMPARE(state.access, ParameterAccess::ReadWrite);
    const auto current = config.parameters().value(0x0015);
    QCOMPARE(current.unit, QStringLiteral("mA")); QCOMPARE(current.scale, 0.1);
    QCOMPARE(current.access, ParameterAccess::ReadOnly);
    const auto voltage = config.parameters().value(0x001A);
    QCOMPARE(voltage.unit, QStringLiteral("V")); QCOMPARE(voltage.scale, 0.01);
    QCOMPARE(voltage.access, ParameterAccess::ReadOnly);
    const auto input = config.parameters().value(0x0408);
    QVERIFY(!input.channel.has_value()); QCOMPARE(input.unit, QStringLiteral("V"));
    QCOMPARE(input.scale, 0.1); QCOMPARE(input.access, ParameterAccess::ReadOnly);
}

void DeviceRepositoryTests::valuesInitiallyUnknown() {
    DeviceRepository repository(configuration(PICModel::PIC8));
    QCOMPARE(repository.configuration().parameters().size(), 5);
    for (auto it = repository.configuration().parameters().cbegin(); it != repository.configuration().parameters().cend(); ++it)
        QVERIFY(!repository.value(it.key()).has_value());
}

void DeviceRepositoryTests::updateAndRetrieve_data() {
    QTest::addColumn<quint16>("raw");
    QTest::newRow("legitimate-zero") << quint16(0);
    QTest::newRow("representative-value") << quint16(356);
    QTest::newRow("all-16-bits") << quint16(0xffff);
}

void DeviceRepositoryTests::updateAndRetrieve() {
    QFETCH(quint16, raw); DeviceRepository repository(configuration(PICModel::PIC8));
    QVERIFY(!repository.value(0x0017).has_value());
    QVERIFY(repository.update(0x0017, raw));
    QVERIFY(repository.value(0x0017).has_value()); QCOMPARE(repository.value(0x0017).value(), raw);
}

void DeviceRepositoryTests::independentParameters() {
    DeviceRepository repository(configuration(PICModel::PIC8));
    QVERIFY(repository.update(0x0017, 900)); QVERIFY(repository.update(0x0021, 0x000C));
    QCOMPARE(repository.value(0x0017).value(), quint16(900)); QCOMPARE(repository.value(0x0021).value(), quint16(12));
    QVERIFY(repository.update(0x0017, 356));
    QCOMPARE(repository.value(0x0017).value(), quint16(356)); QCOMPARE(repository.value(0x0021).value(), quint16(12));
}

void DeviceRepositoryTests::undefinedParameterRejected() {
    DeviceRepository repository(configuration(PICModel::PIC8));
    QVERIFY(repository.update(0x0017, 356));
    QVERIFY(!repository.update(0xBEEF, 1)); QVERIFY(!repository.value(0xBEEF).has_value());
    QVERIFY(!repository.configuration().parameters().contains(0xBEEF));
    QCOMPARE(repository.value(0x0017).value(), quint16(356));
}

void DeviceRepositoryTests::clearPreservesConfiguration() {
    DeviceRepository repository(configuration(PICModel::PIC16));
    QVERIFY(repository.update(0x0017, 356)); QVERIFY(repository.update(0x010A, 0));
    repository.clear(); repository.clear();
    QVERIFY(!repository.value(0x0017).has_value()); QVERIFY(!repository.value(0x010A).has_value());
    QCOMPARE(repository.configuration().model(), PICModel::PIC16);
    QCOMPARE(repository.configuration().channelCount(), 16); QCOMPARE(repository.configuration().parameters().size(), 6);
    const auto definition = repository.configuration().parameters().value(0x010A);
    QCOMPARE(definition.name, QStringLiteral("Channel 16 measured voltage"));
    QCOMPARE(definition.channel.value(), quint8(16)); QCOMPARE(definition.unit, QStringLiteral("V"));
    QCOMPARE(definition.scale, 0.01); QCOMPARE(definition.access, ParameterAccess::ReadOnly);
    QVERIFY(repository.update(0x010A, 123)); QCOMPARE(repository.value(0x010A).value(), quint16(123));
}

void DeviceRepositoryTests::independentRepositories() {
    DeviceRepository pic8(configuration(PICModel::PIC8)), pic16(configuration(PICModel::PIC16));
    QVERIFY(pic8.update(0x0017, 356)); QVERIFY(!pic16.value(0x0017).has_value());
    QVERIFY(pic16.update(0x0017, 0)); QCOMPARE(pic8.value(0x0017).value(), quint16(356));
    QCOMPARE(pic16.value(0x0017).value(), quint16(0));
    pic8.clear(); QVERIFY(!pic8.value(0x0017).has_value());
    QCOMPARE(pic16.value(0x0017).value(), quint16(0));
    QVERIFY(!pic8.update(0x010A, 1)); QVERIFY(pic16.update(0x010A, 1));
}

void DeviceRepositoryTests::configurationSnapshotLifetime() {
    const auto makeRepository = [] {
        auto original = configuration(PICModel::PIC8);
        DeviceRepository repository(original);
        const ParameterDefinition extra{QStringLiteral("Channel 2 measured voltage"), quint8(2), QStringLiteral("V"), 0.01, ParameterAccess::ReadOnly};
        if (!original.addParameter(0x002A, extra)) qFatal("Invalid snapshot fixture");
        return repository;
    };
    auto repository = makeRepository(); // Source was modified, then destroyed.
    QCOMPARE(repository.configuration().parameters().size(), 5);
    QVERIFY(!repository.configuration().parameters().contains(0x002A));
    QVERIFY(!repository.update(0x002A, 123));
    QVERIFY(repository.update(0x0017, 356)); QCOMPARE(repository.value(0x0017).value(), quint16(356));
    DeviceRepository temporary(configuration(PICModel::PIC16));
    QCOMPARE(temporary.configuration().parameters().value(0x010A).channel.value(), quint8(16));
}

void DeviceRepositoryTests::duplicateDefinitionsRejected() {
    DeviceConfiguration config(PICModel::PIC8);
    const ParameterDefinition first{QStringLiteral("Channel 1 voltage setting"), quint8(1), QStringLiteral("V"), 0.01, ParameterAccess::ReadWrite};
    QVERIFY(config.addParameter(0x0017, first));
    auto replacement = first; replacement.name = QStringLiteral("Replacement");
    QVERIFY(!config.addParameter(0x0017, replacement));
    QCOMPARE(config.parameters().size(), 1); QCOMPARE(config.parameters().value(0x0017).name, first.name);
}

void DeviceRepositoryTests::channelAssociations() {
    const ParameterDefinition channel16{QStringLiteral("Channel 16 measured voltage"), quint8(16), QStringLiteral("V"), 0.01, ParameterAccess::ReadOnly};
    DeviceConfiguration pic8(PICModel::PIC8), pic16(PICModel::PIC16);
    QVERIFY(!pic8.addParameter(0x010A, channel16)); QVERIFY(pic8.parameters().isEmpty());
    QVERIFY(pic16.addParameter(0x010A, channel16));
    auto invalid = channel16; invalid.channel = quint8(17);
    DeviceConfiguration invalidConfig(PICModel::PIC16);
    QVERIFY(!invalidConfig.addParameter(0x010A, invalid)); QVERIFY(invalidConfig.parameters().isEmpty());
    QVERIFY(pic8.addParameter(0x0001, {QStringLiteral("Channel zero state"), quint8(0), {}, 1.0, ParameterAccess::ReadWrite}));
}

void DeviceRepositoryTests::readOnlyValuesStayRaw() {
    DeviceRepository repository(configuration(PICModel::PIC8));
    QCOMPARE(repository.configuration().parameters().value(0x001A).access, ParameterAccess::ReadOnly);
    QVERIFY(repository.update(0x001A, 356)); QCOMPARE(repository.value(0x001A).value(), quint16(356));
    QCOMPARE(repository.configuration().parameters().value(0x001A).scale, 0.01);
}

void DeviceRepositoryTests::decodedValueCompatibility() {
    const auto response = PICTextProtocol::decode("K0017 0164\r");
    QCOMPARE(response.kind, PICTextProtocol::Kind::ParameterValue);
    DeviceRepository repository(configuration(PICModel::PIC8));
    QVERIFY(repository.update(response.parameter, response.value));
    QCOMPARE(repository.value(0x0017).value(), quint16(356));
}

void DeviceRepositoryTests::emptyConfiguration() {
    DeviceRepository repository(DeviceConfiguration{PICModel::PIC8});
    QVERIFY(repository.configuration().parameters().isEmpty());
    QVERIFY(!repository.update(0x0017, 0)); QVERIFY(!repository.value(0x0017).has_value()); repository.clear();
}

QTEST_GUILESS_MAIN(DeviceRepositoryTests)
#include "tst_devicerepository.moc"
