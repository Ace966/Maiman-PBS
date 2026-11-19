#include "DeviceFactory.h"
#include "XmlConfigLoader.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

namespace {
QString catalog() {
    return QFINDTESTDATA("../config/device_db.xml");
}

QByteArray minimal() {
    return "<DB><Device id=\"2222\" name=\"Synthetic\"><Commands>"
           "<Command code=\"0042\" unit=\"V\" divider=\"10\" tol=\"2\"/>"
           "</Commands></Device></DB>";
}

QString writeXml(const QTemporaryDir& directory, const QByteArray& xml) {
    QFile file(directory.filePath(QStringLiteral("fixture.xml")));
    if (!file.open(QIODevice::WriteOnly) || file.write(xml) != xml.size())
        qFatal("Cannot write factory fixture: %s", qPrintable(file.errorString()));
    return file.fileName();
}
}

class DeviceFactoryTests : public QObject {
    Q_OBJECT
private slots:
    void catalogModels_data();
    void catalogModels();
    void independentRepositories_data();
    void independentRepositories();
    void ownershipAndAffinity();
    void failures_data();
    void failures();
    void completeDatabaseValidation();
    void retryAndOptionalDiagnostic();
    void receivedValueFlow();
    void signedParameter_data();
    void signedParameter();
};

void DeviceFactoryTests::catalogModels_data() {
    QTest::addColumn<quint16>("id");
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("count");
    QTest::newRow("PIC-8") << quint16(0x1700) << QStringLiteral("PIC-8") << 97;
    QTest::newRow("PIC-16") << quint16(0x1701) << QStringLiteral("PIC-16") << 185;
}

void DeviceFactoryTests::catalogModels() {
    QFETCH(quint16, id);
    QFETCH(QString, name);
    QFETCH(int, count);
    const QString filename = catalog();
    QString error = QStringLiteral("old error");
    const auto repository = DeviceFactory::create(filename, id, &error);
    QVERIFY2(repository, qPrintable(error));
    QVERIFY(error.isEmpty());
    const auto& configuration = repository->configuration();
    QCOMPARE(configuration.modelId(), id);
    QCOMPARE(configuration.modelName(), name);
    QCOMPARE(configuration.parameters().size(), count);
    const auto expected = XmlConfigLoader::loadModel(filename, id);
    QVERIFY(expected);
    for (auto it = expected->parameters().cbegin(); it != expected->parameters().cend(); ++it) {
        QVERIFY(configuration.parameters().contains(it.key()));
        const auto actual = configuration.parameters().value(it.key());
        QCOMPARE(actual.unit, it->unit);
        QCOMPARE(actual.scale, it->scale);
        QCOMPARE(actual.tolerance, it->tolerance);
        QCOMPARE(actual.isSigned, it->isSigned);
        QVERIFY(!repository->value(it.key()).isValid());
    }
    const auto voltage = configuration.parameters().value(0x0017);
    QCOMPARE(voltage.unit, QStringLiteral("V"));
    QCOMPARE(voltage.scale, 0.01);
    QCOMPARE(voltage.tolerance, quint8(2));
    QVERIFY(!voltage.isSigned);
    const auto current = configuration.parameters().value(0x0015);
    QCOMPARE(current.unit, QStringLiteral("mA"));
    QCOMPARE(current.scale, 0.1);
    QCOMPARE(current.tolerance, quint8(1));
    QCOMPARE(configuration.parameters().contains(0x010A), id == 0x1701);
    QCOMPARE(configuration.parameters().contains(0x0091), id == 0x1701);
    QVERIFY(configuration.parameters().contains(0x0701));
    QVERIFY(!repository->value(0xBEEF).isValid());
}

void DeviceFactoryTests::independentRepositories_data() {
    QTest::addColumn<quint16>("secondId");
    QTest::newRow("same-model") << quint16(0x1700);
    QTest::newRow("different-models") << quint16(0x1701);
}

void DeviceFactoryTests::independentRepositories() {
    QFETCH(quint16, secondId);
    const auto first = DeviceFactory::create(catalog(), 0x1700);
    const auto second = DeviceFactory::create(catalog(), secondId);
    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(first.get() != second.get());
    QCOMPARE(first->configuration().modelId(), quint16(0x1700));
    QCOMPARE(first->configuration().parameters().size(), 97);
    QCOMPARE(second->configuration().modelId(), secondId);
    QCOMPARE(second->configuration().parameters().size(), secondId == 0x1700 ? 97 : 185);
    QSignalSpy firstChanged(first.get(), &DeviceRepository::propertyChanged);
    QSignalSpy secondChanged(second.get(), &DeviceRepository::propertyChanged);
    QSignalSpy secondCleared(second.get(), &DeviceRepository::cleared);
    QVERIFY(firstChanged.isValid());
    QVERIFY(secondChanged.isValid());
    QVERIFY(secondCleared.isValid());
    QVERIFY(first->update(0x0017, 356));
    QCOMPARE(firstChanged.count(), 1);
    QVERIFY(secondChanged.isEmpty());
    QVERIFY(!second->value(0x0017).isValid());
    QVERIFY(second->update(0x0017, 0));
    QCOMPARE(first->value(0x0017).toDouble(), 3.56);
    first->clear();
    QVERIFY(!first->value(0x0017).isValid());
    QVERIFY(second->value(0x0017).isValid());
    QCOMPARE(second->value(0x0017).toDouble(), 0.0);
    QCOMPARE(secondChanged.count(), 1);
    QVERIFY(secondCleared.isEmpty());
    QCOMPARE(first->configuration().parameters().size(), 97);
}

void DeviceFactoryTests::ownershipAndAffinity() {
    QPointer<DeviceRepository> destroyedWithOwner;
    const auto survivor = DeviceFactory::create(catalog(), 0x1701);
    QVERIFY(survivor);
    {
        const auto owner = DeviceFactory::create(catalog(), 0x1700);
        QVERIFY(owner);
        QVERIFY(!owner->parent());
        QCOMPARE(owner->thread(), QThread::currentThread());
        destroyedWithOwner = owner.get();
        QVERIFY(!destroyedWithOwner.isNull());
    }
    QVERIFY(destroyedWithOwner.isNull());
    QVERIFY(!survivor->parent());
    QCOMPARE(survivor->thread(), QThread::currentThread());
    QVERIFY(survivor->update(0x0017, 356));
}

void DeviceFactoryTests::failures_data() {
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<quint16>("id");
    QTest::addColumn<bool>("missing");
    QTest::addColumn<QString>("diagnostic");
    QTest::newRow("unknown-model") << minimal() << quint16(0xBEEF) << false << QStringLiteral("unknown model ID beef");
    QTest::newRow("missing-file") << QByteArray() << quint16(0x2222) << true << QString();
    QTest::newRow("malformed") << QByteArray("<DB><Device></DB>") << quint16(0x2222) << false << QStringLiteral("XML at line");
    QTest::newRow("invalid-definition") << minimal().replace("divider=\"10\"", "divider=\"0\"")
        << quint16(0x2222) << false << QStringLiteral("divider");
}

void DeviceFactoryTests::failures() {
    QFETCH(QByteArray, xml);
    QFETCH(quint16, id);
    QFETCH(bool, missing);
    QFETCH(QString, diagnostic);
    const auto survivor = DeviceFactory::create(catalog(), 0x1700);
    QVERIFY(survivor);
    QVERIFY(survivor->update(0x0017, 356));
    const auto saved = survivor->value(0x0017);
    QSignalSpy changed(survivor.get(), &DeviceRepository::propertyChanged);
    QSignalSpy cleared(survivor.get(), &DeviceRepository::cleared);
    QVERIFY(changed.isValid());
    QVERIFY(cleared.isValid());
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("factory-failure-XXXXXX")));
    QVERIFY(directory.isValid());
    const QString filename = missing ? directory.filePath(QStringLiteral("missing.xml")) : writeXml(directory, xml);
    QString loaderError;
    QVERIFY(!XmlConfigLoader::loadModel(filename, id, &loaderError));
    QString factoryError = QStringLiteral("old error");
    QVERIFY(!DeviceFactory::create(filename, id, &factoryError));
    QCOMPARE(factoryError, loaderError);
    QVERIFY(!factoryError.isEmpty());
    QVERIFY(factoryError != QStringLiteral("old error"));
    QVERIFY(factoryError.contains(filename));
    QVERIFY2(factoryError.contains(diagnostic), qPrintable(factoryError));
    QCOMPARE(survivor->value(0x0017), saved);
    QCOMPARE(survivor->configuration().parameters().size(), 97);
    QVERIFY(changed.isEmpty());
    QVERIFY(cleared.isEmpty());
}

void DeviceFactoryTests::completeDatabaseValidation() {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("factory-database-XXXXXX")));
    QVERIFY(directory.isValid());
    const auto xml = minimal().replace("</DB>",
        "<Device id=\"3333\" name=\"Invalid unselected\"><Commands>"
        "<Command code=\"0043\" divider=\"0\"/></Commands></Device></DB>");
    const QString filename = writeXml(directory, xml);
    QString loaderError;
    QVERIFY(!XmlConfigLoader::loadModel(filename, 0x2222, &loaderError));
    QString error;
    QVERIFY(!DeviceFactory::create(filename, 0x2222, &error));
    QCOMPARE(error, loaderError);
    QVERIFY(error.contains(QStringLiteral("divider")));
}

void DeviceFactoryTests::retryAndOptionalDiagnostic() {
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("factory-retry-XXXXXX")));
    QVERIFY(directory.isValid());
    const QString missing = directory.filePath(QStringLiteral("missing.xml"));
    QVERIFY(!DeviceFactory::create(missing, 0x1700));
    QString error;
    QVERIFY(!DeviceFactory::create(missing, 0x1700, &error));
    QVERIFY(!error.isEmpty());
    const auto repository = DeviceFactory::create(catalog(), 0x1700, &error);
    QVERIFY2(repository, qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(!repository->value(0x0017).isValid());
    const auto second = DeviceFactory::create(catalog(), 0x1700);
    QVERIFY(second);
    QVERIFY(second.get() != repository.get());
}

void DeviceFactoryTests::receivedValueFlow() {
    const auto repository = DeviceFactory::create(catalog(), 0x1700);
    QVERIFY(repository);
    QVERIFY(!repository->value(0x0017).isValid());
    QCOMPARE(repository->configuration().parameters().value(0x0017).unit, QStringLiteral("V"));
    QSignalSpy changed(repository.get(), &DeviceRepository::propertyChanged);
    QSignalSpy cleared(repository.get(), &DeviceRepository::cleared);
    QVERIFY(changed.isValid());
    QVERIFY(cleared.isValid());
    repository->clear();
    QVERIFY(cleared.isEmpty());
    QVERIFY(repository->update(0x0017, 356));
    const auto stored = repository->value(0x0017);
    QCOMPARE(stored.metaType().id(), int(QMetaType::Double));
    QCOMPARE(stored.toDouble(), 3.56);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).value<quint16>(), quint16(0x0017));
    QCOMPARE(changed.at(0).at(1).metaType().id(), int(QMetaType::Double));
    QCOMPARE(changed.at(0).at(1), stored);
    QVERIFY(repository->update(0x0017, 356));
    QCOMPARE(changed.count(), 1);
    const auto raw = repository->toRaw(0x0017, 3.56);
    QVERIFY(raw);
    QCOMPARE(*raw, quint16(356));
    QCOMPARE(repository->value(0x0017), stored);
    QCOMPARE(changed.count(), 1);
    QVERIFY(cleared.isEmpty());
    repository->clear();
    QCOMPARE(cleared.count(), 1);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!repository->value(0x0017).isValid());
    QCOMPARE(repository->configuration().modelId(), quint16(0x1700));
    QCOMPARE(repository->configuration().parameters().size(), 97);
    QCOMPARE(repository->configuration().parameters().value(0x0017).scale, 0.01);
    repository->clear();
    QCOMPARE(cleared.count(), 1);
    QVERIFY(repository->update(0x0017, 0));
    QCOMPARE(changed.count(), 2);
    QVERIFY(repository->value(0x0017).isValid());
}

void DeviceFactoryTests::signedParameter_data() {
    QTest::addColumn<QByteArray>("divider");
    QTest::addColumn<double>("expected");
    QTest::addColumn<int>("type");
    QTest::newRow("signed-integral") << QByteArray("1") << -100.0 << int(QMetaType::Int);
    QTest::newRow("signed-scaled") << QByteArray("10") << -10.0 << int(QMetaType::Double);
}

void DeviceFactoryTests::signedParameter() {
    QFETCH(QByteArray, divider);
    QFETCH(double, expected);
    QFETCH(int, type);
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("factory-signed-XXXXXX")));
    QVERIFY(directory.isValid());
    auto xml = minimal().replace("divider=\"10\"", "divider=\"" + divider + "\"");
    xml.replace("code=\"0042\"", "code=\"0042\" isSigned=\"\"");
    const auto repository = DeviceFactory::create(writeXml(directory, xml), 0x2222);
    QVERIFY(repository);
    QCOMPARE(repository->configuration().modelId(), quint16(0x2222));
    QCOMPARE(repository->configuration().modelName(), QStringLiteral("Synthetic"));
    const auto definition = repository->configuration().parameters().value(0x0042);
    QVERIFY(definition.isSigned);
    QCOMPARE(definition.unit, QStringLiteral("V"));
    QCOMPARE(definition.scale, divider == "1" ? 1.0 : 0.1);
    QCOMPARE(definition.tolerance, quint8(2));
    QVERIFY(!repository->value(0x0042).isValid());
    QSignalSpy changed(repository.get(), &DeviceRepository::propertyChanged);
    QVERIFY(changed.isValid());
    QVERIFY(repository->update(0x0042, 0xff9c));
    QCOMPARE(repository->value(0x0042).toDouble(), expected);
    QCOMPARE(repository->value(0x0042).metaType().id(), type);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).value<quint16>(), quint16(0x0042));
    QCOMPARE(changed.at(0).at(1).metaType().id(), type);
    QCOMPARE(changed.at(0).at(1).toDouble(), expected);
    const auto raw = repository->toRaw(0x0042, expected);
    QVERIFY(raw);
    QCOMPARE(*raw, quint16(0xff9c));
    QCOMPARE(changed.count(), 1);
}

QTEST_GUILESS_MAIN(DeviceFactoryTests)
#include "tst_devicefactory.moc"
