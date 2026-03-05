#include <QtTest>
#include <QCoreApplication>
#include <stdexcept>
#include "books/ReportGenerator.h"

class TestReportGenerator : public QObject
{
    Q_OBJECT

private slots:
    void test_startTableExceptions();
    void test_addRowExceptions();
    void test_endTableExceptions();
    
    void test_initialState();
    void test_titlesAndSubtitles();
    void test_tableValidPath();
    void test_multipleTablesAndComplexFlow();
    
    void test_savePdf();

    void test_emptyInputs();
    void test_htmlEscapingNotPerformed();
    void test_savePdfInvalidPath();
    void test_largeData();
    void test_savePdfMultiplePages();
};

void TestReportGenerator::test_startTableExceptions()
{
    ReportGenerator rep;
    rep.startTable({"A", "B"});
    
    // Most likely bug: starting table without ending
    bool caught = false;
    try {
        rep.startTable({"C"});
    } catch (const std::runtime_error& e) {
        caught = true;
        QCOMPARE(QString(e.what()), QString("A table is already started."));
    }
    QVERIFY(caught);
}

void TestReportGenerator::test_addRowExceptions()
{
    ReportGenerator rep;
    
    // Add row without table
    bool caught1 = false;
    try {
        rep.addRow({"Val"});
    } catch (const std::runtime_error& e) {
        caught1 = true;
        QCOMPARE(QString(e.what()), QString("No table started."));
    }
    QVERIFY(caught1);
    
    rep.startTable({"H1", "H2"});
    
    // Add row with too few elements
    bool caught2 = false;
    try {
        rep.addRow({"Val1"});
    } catch (const std::runtime_error& e) {
        caught2 = true;
        QCOMPARE(QString(e.what()), QString("Invalid number of elements for the current table row."));
    }
    QVERIFY(caught2);
    
    // Add row with too many elements
    bool caught3 = false;
    try {
        rep.addRow({"Val1", "Val2", "Val3"});
    } catch (const std::runtime_error& e) {
        caught3 = true;
        QCOMPARE(QString(e.what()), QString("Invalid number of elements for the current table row."));
    }
    QVERIFY(caught3);
}

void TestReportGenerator::test_endTableExceptions()
{
    ReportGenerator rep;
    
    // End without table
    bool caught = false;
    try {
        rep.endTable();
    } catch (const std::runtime_error& e) {
        caught = true;
        QCOMPARE(QString(e.what()), QString("No table started."));
    }
    QVERIFY(caught);
    
    rep.startTable({"H1"});
    rep.endTable();
    
    // End after ended
    caught = false;
    try {
        rep.endTable();
    } catch (const std::runtime_error& e) {
        caught = true;
        QCOMPARE(QString(e.what()), QString("No table started."));
    }
    QVERIFY(caught);
}

void TestReportGenerator::test_initialState()
{
    ReportGenerator rep;
    QString html = rep.getHtml();
    
    QVERIFY(html.contains("</body></html>"));
    QVERIFY(!html.contains("<h1>"));
    QVERIFY(!html.contains("<h2>"));
    QVERIFY(!html.contains("<table"));
    
    QCOMPARE(html.count("<html>"), 1);
    QCOMPARE(html.count("<body>"), 1);
    QCOMPARE(html.count("</body>"), 1);
    QCOMPARE(html.count("</html>"), 1);
}

void TestReportGenerator::test_titlesAndSubtitles()
{
    ReportGenerator rep;
    rep.addTitle("My Main Title");
    rep.addSubtitle("My SubTitle 1");
    rep.addSubtitle("My SubTitle 2");
    
    QString html = rep.getHtml();
    
    QVERIFY(html.contains("<h1>My Main Title</h1>"));
    QVERIFY(html.contains("<h2>My SubTitle 1</h2>"));
    QVERIFY(html.contains("<h2>My SubTitle 2</h2>"));
    QVERIFY(html.indexOf("<h1>") < html.indexOf("<h2>")); // Title before subtitle
    QVERIFY(html.indexOf("<h2>My SubTitle 1") < html.indexOf("<h2>My SubTitle 2"));
    
    QCOMPARE(html.count("<h1>"), 1);
    QCOMPARE(html.count("<h2>"), 2);
}

void TestReportGenerator::test_tableValidPath()
{
    ReportGenerator rep;
    rep.startTable({"Col1", "Col2"});
    rep.addRow({"Val1", "Val2"});
    rep.addRow({"Val3", "Val4"});
    
    // getHtml() before endTable() should still close the table gracefully
    QString earlyHtml = rep.getHtml();
    QVERIFY(earlyHtml.contains("</table>"));
    QVERIFY(earlyHtml.contains("</body></html>"));
    
    rep.endTable();
    QString html = rep.getHtml();
    
    QVERIFY(html.contains("<table border=\"1\""));
    QVERIFY(html.contains(">Col1</th>"));
    QVERIFY(html.contains(">Col2</th>"));
    QVERIFY(html.contains(">Val1</td>"));
    QVERIFY(html.contains(">Val2</td>"));
    QVERIFY(html.contains(">Val3</td>"));
    QVERIFY(html.contains(">Val4</td>"));
    QVERIFY(html.contains("</table>"));
    
    // Structural checks
    QCOMPARE(html.count("<table"), 1);
    QCOMPARE(html.count("</table>"), 1);
    QCOMPARE(html.count("<tr"), 3); // 1 header + 2 rows
    QCOMPARE(html.count("<th"), 2);
    QCOMPARE(html.count("<td"), 4);
    
    // Tag ordering
    int tableStartIdx = html.indexOf("<table");
    int firstThIdx = html.indexOf(">Col1</th>");
    int firstTdIdx = html.indexOf(">Val1</td>");
    int tableEndIdx = html.indexOf("</table>");
    
    QVERIFY(tableStartIdx != -1);
    QVERIFY(firstThIdx > tableStartIdx);
    QVERIFY(firstTdIdx > firstThIdx);
    QVERIFY(tableEndIdx > firstTdIdx);
}

void TestReportGenerator::test_multipleTablesAndComplexFlow()
{
    ReportGenerator rep;
    rep.addTitle("T1");
    
    rep.startTable({"Header1"});
    rep.addRow({"Row1-1"});
    rep.endTable();
    
    rep.addSubtitle("S1");
    
    rep.startTable({"Header2A", "Header2B"});
    rep.addRow({"Row2-1A", "Row2-1B"});
    rep.addRow({"Row2-2A", "Row2-2B"});
    rep.endTable();
    
    QString html = rep.getHtml();
    
    QCOMPARE(html.count("<table"), 2);
    QCOMPARE(html.count("</table>"), 2);
    QCOMPARE(html.count("<h1>"), 1);
    QCOMPARE(html.count("<h2>"), 1);
    
    QCOMPARE(html.count("<th"), 3);
    QCOMPARE(html.count("<td"), 5);
    QCOMPARE(html.count("<tr"), 5); // 1 head + 1 row for table1, 1 head + 2 rows for table 2
    
    QVERIFY(html.indexOf("<h1>T1</h1>") < html.indexOf("<table"));
    QVERIFY(html.indexOf("</table>") < html.indexOf("<h2>S1</h2>"));
    QVERIFY(html.indexOf("<h2>S1</h2>") < html.lastIndexOf("<table"));
}

void TestReportGenerator::test_savePdf()
{
    ReportGenerator rep;
    rep.addTitle("Title");
    rep.startTable({"H1"});
    rep.addRow({"V1"});
    rep.endTable();

    QString path = "test_output_report.pdf";
    QFile::remove(path); // ensure it doesn't exist

    rep.save(path);
    
    QVERIFY(QFile::exists(path));
    QFile f(path);
    QVERIFY(f.size() > 0);
    
    QFile::remove(path); // clean up
}

void TestReportGenerator::test_emptyInputs()
{
    ReportGenerator rep;
    rep.addTitle("");
    rep.addSubtitle("");
    rep.startTable({});
    rep.addRow({});
    rep.endTable();

    QString html = rep.getHtml();
    QVERIFY(html.contains("<h1></h1>"));
    QVERIFY(html.contains("<h2></h2>"));
    QCOMPARE(html.count("<th"), 0);
    QCOMPARE(html.count("<td"), 0);
    QVERIFY(html.contains("<tr>\n  </tr>"));
}

void TestReportGenerator::test_htmlEscapingNotPerformed()
{
    ReportGenerator rep;
    rep.addTitle("<script>alert()</script>");
    QString html = rep.getHtml();
    QVERIFY(html.contains("<h1><script>alert()</script></h1>"));
}

void TestReportGenerator::test_savePdfInvalidPath()
{
    ReportGenerator rep;
    rep.addTitle("Test");
    
    // /root is usually inaccessible for writing for normal users, or an invalid path
    QString invalidPath = "/root/this_should_fail_report.pdf";
    rep.save(invalidPath);
    // Should not crash, and should not create the file
    QVERIFY(!QFile::exists(invalidPath));
}

void TestReportGenerator::test_largeData()
{
    ReportGenerator rep;
    QStringList headers;
    for (int i = 0; i < 100; ++i) {
        headers << "H" + QString::number(i);
    }
    rep.startTable(headers);
    for (int r = 0; r < 50; ++r) {
        QStringList row;
        for (int i = 0; i < 100; ++i) {
            row << "V" + QString::number(r) + "-" + QString::number(i);
        }
        rep.addRow(row);
    }
    rep.endTable();

    QString html = rep.getHtml();
    QCOMPARE(html.count("<th"), 100);
    QCOMPARE(html.count("<td"), 5000);
    QCOMPARE(html.count("<tr"), 51);
    QVERIFY(html.contains(">H99</th>"));
    QVERIFY(html.contains(">V49-99</td>"));
}

void TestReportGenerator::test_savePdfMultiplePages()
{
    ReportGenerator rep;
    rep.addTitle("Long Report");
    rep.startTable({"Col"});
    for (int i = 0; i < 500; ++i) {
        rep.addRow({QString("Row %1").arg(i)});
    }
    rep.endTable();

    QString path = "test_output_long_report.pdf";
    QFile::remove(path);

    rep.save(path);
    QVERIFY(QFile::exists(path));
    
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    QVERIFY(data.size() > 1000); // Should be a decent sized PDF
    QVERIFY(data.startsWith("%PDF-")); // Valid PDF header
    QVERIFY(data.contains("/Type /Page")); // Should contain pages
    f.close();
    QFile::remove(path);
}

QTEST_MAIN(TestReportGenerator)
#include "test_report_generator.moc"
