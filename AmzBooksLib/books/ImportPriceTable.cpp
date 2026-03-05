#include "ImportPriceTable.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QList>
#include <QPair>

// ── Fixed row order ──────────────────────────────────────────────────────────
// The table always contains exactly these rows in this order.
// An empty countryCode string represents the "Default" fallback row.
static const QList<QPair<QString /*code*/, QString /*label*/>> COUNTRY_ROWS = {
    {"",   "Default"},
    {"US", "US"},
    {"CA", "CA"},
    {"UK", "UK"},
    {"JP", "JP"},
};

// CSV serialisation label for the default row
static constexpr auto DEFAULT_CSV_LABEL = "Default";

static const QStringList HEADER_IDS = {"Year", "Country", "Price"};

// ── Construction ─────────────────────────────────────────────────────────────

ImportPriceTable::ImportPriceTable(const QDir &workingDir, QObject *parent)
    : QObject(parent)
{
    m_filePath = workingDir.absoluteFilePath("import-prices.csv");

    m_wasNewlyCreated = !QFile::exists(m_filePath);
    _load(); // populates m_prices
}

// ── Public API ───────────────────────────────────────────────────────────────

double ImportPriceTable::getShippingPrice(int year, const QString &countryCode) const
{
    auto itYear = m_prices.constFind(year);
    if (itYear == m_prices.constEnd()) {
        // Fallback to legacy year 0 if available
        itYear = m_prices.constFind(0);
        if (itYear == m_prices.constEnd()) {
            return 0.0;
        }
    }

    const auto &countryPrices = itYear.value();
    
    // Exact match first
    auto itCountry = countryPrices.constFind(countryCode);
    if (itCountry != countryPrices.constEnd()) {
        return itCountry.value();
    }
    
    // Fallback to Default (empty country code)
    itCountry = countryPrices.constFind("");
    if (itCountry != countryPrices.constEnd()) {
        return itCountry.value();
    }
    
    return 0.0;
}

void ImportPriceTable::setShippingPrice(int year, const QString &countryCode, double price)
{
    // Initialize standard countries if year doesn't exist to ensure they are written out
    if (!m_prices.contains(year)) {
        for (const auto &[code, label] : COUNTRY_ROWS) {
            m_prices[year][code] = 0.0;
        }
    }

    if (qFuzzyCompare(m_prices[year].value(countryCode, 0.0), price)) {
        return; // No change
    }

    m_prices[year][countryCode] = price;
    _save();
    emit pricesChanged();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ImportPriceTable::_load()
{
    m_prices.clear();

    QFile file(m_filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.readLine(); // skip header line

        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;

            const QStringList parts = line.split(';');
            if (parts.size() < 2) continue;

            int year = 0;
            QString csvLabel;
            double price = 0.0;

            if (parts.size() >= 3) {
                // New format: Year;Country;Price
                year = parts[0].trimmed().toInt();
                csvLabel = parts[1].trimmed();
                price = parts[2].trimmed().toDouble();
            } else {
                // Legacy format: Country;Price
                csvLabel = parts[0].trimmed();
                price = parts[1].trimmed().toDouble();
            }

            const QString code = (csvLabel == DEFAULT_CSV_LABEL) ? QString{} : csvLabel;
            
            // Generate defaults if year encountered for the first time
            if (!m_prices.contains(year)) {
                for (const auto &[cCode, cLabel] : COUNTRY_ROWS) {
                    m_prices[year][cCode] = 0.0;
                }
            }
            
            m_prices[year][code] = price;
        }
    } else {
        // If file doesn't exist, generate default countries for legacy year 0
        for (const auto &[code, label] : COUNTRY_ROWS) {
            m_prices[0][code] = 0.0;
        }
    }
}

void ImportPriceTable::_save() const
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "ImportPriceTable: cannot write to" << m_filePath << "-" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out << HEADER_IDS.join(';') << '\n';
    
    // Sort years
    QList<int> years = m_prices.keys();
    std::sort(years.begin(), years.end());
    
    for (int year : years) {
        const auto &countryPrices = m_prices[year];
        
        // Output in fixed row order first
        for (const auto &[code, label] : COUNTRY_ROWS) {
            double price = countryPrices.value(code, 0.0);
            const QString outLabel = code.isEmpty() ? DEFAULT_CSV_LABEL : code;
            out << year << ';' << outLabel << ';' << price << '\n';
        }
        
        // Output any custom countries not in fixed row order
        for (auto it = countryPrices.constBegin(); it != countryPrices.constEnd(); ++it) {
            bool isFixed = false;
            for (const auto &[code, label] : COUNTRY_ROWS) {
                if (code == it.key()) {
                    isFixed = true;
                    break;
                }
            }
            if (!isFixed) {
                out << year << ';' << it.key() << ';' << it.value() << '\n';
            }
        }
    }
}
