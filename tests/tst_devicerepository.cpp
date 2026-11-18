#include "DeviceRepository.h"
#include "PICTextProtocol.h"

#include <QtTest>
#include <cmath>
#include <limits>

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
    void receivedValuesAreProcessed();
    void decodedValueCompatibility();
    void emptyConfiguration();
    void processedValues_data();void processedValues();
    void inverseValues_data();void inverseValues();
    void boundaryNoise_data();void boundaryNoise();
    void invalidMetadata_data();void invalidMetadata();
    void failedConversionPreservesValue();void inverseDoesNotStore();
    void roundTrips_data();void roundTrips();void toleranceDoesNotRound();
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
        QVERIFY(!repository.value(it.key()).isValid());
}

void DeviceRepositoryTests::updateAndRetrieve_data() {
    QTest::addColumn<quint16>("raw");
    QTest::newRow("legitimate-zero") << quint16(0);
    QTest::newRow("representative-value") << quint16(356);
    QTest::newRow("all-16-bits") << quint16(0xffff);
}

void DeviceRepositoryTests::updateAndRetrieve() {
    QFETCH(quint16, raw); DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(!repository.value(0x0017).isValid());
    QVERIFY(repository.update(0x0017, raw));
    QVERIFY(repository.update(0x0017, raw)); // An unchanged valid update is still accepted.
    QVERIFY(repository.value(0x0017).isValid()); QCOMPARE(repository.value(0x0017).toDouble(), double(raw) * 0.01);
}

void DeviceRepositoryTests::independentParameters() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(0x0017, 900)); QVERIFY(repository.update(0x0021, 0x000C));
    QCOMPARE(repository.value(0x0017).toDouble(), 9.0); QCOMPARE(repository.value(0x0021).toUInt(), uint(12));
    QVERIFY(repository.update(0x0017, 356));
    QCOMPARE(repository.value(0x0017).toDouble(), 3.56); QCOMPARE(repository.value(0x0021).toUInt(), uint(12));
}

void DeviceRepositoryTests::undefinedParameterRejected() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(0x0017, 356));
    QVERIFY(!repository.update(0xBEEF, 1)); QVERIFY(!repository.value(0xBEEF).isValid());
    QVERIFY(!repository.configuration().parameters().contains(0xBEEF));
    QCOMPARE(repository.value(0x0017).toDouble(), 3.56);
}

void DeviceRepositoryTests::clearPreservesConfiguration() {
    DeviceRepository repository(configuration(0x1701, QStringLiteral("PIC-16"), true));
    QVERIFY(repository.update(0x0017, 356)); QVERIFY(repository.update(0x010A, 0));
    repository.clear(); repository.clear();
    QVERIFY(!repository.value(0x0017).isValid()); QVERIFY(!repository.value(0x010A).isValid());
    QCOMPARE(repository.configuration().modelId(), quint16(0x1701));
    QCOMPARE(repository.configuration().parameters().size(), 6);
    const auto definition = repository.configuration().parameters().value(0x010A);
    QCOMPARE(definition.unit, QStringLiteral("V"));
    QCOMPARE(definition.scale, 0.01);
    QVERIFY(repository.update(0x010A, 123)); QCOMPARE(repository.value(0x010A).toDouble(), 1.23);
}

void DeviceRepositoryTests::independentRepositories() {
    DeviceRepository pic8(configuration(0x1700, QStringLiteral("PIC-8"))), pic16(configuration(0x1701, QStringLiteral("PIC-16"), true));
    QVERIFY(pic8.update(0x0017, 356)); QVERIFY(!pic16.value(0x0017).isValid());
    QVERIFY(pic16.update(0x0017, 0)); QCOMPARE(pic8.value(0x0017).toDouble(), 3.56);
    QCOMPARE(pic16.value(0x0017).toDouble(), 0.0);
    pic8.clear(); QVERIFY(!pic8.value(0x0017).isValid());
    QCOMPARE(pic16.value(0x0017).toDouble(), 0.0);
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
    QVERIFY(repository.update(0x0017, 356)); QCOMPARE(repository.value(0x0017).toDouble(), 3.56);
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

void DeviceRepositoryTests::receivedValuesAreProcessed() {
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(0x001A, 356)); QCOMPARE(repository.value(0x001A).toDouble(), 3.56);
    QCOMPARE(repository.configuration().parameters().value(0x001A).scale, 0.01);
}

void DeviceRepositoryTests::decodedValueCompatibility() {
    const auto response = PICTextProtocol::decode("K0017 0164\r");
    QCOMPARE(response.kind, PICTextProtocol::Kind::ParameterValue);
    QCOMPARE(response.value, quint16(356)); // Communication still returns the raw wire value.
    DeviceRepository repository(configuration(0x1700, QStringLiteral("PIC-8")));
    QVERIFY(repository.update(response.parameter, response.value));
    QCOMPARE(repository.value(0x0017).toDouble(), 3.56);
}

void DeviceRepositoryTests::emptyConfiguration() {
    DeviceRepository repository(DeviceConfiguration{0x1700, QStringLiteral("PIC-8")});
    QVERIFY(repository.configuration().parameters().isEmpty());
    QVERIFY(!repository.update(0x0017, 0)); QVERIFY(!repository.value(0x0017).isValid()); repository.clear();
}

namespace {
constexpr quint16 convertedCode = 0xA100;
constexpr quint16 unchangedCode = 0xA101;
DeviceRepository numericRepository(double scale, bool isSigned, quint8 tolerance = 0) {
    DeviceConfiguration model(0x2222, QStringLiteral("Synthetic numeric model"));
    if (!model.addParameter(convertedCode, {QStringLiteral("u"), scale, tolerance, isSigned})
        || !model.addParameter(unchangedCode, {})) qFatal("Invalid numeric fixture");
    return DeviceRepository(model);
}
}

void DeviceRepositoryTests::processedValues_data() {
    QTest::addColumn<quint16>("raw"); QTest::addColumn<bool>("isSigned");
    QTest::addColumn<double>("scale"); QTest::addColumn<double>("physical");
    QTest::addColumn<int>("type");
    const auto row = [](const char* name, quint16 raw, bool sign, double scale, double physical, int type) {
        QTest::newRow(name) << raw << sign << scale << physical << type;
    };
    row("voltage", 356, false, 0.01, 3.56, QMetaType::Double);
    row("tenths", 130, false, 0.1, 13.0, QMetaType::Double);
    row("unsigned-zero", 0, false, 1.0, 0.0, QMetaType::UInt);
    row("unsigned-full-mask", 0xffff, false, 1.0, 65535.0, QMetaType::UInt);
    row("unsigned-high-bit", 0x8000, false, 1.0, 32768.0, QMetaType::UInt);
    row("signed-zero", 0, true, 1.0, 0.0, QMetaType::Int);
    row("signed-positive", 123, true, 1.0, 123.0, QMetaType::Int);
    row("signed-upper-bound", 0x7fff, true, 1.0, 32767.0, QMetaType::Int);
    row("signed-lower-bound", 0x8000, true, 1.0, -32768.0, QMetaType::Int);
    row("signed-minus-one", 0xffff, true, 1.0, -1.0, QMetaType::Int);
    row("signed-tenths", 0xff9c, true, 0.1, -10.0, QMetaType::Double);
    row("unsigned-negative-scale", 20, false, -0.25, -5.0, QMetaType::Double);
    row("signed-negative-scale", 0xffec, true, -0.25, 5.0, QMetaType::Double);
    row("scaled-zero-stays-double", 0, false, 0.01, 0.0, QMetaType::Double);
}

void DeviceRepositoryTests::processedValues() {
    QFETCH(quint16, raw); QFETCH(bool, isSigned); QFETCH(double, scale);
    QFETCH(double, physical); QFETCH(int, type);
    auto repository = numericRepository(scale, isSigned);
    QVERIFY(!repository.value(convertedCode).isValid());
    QVERIFY(repository.update(convertedCode, raw));
    const auto stored = repository.value(convertedCode);
    QVERIFY(stored.isValid());
    QCOMPARE(stored.metaType().id(), type);
    QCOMPARE(stored.toDouble(), physical);
    QVERIFY(repository.update(convertedCode, raw));
    QCOMPARE(repository.value(convertedCode), stored);
    QVERIFY(!repository.value(0xBEEF).isValid());
}

void DeviceRepositoryTests::inverseValues_data() {
    QTest::addColumn<bool>("isSigned"); QTest::addColumn<double>("scale");
    QTest::addColumn<double>("physical"); QTest::addColumn<bool>("accepted");
    QTest::addColumn<quint16>("payload");
    const auto row = [](const char* name, bool sign, double scale, double value, bool ok, quint16 payload) {
        QTest::newRow(name) << sign << scale << value << ok << payload;
    };
    row("zero", false, 1, 0, true, 0);
    row("unsigned-bound", false, 1, 65535, true, 0xffff);
    row("signed-min", true, 1, -32768, true, 0x8000);
    row("signed-max", true, 1, 32767, true, 0x7fff);
    row("negative-one", true, 1, -1, true, 0xffff);
    row("measurement", false, 0.01, 3.56, true, 356);
    row("physical-value", false, 0.01, 9, true, 900);
    row("positive-half-away", true, 1, 2.5, true, 3);
    row("negative-half-away", true, 1, -2.5, true, 0xfffd);
    row("fraction-below-half", true, 0.25, -2.375 + 0.01, true, 0xfff7);
    row("scaled-negative-half", true, 0.25, -2.375, true, 0xfff6);
    row("negative-scale", false, -0.1, -13, true, 130);
    row("negative-scale-signed", true, -0.25, 2.375, true, 0xfff6);
    row("negative-scale-wrong-sign", false, -0.1, 1, false, 0);
    row("unsigned-small-negative-rejected-before-round", false, 1, -0.25, false, 0);
    row("unsigned-small-overflow-rejected-before-round", false, 1, 65535.25, false, 0);
    row("unsigned-low-half-cell-rejected", false, 1, -0.49, false, 0);
    row("unsigned-high-half-cell-rejected", false, 1, 65535.49, false, 0);
    row("signed-small-underflow-rejected-before-round", true, 1, -32768.25, false, 0);
    row("signed-small-overflow-rejected-before-round", true, 1, 32767.25, false, 0);
    row("signed-overflow", true, 1, 32768, false, 0);
    row("signed-underflow", true, 1, -32769, false, 0);
    row("unsigned-overflow", false, 1, 65536, false, 0);
    row("unsigned-underflow", false, 1, -1, false, 0);
    row("nan-input", false, 1, std::numeric_limits<double>::quiet_NaN(), false, 0);
    row("positive-infinite-input", true, 1, std::numeric_limits<double>::infinity(), false, 0);
    row("negative-infinite-input", true, -1, -std::numeric_limits<double>::infinity(), false, 0);
    row("finite-operands-infinite-quotient", false, std::numeric_limits<double>::min(), std::numeric_limits<double>::max(), false, 0);
}

void DeviceRepositoryTests::inverseValues() {
    QFETCH(bool, isSigned); QFETCH(double, scale); QFETCH(double, physical);
    QFETCH(bool, accepted); QFETCH(quint16, payload);
    const auto repository = numericRepository(scale, isSigned);
    const auto result = repository.toRaw(convertedCode, physical);
    QCOMPARE(result.has_value(), accepted);
    if (accepted) QCOMPARE(*result, payload);
    QVERIFY(!repository.value(convertedCode).isValid());
}

void DeviceRepositoryTests::boundaryNoise_data() {
    QTest::addColumn<bool>("isSigned"); QTest::addColumn<double>("boundary");
    QTest::addColumn<double>("direction"); QTest::addColumn<quint16>("payload");
    QTest::newRow("unsigned-lower") << false << 0.0 << -1.0 << quint16(0);
    QTest::newRow("unsigned-upper") << false << 65535.0 << 1.0 << quint16(0xffff);
    QTest::newRow("signed-lower") << true << -32768.0 << -1.0 << quint16(0x8000);
    QTest::newRow("signed-upper") << true << 32767.0 << 1.0 << quint16(0x7fff);
}

void DeviceRepositoryTests::boundaryNoise() {
    QFETCH(bool, isSigned); QFETCH(double, boundary); QFETCH(double, direction); QFETCH(quint16, payload);
    const auto repository = numericRepository(1, isSigned);
    const double outside = std::nextafter(boundary, direction * std::numeric_limits<double>::infinity());
    const auto exact = repository.toRaw(convertedCode, boundary);
    const auto arithmeticNoise = repository.toRaw(convertedCode, outside);
    QVERIFY(exact); QCOMPARE(*exact, payload);
    QVERIFY(arithmeticNoise); QCOMPARE(*arithmeticNoise, payload);
    QVERIFY(!repository.toRaw(convertedCode, boundary + direction * 1e-6));
}

void DeviceRepositoryTests::invalidMetadata_data() {
    QTest::addColumn<double>("scale");
    QTest::newRow("zero-scale") << 0.0;
    QTest::newRow("negative-zero-scale") << -0.0;
    QTest::newRow("nan-scale") << std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("infinite-scale") << std::numeric_limits<double>::infinity();
    QTest::newRow("negative-infinite-scale") << -std::numeric_limits<double>::infinity();
}

void DeviceRepositoryTests::invalidMetadata() {
    QFETCH(double, scale);
    for (bool sign : {false, true}) {
        auto repository = numericRepository(scale, sign);
        QVERIFY(repository.update(unchangedCode, 42));
        QVERIFY(!repository.update(convertedCode, 0));
        QVERIFY(!repository.update(convertedCode, 0xffff));
        QVERIFY(!repository.value(convertedCode).isValid());
        QCOMPARE(repository.value(unchangedCode).toUInt(), uint(42));
        QVERIFY(!repository.toRaw(convertedCode, 0));
        QVERIFY(!repository.toRaw(convertedCode, 1));
    }
}

void DeviceRepositoryTests::failedConversionPreservesValue() {
    const double largest = std::numeric_limits<double>::max();
    auto repository = numericRepository(largest, false);
    QVERIFY(repository.update(convertedCode, 1));
    const auto saved = repository.value(convertedCode);
    QVERIFY(saved.isValid()); QCOMPARE(saved.toDouble(), largest);
    QVERIFY(!repository.update(convertedCode, 2)); // Multiplication overflows; last value remains intact.
    QCOMPARE(repository.value(convertedCode), saved);
    QVERIFY(!repository.update(0xBEEF, 0));
    QCOMPARE(repository.value(convertedCode), saved);
    auto signedRepository = numericRepository(-largest, true);
    QVERIFY(signedRepository.update(convertedCode, 0xffff));
    const auto signedSaved = signedRepository.value(convertedCode);
    QVERIFY(!signedRepository.update(convertedCode, 0x8000));
    QCOMPARE(signedRepository.value(convertedCode), signedSaved);
    auto tinyRepository = numericRepository(std::numeric_limits<double>::denorm_min(), false);
    QVERIFY(tinyRepository.update(convertedCode, 1));
    QVERIFY(tinyRepository.value(convertedCode).toDouble() == std::numeric_limits<double>::denorm_min());
    const auto tinyPayload = tinyRepository.toRaw(convertedCode, tinyRepository.value(convertedCode).toDouble());
    QVERIFY(tinyPayload); QCOMPARE(*tinyPayload, quint16(1));
}

void DeviceRepositoryTests::inverseDoesNotStore() {
    auto repository = numericRepository(0.1, true);
    const DeviceRepository& view = repository;
    const auto raw = view.toRaw(convertedCode, -10);
    QVERIFY(raw); QCOMPARE(*raw, quint16(0xff9c));
    QVERIFY(!view.value(convertedCode).isValid());
    QVERIFY(!view.toRaw(0xBEEF, 0));
    QVERIFY(repository.update(convertedCode, 130));
    const auto before = repository.value(convertedCode);
    QVERIFY(view.toRaw(convertedCode, 5.5));
    QVERIFY(!view.toRaw(convertedCode, std::numeric_limits<double>::infinity()));
    QCOMPARE(repository.value(convertedCode), before);
    repository.clear();
    QVERIFY(!repository.value(convertedCode).isValid());
    const auto metadata = repository.configuration().parameters().value(convertedCode);
    QCOMPARE(metadata.scale, 0.1); QVERIFY(metadata.isSigned);
    QVERIFY(view.toRaw(convertedCode, -10));
    QVERIFY(!view.value(convertedCode).isValid());
}

void DeviceRepositoryTests::roundTrips_data() {
    QTest::addColumn<double>("scale"); QTest::addColumn<bool>("isSigned");
    for (bool sign : {false, true}) {
        for (double scale : {1.0, 0.003, 0.1, -0.01, -0.25}) {
            const auto name = QStringLiteral("%1/%2").arg(sign ? "signed" : "unsigned").arg(scale);
            QTest::newRow(qPrintable(name)) << scale << sign;
        }
    }
}

void DeviceRepositoryTests::roundTrips() {
    QFETCH(double, scale); QFETCH(bool, isSigned);
    auto repository = numericRepository(scale, isSigned);
    for (unsigned word = 0; word < 65536; ++word) {
        if (!repository.update(convertedCode, quint16(word))) QFAIL("Valid raw update rejected");
        const auto stored = repository.value(convertedCode);
        const auto raw = repository.toRaw(convertedCode, stored.toDouble());
        if (!stored.isValid() || !raw || *raw != word)
            QFAIL(qPrintable(QStringLiteral("Round trip failed at raw %1").arg(word)));
    }
}

void DeviceRepositoryTests::toleranceDoesNotRound() {
    for (quint8 precision : {quint8(0), quint8(2), quint8(255)}) {
        auto repository = numericRepository(0.01, true, precision);
        QVERIFY(repository.update(convertedCode, 356));
        QCOMPARE(repository.value(convertedCode).toDouble(), 3.56);
        QVERIFY(repository.update(convertedCode, 0xff85));
        QCOMPARE(repository.value(convertedCode).toDouble(), -1.23);
        const auto raw = repository.toRaw(convertedCode, -1.23);
        QVERIFY(raw); QCOMPARE(*raw, quint16(0xff85));
        QCOMPARE(repository.configuration().parameters().value(convertedCode).tolerance, precision);
    }
}

QTEST_GUILESS_MAIN(DeviceRepositoryTests)
#include "tst_devicerepository.moc"
