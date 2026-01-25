#ifndef MODEL_CURRENCYRATEMANAGER_H
#define MODEL_CURRENCYRATEMANAGER_H

#include <QObject>
#include <QDir>
#include <QDate>
#include <QHash>
#include <QMutex>

/*
 * To test this class:
 * Define environment variable FIXER_IO_API_KEY with your fixer.io API key.
 *
 * Temporary (current terminal):
 * Linux: export FIXER_IO_API_KEY=your_key
 * Windows: set FIXER_IO_API_KEY=your_key
 *
 * Permanent (User session / IDEs):
 * Linux: Add 'export FIXER_IO_API_KEY=your_key' to ~/.profile and logout/login.
 */
class CurrencyRateManager : public QObject
{
    Q_OBJECT
public:
    static void setAllowRealApiCalls(bool allowed);

    explicit CurrencyRateManager(const QDir &workingDir, const QString &apiKey, QObject *parent = nullptr);

    double rate(const QString &source, const QString &dest, const QDate &date) const;
    double convert(double amount, const QString &source, const QString &dest, const QDate &date);
    double retrieveCurrency(const QString &source, const QString &dest, const QDate &date) const;
    void importRate(const QString &date, const QString &currencyFrom, const QString &currencyTo, double rate);
    void setWorkingDir(const QDir &path);

private:
    void _loadRates() const;
    void _appendRate(const QString &key, double rate) const;
    QString _csvPath() const;

    static bool s_allowRealApiCalls;

    QDir m_workingDir;
    QString m_apiKey;
    mutable QHash<QString, double> m_rateCache;
    mutable bool m_loaded = false;
    mutable QMutex m_mutex;
};

#endif // MODEL_CURRENCYRATEMANAGER_H
