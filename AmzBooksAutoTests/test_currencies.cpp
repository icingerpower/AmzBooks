#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>

#include "CurrencyRateManager.h"
#include "ExceptionRateCurrency.h"

class TestCurrencies : public QObject
{
    Q_OBJECT

public:
    TestCurrencies() {}
    ~TestCurrencies() {}

private slots:
    void initTestCase()
    {
        m_apiKey = QProcessEnvironment::systemEnvironment().value("FIXER_IO_API_KEY");
        if (m_apiKey.isEmpty()) {
            QString msg = "Environment variable 'FIXER_IO_API_KEY' is not set.\n"
                          "Linux: export FIXER_IO_API_KEY=your_key\n"
                          "Windows: set FIXER_IO_API_KEY=your_key\n"
                          "Tests will fail without this key.";
            QFAIL(qPrintable(msg));
        }
        
        QDir dir(QCoreApplication::applicationDirPath());
        if (!dir.exists("test_data")) dir.mkdir("test_data");
        m_workingDir = QDir(dir.absoluteFilePath("test_data"));
    }

    void testRateRetrieval()
    {
        if (m_apiKey.isEmpty()) QSKIP("No API Key");

        CurrencyRateManager manager(m_workingDir, m_apiKey);
        
        // Pick a date in the past to ensure stability
        QDate date(2023, 10, 27);
        QString source = "USD";
        QString dest = "EUR";

        try {
            double rate = manager.rate(source, dest, date);
            qDebug() << "Rate USD->EUR on" << date << ":" << rate;
            QVERIFY(rate > 0.0);
            QVERIFY(rate < 2.0); // Reasonable range for USD->EUR
            
            // Test conversion
            double amount = 100.0;
            double converted = manager.convert(amount, source, dest, date);
            QCOMPARE(converted, amount * rate);

        } catch (const ExceptionRateCurrency &e) {
            QFAIL(qPrintable("ExceptionRateCurrency caught: " + e.url()));
        }
    }

private:
    QDir m_workingDir;
    QString m_apiKey;
};

QTEST_MAIN(TestCurrencies)

#include "test_currencies.moc"
