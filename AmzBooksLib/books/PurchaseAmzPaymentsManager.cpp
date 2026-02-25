#include "PurchaseAmzPaymentsManager.h"

#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QtMath>

#include "ExceptionWithTitleText.h"

// ---------------------------------------------------------------------------
// Approximate EUR conversion rates (fixed – not real-time)
// 200 EUR thresholds in each currency:
//   USD ~217, GBP ~172, CAD ~294, JPY ~32258, AUD ~333, MXN ~4348,
//   SEK ~2299, PLN ~870, TRY ~7407, AED ~800, SAR ~833, SGD ~290,
//   BRL ~1176, INR ~18182
// ---------------------------------------------------------------------------

const QStringList PurchaseAmzPaymentsManager::HEADER = {
    "Country", "Date From", "Date To",
    "Balance Start", "Balance End",
    "Expenses", "Refunded", "Paid"
};

// ─── static helpers ──────────────────────────────────────────────────────────

double PurchaseAmzPaymentsManager::toEur(double amount, const QString &currency)
{
    if (currency == "EUR") return amount;
    if (currency == "USD") return amount * 0.92;
    if (currency == "GBP") return amount * 1.16;
    if (currency == "CAD") return amount * 0.68;
    if (currency == "JPY") return amount * 0.0062;
    if (currency == "AUD") return amount * 0.60;
    if (currency == "MXN") return amount * 0.046;
    if (currency == "SEK") return amount * 0.087;
    if (currency == "PLN") return amount * 0.23;
    if (currency == "CZK") return amount * 0.040;
    if (currency == "HUF") return amount * 0.0026;
    if (currency == "AED") return amount * 0.25;
    if (currency == "SGD") return amount * 0.69;
    if (currency == "SAR") return amount * 0.24;
    if (currency == "TRY") return amount * 0.027;
    if (currency == "BRL") return amount * 0.17;
    if (currency == "INR") return amount * 0.011;
    // Unknown currency – treat as EUR (conservative)
    return amount;
}

// Parse "1311.19USD" → amount=1311.19, currency="USD"
static bool parseAmountCurrency(const QString &token,
                                double &amount, QString &currency)
{
    static QRegularExpression rx("^([0-9]+(?:\\.[0-9]*)?)([A-Z]+)$");
    QRegularExpressionMatch m = rx.match(token);
    if (!m.hasMatch())
        return false;
    amount   = m.captured(1).toDouble();
    currency = m.captured(2);
    return true;
}

// ─── decode ──────────────────────────────────────────────────────────────────

AmzPaymentInfo PurchaseAmzPaymentsManager::decode(const QString &filePath)
{
    AmzPaymentInfo info;
    info.filePath = filePath;

    QFileInfo fileInfo(filePath);

    // Use fileName() and strip only a purely-alphabetic file extension (e.g. ".pdf",
    // ".csv").  Do NOT use completeBaseName() because amounts like "177.90USD" contain
    // a dot that would be misinterpreted as an extension separator.
    QString baseName = fileInfo.fileName();
    {
        int lastDot = baseName.lastIndexOf('.');
        if (lastDot >= 0) {
            const QString potentialExt = baseName.mid(lastDot + 1);
            static const QRegularExpression rxPureAlpha("^[A-Za-z]+$");
            if (rxPureAlpha.match(potentialExt).hasMatch())
                baseName = baseName.left(lastDot);
        }
    }

    QStringList parts = baseName.split("__");

    // Minimum required parts:
    // [0] payment_MARKETPLACE_YYYY_MM_DD
    // [1] to
    // [2] YYYY_MM_DD
    // [3..size-2] optional tokens (balance-begin, balance-end mandatory, expenses, refunded-expenses)
    // [last] PAID_CUR
    if (parts.size() < 5) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Amazon Payment Filename"),
            QObject::tr("'%1' has too few parts (need ≥ 5, got %2).")
                .arg(filePath).arg(parts.size()));
        ex.raise();
    }

    // ── Part 0: payment_{marketplace}_{YYYY}_{MM}_{DD} ───────────────────────
    static QRegularExpression rxPart0(
        "^payment_(.+?)_(\\d{4})_(\\d{2})_(\\d{2})$");
    QRegularExpressionMatch m0 = rxPart0.match(parts[0]);
    if (!m0.hasMatch()) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Amazon Payment Filename"),
            QObject::tr("First part '%1' does not match 'payment_MARKETPLACE_YYYY_MM_DD'.")
                .arg(parts[0]));
        ex.raise();
    }
    info.countryCode = m0.captured(1);
    info.dateFrom = QDate::fromString(
        QString("%1-%2-%3").arg(m0.captured(2), m0.captured(3), m0.captured(4)),
        Qt::ISODate);
    if (!info.dateFrom.isValid()) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Date"),
            QObject::tr("Date from '%1' is not valid.").arg(parts[0]));
        ex.raise();
    }

    // ── Part 1: "to" ──────────────────────────────────────────────────────────
    if (parts[1] != "to") {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Amazon Payment Filename"),
            QObject::tr("Second part should be 'to', got '%1'.").arg(parts[1]));
        ex.raise();
    }

    // ── Part 2: YYYY_MM_DD (dateTo) ───────────────────────────────────────────
    static QRegularExpression rxDate("^(\\d{4})_(\\d{2})_(\\d{2})$");
    QRegularExpressionMatch m2 = rxDate.match(parts[2]);
    if (!m2.hasMatch()) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Date"),
            QObject::tr("Date-to part '%1' is not in YYYY_MM_DD format.").arg(parts[2]));
        ex.raise();
    }
    info.dateTo = QDate::fromString(
        QString("%1-%2-%3").arg(m2.captured(1), m2.captured(2), m2.captured(3)),
        Qt::ISODate);
    if (!info.dateTo.isValid()) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Date"),
            QObject::tr("Date to '%1' is not valid.").arg(parts[2]));
        ex.raise();
    }

    // ── Last part: paid {amount}{CUR} ─────────────────────────────────────────
    if (!parseAmountCurrency(parts.last(), info.paid, info.paidCurrency)) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Amount"),
            QObject::tr("Cannot parse paid amount from '%1'.").arg(parts.last()));
        ex.raise();
    }

    // ── Middle parts (index 3 … size-2): scan for known prefixes ──────────────
    // balance-begin and balance-end are optional but must appear together (or not at all).
    const QString balBeginPrefix         = "balance-begin-";
    const QString balEndPrefix           = "balance-end-";
    const QString expensesPrefix         = "expenses-";
    const QString refundedExpensesPrefix = "refunded-expenses-";

    for (int i = 3; i < parts.size() - 1; ++i) {
        const QString &part = parts[i];

        if (part.startsWith(balBeginPrefix)) {
            if (!parseAmountCurrency(part.mid(balBeginPrefix.length()),
                                     info.balanceStart, info.balanceStartCurrency)) {
                ExceptionWithTitleText ex(
                    QObject::tr("Invalid Amount"),
                    QObject::tr("Cannot parse balance-begin amount from '%1'.").arg(part));
                ex.raise();
            }
            info.hasBalanceStart = true;

        } else if (part.startsWith(balEndPrefix)) {
            if (!parseAmountCurrency(part.mid(balEndPrefix.length()),
                                     info.balanceEnd, info.balanceEndCurrency)) {
                ExceptionWithTitleText ex(
                    QObject::tr("Invalid Amount"),
                    QObject::tr("Cannot parse balance-end amount from '%1'.").arg(part));
                ex.raise();
            }
            info.hasBalanceEnd = true;

        } else if (part.startsWith(refundedExpensesPrefix)) {
            if (!parseAmountCurrency(part.mid(refundedExpensesPrefix.length()),
                                     info.refundedExpenses,
                                     info.refundedExpensesCurrency)) {
                ExceptionWithTitleText ex(
                    QObject::tr("Invalid Amount"),
                    QObject::tr("Cannot parse refunded-expenses from '%1'.").arg(part));
                ex.raise();
            }
            info.hasRefundedExpenses = true;

        } else if (part.startsWith(expensesPrefix)) {
            if (!parseAmountCurrency(part.mid(expensesPrefix.length()),
                                     info.expenses, info.expensesCurrency)) {
                ExceptionWithTitleText ex(
                    QObject::tr("Invalid Amount"),
                    QObject::tr("Cannot parse expenses from '%1'.").arg(part));
                ex.raise();
            }
            info.hasExpenses = true;
        }
    }

    // balance-begin and balance-end must appear together or not at all
    if (info.hasBalanceStart != info.hasBalanceEnd) {
        ExceptionWithTitleText ex(
            QObject::tr("Invalid Amazon Payment Filename"),
            QObject::tr("'%1': balance-begin and balance-end must both be present or both absent.")
                .arg(filePath));
        ex.raise();
    }

    // ── Threshold validation ───────────────────────────────────────────────────
    // Only meaningful when both balance tokens are present.
    if (!info.hasExpenses && info.hasBalanceStart && info.hasBalanceEnd) {
        double balStartEur = toEur(info.balanceStart, info.balanceStartCurrency);
        double balEndEur   = toEur(info.balanceEnd,   info.balanceEndCurrency);
        double paidEur     = toEur(info.paid,          info.paidCurrency);

        double estimatedExpensesEur = qMax(0.0, balStartEur - balEndEur - paidEur);

        if (estimatedExpensesEur > 200.0) {
            ExceptionWithTitleText ex(
                QObject::tr("Missing Expenses"),
                QObject::tr("'%1': expenses token is absent but estimated expenses "
                            "(%2 EUR) exceed the 200 EUR threshold. "
                            "Include the expenses-{amount}{CUR} token in the filename.")
                    .arg(filePath)
                    .arg(estimatedExpensesEur, 0, 'f', 2));
            ex.raise();
        }
    }

    return info;
}

// ─── encode ──────────────────────────────────────────────────────────────────

QString PurchaseAmzPaymentsManager::encode(const AmzPaymentInfo &info)
{
    QStringList parts;

    // payment_{marketplace}_{YYYY}_{MM}_{DD}
    parts << QString("payment_%1_%2")
                 .arg(info.countryCode,
                      info.dateFrom.toString("yyyy_MM_dd"));

    parts << "to";
    parts << info.dateTo.toString("yyyy_MM_dd");

    if (info.hasBalanceStart) {
        parts << QString("balance-begin-%1%2")
                     .arg(info.balanceStart, 0, 'f', 2)
                     .arg(info.balanceStartCurrency);
    }

    if (info.hasBalanceEnd) {
        parts << QString("balance-end-%1%2")
                     .arg(info.balanceEnd, 0, 'f', 2)
                     .arg(info.balanceEndCurrency);
    }

    if (info.hasExpenses) {
        parts << QString("expenses-%1%2")
                     .arg(info.expenses, 0, 'f', 2)
                     .arg(info.expensesCurrency);
    }

    if (info.hasRefundedExpenses) {
        parts << QString("refunded-expenses-%1%2")
                     .arg(info.refundedExpenses, 0, 'f', 2)
                     .arg(info.refundedExpensesCurrency);
    }

    parts << QString("%1%2")
                 .arg(info.paid, 0, 'f', 2)
                 .arg(info.paidCurrency);

    return parts.join("__");
}

// ─── add / remove / getRelativePath ──────────────────────────────────────────

QString PurchaseAmzPaymentsManager::getRelativePath(const AmzPaymentInfo &info)
{
    return QString("amazon-payments/%1").arg(info.dateFrom.year());
}

void PurchaseAmzPaymentsManager::add(const QString &sourceFilePath, const AmzPaymentInfo &info)
{
    QFileInfo sourceInfo(sourceFilePath);
    if (!sourceInfo.exists()) {
        ExceptionWithTitleText ex(
            QObject::tr("File Not Found"),
            QObject::tr("The source file '%1' does not exist.").arg(sourceFilePath));
        ex.raise();
    }

    QString relativePath = getRelativePath(info);
    QDir destDir(m_workingDir);
    if (!destDir.mkpath(relativePath)) {
        ExceptionWithTitleText ex(
            QObject::tr("Directory Error"),
            QObject::tr("Could not create directory '%1'.").arg(relativePath));
        ex.raise();
    }
    if (!destDir.cd(relativePath)) {
        ExceptionWithTitleText ex(
            QObject::tr("Directory Error"),
            QObject::tr("Could not access directory '%1'.").arg(relativePath));
        ex.raise();
    }

    QString ext = sourceInfo.suffix().toLower();
    QString fileName = encode(info) + (ext.isEmpty() ? QString() : "." + ext);
    QString destFilePath = destDir.filePath(fileName);

    if (!QFile::copy(sourceFilePath, destFilePath)) {
        ExceptionWithTitleText ex(
            QObject::tr("Copy Error"),
            QObject::tr("Failed to copy file from '%1' to '%2'.").arg(sourceFilePath, destFilePath));
        ex.raise();
    }

    _load();
}

bool PurchaseAmzPaymentsManager::remove(const QString &fileName)
{
    QString filePath;
    for (const AmzPaymentInfo &info : m_data) {
        if (QFileInfo(info.filePath).fileName() == fileName) {
            filePath = info.filePath;
            break;
        }
    }

    if (filePath.isEmpty()) {
        if (QFile::exists(fileName))
            filePath = fileName;
        else
            return false;
    }

    if (!QFile::remove(filePath))
        return false;

    _load();
    return true;
}

// ─── QAbstractTableModel ─────────────────────────────────────────────────────

PurchaseAmzPaymentsManager::PurchaseAmzPaymentsManager(
        const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    _load();
}

int PurchaseAmzPaymentsManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_data.size();
}

int PurchaseAmzPaymentsManager::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return HEADER.size();
}

QVariant PurchaseAmzPaymentsManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size())
        return QVariant();

    const AmzPaymentInfo &item = m_data.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return item.countryCode;
        case 1: return item.dateFrom;
        case 2: return item.dateTo;
        case 3: return QString("%1 %2").arg(item.balanceStart, 0, 'f', 2)
                                       .arg(item.balanceStartCurrency);
        case 4: return QString("%1 %2").arg(item.balanceEnd, 0, 'f', 2)
                                       .arg(item.balanceEndCurrency);
        case 5: return item.hasExpenses
                    ? QString("%1 %2").arg(item.expenses, 0, 'f', 2)
                                      .arg(item.expensesCurrency)
                    : QString();
        case 6: return item.hasRefundedExpenses
                    ? QString("%1 %2").arg(item.refundedExpenses, 0, 'f', 2)
                                      .arg(item.refundedExpensesCurrency)
                    : QString();
        case 7: return QString("%1 %2").arg(item.paid, 0, 'f', 2)
                                       .arg(item.paidCurrency);
        }
    }
    return QVariant();
}

QVariant PurchaseAmzPaymentsManager::headerData(int section,
                                                 Qt::Orientation orientation,
                                                 int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section < HEADER.size())
            return HEADER[section];
    }
    return QVariant();
}

QList<AmzPaymentInfo> PurchaseAmzPaymentsManager::getPayments(
        const QDate &from, const QDate &to) const
{
    QList<AmzPaymentInfo> result;
    for (const AmzPaymentInfo &info : m_data) {
        if (info.dateFrom >= from && info.dateFrom <= to)
            result.append(info);
    }
    return result;
}

const QList<AmzPaymentInfo> &PurchaseAmzPaymentsManager::allPayments() const
{
    return m_data;
}

void PurchaseAmzPaymentsManager::_load()
{
    beginResetModel();
    m_data.clear();

    QDir paymentsDir(m_workingDir);
    if (paymentsDir.cd("amazon-payments")) {
        scanDirectory(paymentsDir);
    }

    std::sort(m_data.begin(), m_data.end(),
              [](const AmzPaymentInfo &a, const AmzPaymentInfo &b) {
        return a.dateFrom > b.dateFrom;
    });

    endResetModel();
}

void PurchaseAmzPaymentsManager::scanDirectory(const QDir &dir)
{
    QDirIterator it(dir.path(),
                    QStringList() << "payment_*",
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        try {
            AmzPaymentInfo info = decode(filePath);
            if (info.dateFrom.isValid())
                m_data.append(info);
        } catch (const ExceptionWithTitleText &) {
            // Skip files that don't match the expected pattern
        }
    }
}
