#include "CurrencyRateManager.h"
#include "ExceptionRateCurrency.h"
#include <QDebug>
#include <QFileInfo>
#include <QTextStream>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QFile>

namespace {
double extractRateFromContent(const QString &content, const QString &currency) {
    double rate = 1.;
    if (currency != "EUR") {
        if (!content.contains(currency)) {
            qDebug() << content;
            throw ExceptionRateCurrency("Rate not found for currency: " + currency);
        }
        QString searchStr = QString("\"%1\":").arg(currency);
        auto parts = content.split(searchStr);
        if (parts.size() < 2) {
             throw ExceptionRateCurrency("Parse error for currency: " + currency);
        }
        QString rateString = parts[1];
        if (rateString.contains(",")) {
            rate = rateString.split(",")[0].toDouble();
        } else {
            rate = rateString.split("}")[0].toDouble();
        }
    }
    return rate;
}
} // namespace

CurrencyRateManager::CurrencyRateManager(const QDir &workingDir, const QString &apiKey, QObject *parent)
    : QObject(parent)
    , m_workingDir(workingDir)
    , m_apiKey(apiKey)
{
}

double CurrencyRateManager::rate(const QString &source, const QString &dest, const QDate &date) const
{
    Q_ASSERT(date.isValid());
    Q_ASSERT(source != dest);
    Q_ASSERT(source.size() == 3);
    Q_ASSERT(dest.size() == 3);
    
    QString key = date.toString("yyyy_MM_dd") + "/" + dest + "-" + source;
    
    QMutexLocker locker(&m_mutex);
    _loadRates();

    if (!m_rateCache.contains(key)) {
        // Unlock to perform network request without holding the lock
        locker.unlock();
        double rateValue = retrieveCurrency(source, dest, date);
        locker.relock();
        
        // Check again in case another thread filled it
        if (!m_rateCache.contains(key)) {
            _appendRate(key, rateValue);
            
            QString keyReversed = date.toString("yyyy_MM_dd") + "/" + source + "-" + dest;
            if (rateValue != 0.0) {
                 _appendRate(keyReversed, 1.0 / rateValue);
            }
        }
    }
    return m_rateCache.value(key, 1.0);
}

double CurrencyRateManager::convert(double amount, const QString &source, const QString &dest, const QDate &date)
{
    if (source != dest) {
        Q_ASSERT(source.length() == 3 && dest.length() == 3 && source.toUpper() != dest.toUpper());
        double r = this->rate(source, dest, date);
        return amount * r;
    }
    return amount;
}

double CurrencyRateManager::retrieveCurrency(const QString &source, const QString &dest, const QDate &date) const
{
    Q_ASSERT(date < QDate::currentDate());
    double rate = 1.0;
    if (source != dest) {
        QString url = "http://data.fixer.io/api/";
        url += date.toString("yyyy-MM-dd");
        url += "?access_key=" + m_apiKey;
        url += "&symbols=";
        bool hasEuro = source == "EUR" || dest == "EUR";
        if (!hasEuro) {
            url += source;
            url += ",";
            url += dest;
        } else {
            QString combined = source + dest;
            combined.replace("EUR", "");
            url += combined;
        }
        qDebug() << "url to retrieve currency:" << url;
        
        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        QNetworkReply *reply = manager.get(request);
        
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() != QNetworkReply::NoError) {
            QString errorMsg = reply->errorString();
            delete reply;
            throw ExceptionRateCurrency("Network error: " + errorMsg);
        }
        
        QString rateInfo = QString::fromUtf8(reply->readAll());
        delete reply;

        if (rateInfo.isEmpty()) {
            throw ExceptionRateCurrency(url);
        }
        
        double rateSource = 1.0; 
        double rateDest = 1.0;
        
        try {
            rateSource = extractRateFromContent(rateInfo, source);
            rateDest = extractRateFromContent(rateInfo, dest);
        } catch (...) {
            throw ExceptionRateCurrency(url);
        }

        if (rateSource == 0.0) {
             throw ExceptionRateCurrency(url + " (source rate is 0)");
        }

        rate = rateDest / rateSource;
    }
    return rate;
}

void CurrencyRateManager::setWorkingDir(const QDir &path)
{
    QMutexLocker locker(&m_mutex);
    m_workingDir = path;
    m_loaded = false;
    m_rateCache.clear();
}

QString CurrencyRateManager::_csvPath() const
{
    return m_workingDir.absoluteFilePath("currency-rates.csv");
}

/*
 * Helper to call _loadRates from const method where we are sure locking is handled if needed
 * or just casting constness.
 * Actually _loadRates is const, but m_loaded is mutable.
 */
void CurrencyRateManager::_loadRates() const
{
    if (m_loaded) return;

    QString path = _csvPath();
    if (!QFileInfo::exists(path)) {
        m_loaded = true;
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_loaded = true;
        return;
    }

    QTextStream in(&file);
    // Read header line?
    // The previous implementation used CsvReader which handled headers.
    // We expect: Date,Source,Dest,Rate
    
    // Let's read the first line and check headers or just skip logic.
    // The previous code had dynamic column index detection.
    
    QString headerLine = in.readLine();
    QStringList headers = headerLine.split(",");
    
    int idxDate = -1;
    int idxSource = -1;
    int idxDest = -1;
    int idxRate = -1;
    
    for(int i=0; i<headers.size(); ++i) {
        QString h = headers[i].trimmed();
        if (h == "Date") idxDate = i;
        else if (h == "Source") idxSource = i;
        else if (h == "Dest") idxDest = i;
        else if (h == "Rate") idxRate = i;
    }
    
    // Fallback to defaults if headers missing (or empty file)
    if (idxDate == -1) idxDate = 0;
    if (idxSource == -1) idxSource = 1;
    if (idxDest == -1) idxDest = 2;
    if (idxRate == -1) idxRate = 3;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        
        QStringList row = line.split(",");
        int maxNeeded = qMax(idxDate, qMax(idxSource, qMax(idxDest, idxRate)));
        if (row.size() <= maxNeeded) continue;

        QString dateStr = row[idxDate].trimmed();
        QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
        if (!date.isValid()) date = QDate::fromString(dateStr, "yyyy_MM_dd");

        QString key = date.toString("yyyy_MM_dd") + "/" + row[idxDest].trimmed() + "-" + row[idxSource].trimmed();
        double rate = row[idxRate].toDouble();
        m_rateCache.insert(key, rate);
    }

    m_loaded = true;
}

void CurrencyRateManager::_appendRate(const QString &key, double rate) const
{
    // Update cache
    m_rateCache.insert(key, rate);

    // Append to file
    QString path = _csvPath();
    QFile file(path);
    bool exists = file.exists();
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        if (!exists) {
            out << "Date,Source,Dest,Rate\n";
        }
        
        auto parts = key.split("/");
        if (parts.size() == 2) {
            QString dateStr = parts[0]; // yyyy_MM_dd
            QDate date = QDate::fromString(dateStr, "yyyy_MM_dd");
            
            auto currencies = parts[1].split("-");
            if (currencies.size() == 2) {
                QString dest = currencies[0];
                QString source = currencies[1];
                
                out << date.toString("yyyy-MM-dd") << "," 
                    << source << "," 
                    << dest << "," 
                    << QString::number(rate, 'g', 10) << "\n";
            }
        }
    }
}
