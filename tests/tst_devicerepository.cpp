#include "DeviceRepository.h"
#include "PICTextProtocol.h"

#include <QtTest>

namespace {
// Representative USB register IDs/units from v1.1.0; not a complete table.
DeviceConfiguration configuration(quint16 modelId, const QString& name, bool extra = false) {
    DeviceConfiguration result(modelId, name);
    const auto add = [&](quint16 id, const ParameterDefinition& definition) {
        if (!result.addParameter(id, definition)) qFatal("Invalid configuration fixture");
    };
    add(0x0017, {QStringLiteral("V"), 0.01});
    add(0x0021, {{}, 1.0});
    add(0x0015, {QStringLiteral("mA"), 0.1});
    add(0x001A, {QStringLiteral("V"), 0.01});
    add(0x0408, {QStringLiteral("V"), 0.1});
    if (extra) add(0x010A, {QStringLiteral("V"), 0.01});
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
    void arbitraryDefinitions();
    void receivedValuesStayRaw();
    void decodedValueCompatibility();
    void emptyConfiguration();
};

void DeviceRepositoryTests::models_data() {
    QTest::addColumn<quint16>("id"); QTest::addColumn<QString>("name"); QTest::addColumn<bool>("extra");
    QTest::newRow("PIC-8") << quint16(0x1700) << QStringLiteral("PIC-8") << false;
    QTest::newRow("PIC-16") << quint16(0x1701) << QStringLiteral("PIC-16") << true;
    QTest::newRow("generic") << quint16(0x2222) << QStringLiteral("Third model") << false;
}

void DeviceRepositoryTests::models() {
    QFETCH(quint16, id); QFETCH(QString, name); QFETCH(bool, extra);
    DeviceRepository repository(configuration(id, name, extra));
    QCOMPARE(repository.configuration().modelId(), id);
    QCOMPARE(repository.configuration().modelName(), name);
    QCOMPARE(repository.configuration().parameters().contains(0x010A), extra);
}

void DeviceRepositoryTests::representativeDefinitions() {
    const auto config = configuration(0x1700, QStringLiteral("PIC-8"));
    const auto setting = config.parameters().value(0x0017);
    QCOMPARE(setting.unit, QStringLiteral("V")); QCOMPARE(setting.scale, 0.01);
    const auto state = config.parameters().value(0x0021);
    QCOMPARE(state.scale, 1.0); QVERIFY(state.unit.isEmpty());
    const auto current = config.parameters().value(0x0015);
    QCOMPARE(current.unit, QStringLiteral("mA")); QCOMPARE(current.scale, 0.1);
    const auto voltage = config.parameters().value(0x001A);
    QCOMPARE(voltage.unit, QStringLiteral("V")); QCOMPARE(voltage.scale, 0.01);
    const auto input = config.parameters().value(0x0408);
    QCOMPARE(input.unit, QStringLiteral("V")); QCOMPARE(input.scale, 0.1);
}

void DeviceRepositoryTests::valuesInitiallyUnknown() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
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
    QFETCH(quint16, raw); DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(!repository.value(0x0017).has_value());
    QVERIFY(repository.update(0x0017, raw));
    QVERIFY(repository.value(0x0017).has_value()); QCOMPARE(repository.value(0x0017).value(), raw);
}

void DeviceRepositoryTests::independentParameters() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(0x0017, 900)); QVERIFY(repository.update(0x0021, 0x000C));
    QCOMPARE(repository.value(0x0017).value(), quint16(900)); QCOMPARE(repository.value(0x0021).value(), quint16(12));
    QVERIFY(repository.update(0x0017, 356));
    QCOMPARE(repository.value(0x0017).value(), quint16(356)); QCOMPARE(repository.value(0x0021).value(), quint16(12));
}

void DeviceRepositoryTests::undefinedParameterRejected() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(0x0017, 356));
    QVERIFY(!repository.update(0xBEEF, 1)); QVERIFY(!repository.value(0xBEEF).has_value());
    QVERIFY(!repository.configuration().parameters().contains(0xBEEF));
    QCOMPARE(repository.value(0x0017).value(), quint16(356));
}

void DeviceRepositoryTests::clearPreservesConfiguration() {
    DeviceRepository repository(configuration(0x1701, QStringLiteral("PIC-16"), true));
    QVERIFY(repository.update(0x0017, 356)); QVERIFY(repository.update(0x010A, 0));
    repository.clear(); repository.clear();
    QVERIFY(!repository.value(0x0017).has_value()); QVERIFY(!repository.value(0x010A).has_value());
    QCOMPARE(repository.configuration().modelId(), quint16(0x1701));
    QCOMPARE(repository.configuration().parameters().size(), 6);
    const auto definition = repository.configuration().parameters().value(0x010A);
    QCOMPARE(definition.unit, QStringLiteral("V"));
    QCOMPARE(definition.scale, 0.01);
    QVERIFY(repository.update(0x010A, 123)); QCOMPARE(repository.value(0x010A).value(), quint16(123));
}

void DeviceRepositoryTests::independentRepositories() {
    DeviceRepository pic8(configuration(0x1700, QStringLiteral("PIC-8"))), pic16(configuration(0x1701, QStringLiteral("PIC-16"), true));
    QVERIFY(pic8.update(0x0017, 356)); QVERIFY(!pic16.value(0x0017).has_value());
    QVERIFY(pic16.update(0x0017, 0)); QCOMPARE(pic8.value(0x0017).value(), quint16(356));
    QCOMPARE(pic16.value(0x0017).value(), quint16(0));
    pic8.clear(); QVERIFY(!pic8.value(0x0017).has_value());
    QCOMPARE(pic16.value(0x0017).value(), quint16(0));
    QVERIFY(!pic8.update(0x010A, 1)); QVERIFY(pic16.update(0x010A, 1));
}

void DeviceRepositoryTests::configurationSnapshotLifetime() {
    const auto makeRepository = [] {
        auto original = configuration(0x1700, QStringLiteral("PIC-8"));
        DeviceRepository repository(original);
        const ParameterDefinition extra{QStringLiteral("V"), 0.01};
        if (!original.addParameter(0x002A, extra)) qFatal("Invalid snapshot fixture");
        return repository;
    };
    auto repository = makeRepository(); // Source was modified, then destroyed.
    QCOMPARE(repository.configuration().parameters().size(), 5);
    QVERIFY(!repository.configuration().parameters().contains(0x002A));
    QVERIFY(!repository.update(0x002A, 123));
    QVERIFY(repository.update(0x0017, 356)); QCOMPARE(repository.value(0x0017).value(), quint16(356));
    DeviceRepository temporary(configuration(0x1701, QStringLiteral("PIC-16"), true));
    QCOMPARE(temporary.configuration().parameters().value(0x010A).scale, 0.01);
}

void DeviceRepositoryTests::duplicateDefinitionsRejected() {
    DeviceConfiguration config(0x1700, QStringLiteral("PIC-8"));
    const ParameterDefinition first{QStringLiteral("V"), 0.01};
    QVERIFY(config.addParameter(0x0017, first));
    auto replacement = first; replacement.scale = 2.0;
    QVERIFY(!config.addParameter(0x0017, replacement));
    QCOMPARE(config.parameters().size(), 1); QCOMPARE(config.parameters().value(0x0017).scale, first.scale);
}

void DeviceRepositoryTests::arbitraryDefinitions() {
    DeviceConfiguration generic(0x2222, QStringLiteral("Third model"));
    QVERIFY(generic.addParameter(0xBEEF, {QStringLiteral("mV"), 0.001, 3}));
    QVERIFY(generic.addParameter(0x0001, {}));
    QCOMPARE(generic.parameters().size(), 2);
    QCOMPARE(generic.parameters().value(0xBEEF).tolerance, quint8(3));
}

void DeviceRepositoryTests::receivedValuesStayRaw() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(0x001A, 356)); QCOMPARE(repository.value(0x001A).value(), quint16(356));
    QCOMPARE(repository.configuration().parameters().value(0x001A).scale, 0.01);
}

void DeviceRepositoryTests::decodedValueCompatibility() {
    const auto response = PICTextProtocol::decode("K0017 0164\r");
    QCOMPARE(response.kind, PICTextProtocol::Kind::ParameterValue);
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(response.parameter, response.value));
    QCOMPARE(repository.value(0x0017).value(), quint16(356));
}

void DeviceRepositoryTests::emptyConfiguration() {
    DeviceRepository repository(DeviceConfiguration{0x1700, QStringLiteral("PIC-8")});
    QVERIFY(repository.configuration().parameters().isEmpty());
    QVERIFY(!repository.update(0x0017, 0)); QVERIFY(!repository.value(0x0017).has_value()); repository.clear();
}

QTEST_GUILESS_MAIN(DeviceRepositoryTests)
#include "tst_devicerepository.moc"
