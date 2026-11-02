#include "XmlParser.h"

#include <QtTest>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(std::declval<TreeItem&>().children()),
                             const std::vector<TreeItem>&>);

class XmlParserTests : public QObject {
    Q_OBJECT
private slots:
    void attributes();
    void nestedAndRepeatedElements();
    void textAndEmptyElements();
    void malformed_data();
    void malformed();
    void ownershipAndLifetime();
};

void XmlParserTests::attributes() {
    QString error = QStringLiteral("previous error");
    const auto tree = XmlParser::parse("<Item id=\"2A\" label=\"A &amp; B\"/>", &error);
    QVERIFY2(tree.has_value(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(tree->name(), QStringLiteral("Item"));
    QVERIFY(tree->value().isEmpty());
    QCOMPARE(tree->children().size(), size_t(2));
    QCOMPARE(tree->children()[0].name(), QStringLiteral("id"));
    QCOMPARE(tree->children()[0].value(), QStringLiteral("2A"));
    QVERIFY(tree->children()[0].children().empty());
    QCOMPARE(tree->children()[1].name(), QStringLiteral("label"));
    QCOMPARE(tree->children()[1].value(), QStringLiteral("A & B"));
}

void XmlParserTests::nestedAndRepeatedElements() {
    const auto tree = XmlParser::parse(
        "<Node kind=\"root\"><Node><Node>deep</Node></Node>"
        "<Entry id=\"1\"/><Entry id=\"2\"/></Node>");
    QVERIFY(tree);
    const auto& children = tree->children();
    QCOMPARE(children.size(), size_t(4));
    QCOMPARE(children[0].name(), QStringLiteral("kind"));
    QCOMPARE(children[1].name(), QStringLiteral("Node"));
    QCOMPARE(children[1].children().size(), size_t(1));
    QCOMPARE(children[1].children()[0].name(), QStringLiteral("Node"));
    QCOMPARE(children[1].children()[0].value(), QStringLiteral("deep"));
    QCOMPARE(children[2].name(), QStringLiteral("Entry"));
    QCOMPARE(children[3].name(), QStringLiteral("Entry"));
    QCOMPARE(children[2].children()[0].value(), QStringLiteral("1"));
    QCOMPARE(children[3].children()[0].value(), QStringLiteral("2"));
}

void XmlParserTests::textAndEmptyElements() {
    const auto tree = XmlParser::parse(
        "<?xml version=\"1.0\"?><Root> before <![CDATA[A<]]>&amp;"
        "<!--ignored--><?note ignored?><Empty/><Text> caf\xc3\xa9 </Text> after </Root>");
    QVERIFY(tree);
    QCOMPARE(tree->value(), QStringLiteral(" before A<& after "));
    QCOMPARE(tree->children().size(), size_t(2));
    QCOMPARE(tree->children()[0].name(), QStringLiteral("Empty"));
    QVERIFY(tree->children()[0].value().isEmpty());
    QVERIFY(tree->children()[0].children().empty());
    QCOMPARE(tree->children()[1].value(), QString::fromUtf8(" caf\xc3\xa9 "));
    const auto whitespace = XmlParser::parse("<Root>\n  <Empty/>\n</Root>");
    QVERIFY(whitespace);
    QCOMPARE(whitespace->value(), QStringLiteral("\n  \n"));
}

void XmlParserTests::malformed_data() {
    QTest::addColumn<QByteArray>("xml");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("whitespace-only") << QByteArray(" \n ");
    QTest::newRow("unclosed-root") << QByteArray("<Root>");
    QTest::newRow("mismatched-tags") << QByteArray("<Root>\n<Item></Root>");
    QTest::newRow("bad-attribute") << QByteArray("<Root id=unquoted/>");
    QTest::newRow("duplicate-attribute") << QByteArray("<Root id=\"1\" id=\"2\"/>");
    QTest::newRow("bad-entity") << QByteArray("<Root>&missing;</Root>");
    QTest::newRow("second-root") << QByteArray("<Root/><Another/>");
    QTest::newRow("trailing-text") << QByteArray("<Root/>unexpected");
    QTest::newRow("truncated-tail") << QByteArray("<Root/><!--");
}

void XmlParserTests::malformed() {
    QFETCH(QByteArray, xml);
    QString error;
    QVERIFY(!XmlParser::parse(xml, &error));
    QVERIFY2(QRegularExpression(QStringLiteral("^XML at line [0-9]+, column [0-9]+: .+"))
                 .match(error).hasMatch(), qPrintable(error));
    QVERIFY(!XmlParser::parse(xml)); // Diagnostics are optional, failure is not.
    const auto valid = XmlParser::parse("<Valid/>", &error);
    QVERIFY(valid);
    QVERIFY(error.isEmpty());
}

void XmlParserTests::ownershipAndLifetime() {
    std::optional<TreeItem> copy;
    {
        QByteArray input("<Root id=\"owned\"><Child>retained</Child></Root>");
        auto original = XmlParser::parse(input);
        QVERIFY(original);
        copy = *original;
        input.fill('x');
        original.reset();
    } // Input, reader and original tree are gone; the copy owns its children.
    TreeItem moved = std::move(*copy);
    copy.reset();
    QCOMPARE(moved.name(), QStringLiteral("Root"));
    QCOMPARE(moved.children().size(), size_t(2));
    QCOMPARE(moved.children()[0].value(), QStringLiteral("owned"));
    QCOMPARE(moved.children()[1].value(), QStringLiteral("retained"));
}

QTEST_GUILESS_MAIN(XmlParserTests)
#include "tst_xmlparser.moc"
