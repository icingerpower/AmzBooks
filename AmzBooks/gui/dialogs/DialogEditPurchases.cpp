#include "DialogEditPurchases.h"
#include "ui_DialogEditPurchases.h"

#include <QDateEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QFileInfo>
#include <QtMath>
#include <algorithm>

#include "CountriesEu.h"
#include "ExceptionWithTitleText.h"
#include "books/BookAccountPurchaseTable.h"
#include "CurrencyRateManager.h"

// Extract the numeric rate string (e.g. "20" or "5.5") from a VAT token
// such as "FR-TVA20-13.6EUR" → "20", or "TVA-13.6EUR" → "".
static QString extractVatRateFromToken(const QString &token)
{
    static QRegularExpression regexRate(QStringLiteral("[0-9.]+"));
    const QStringList parts = token.split(QLatin1Char('-'));
    // Token with country: parts[0]=country, parts[1]=TVAxxx, parts[2]=amount
    // Token without country: parts[0]=TVAxxx, parts[1]=amount
    const int ratePartIdx = (parts.size() >= 3) ? 1 : 0;
    if (ratePartIdx < parts.size()) {
        const QRegularExpressionMatch m = regexRate.match(parts[ratePartIdx]);
        if (m.hasMatch())
            return m.captured(0);
    }
    return {};
}

DialogEditPurchases::DialogEditPurchases(const BookAccountPurchaseTable *purchaseTable,
                                         const QStringList &filePaths,
                                         const QString &companyCurrency,
                                         const CurrencyRateManager *currencyRateManager,
                                         QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditPurchases),
    m_purchaseTable(purchaseTable),
    m_currencyRateManager(currencyRateManager),
    m_filePaths(filePaths),
    m_companyCurrency(companyCurrency)
{
    ui->setupUi(this);
    _setupTable();
    _populateTable();
    _validateAll();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogEditPurchases::_onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogEditPurchases::~DialogEditPurchases()
{
    delete ui;
}

// ── table setup ────────────────────────────────────────────────────────────────

void DialogEditPurchases::_setupTable()
{
    ui->tableWidget->setColumnCount(17);
    ui->tableWidget->setHorizontalHeaderLabels({
        tr("File"), tr("Date"), tr("Account"), tr("Label"),
        tr("Supplier"), tr("Amount"), tr("Currency"),
        tr("VAT Amount"), tr("VAT Amt Company"), tr("VAT Rate %"),
        tr("VAT Currency"), tr("VAT Country"),
        tr("Country From"), tr("Country To"),
        tr("Inv."), tr("DDP"), tr("Status")
    });
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
}

void DialogEditPurchases::_populateTable()
{
    ui->tableWidget->setRowCount(m_filePaths.size());
    m_rows.clear();

    QStringList multiVatFiles;

    for (int i = 0; i < m_filePaths.size(); ++i) {
        const QString &filePath = m_filePaths[i];
        const QString fileName = QFileInfo(filePath).fileName();

        PurchaseInformation info;
        QString decodeError;
        try {
            info = PurchaseInvoiceManager::decode(fileName);
            info.filePath = filePath;
        } catch (const ExceptionWithTitleText &e) {
            decodeError = e.errorTitle() + ": " + e.errorText();
            info.filePath = filePath;
            info.originalExtension = QFileInfo(filePath).suffix();
        } catch (...) {
            decodeError = tr("Unknown decode error");
            info.filePath = filePath;
            info.originalExtension = QFileInfo(filePath).suffix();
        }

        if (info.vatTokens.size() > 1)
            multiVatFiles.append(fileName);

        // File column (read-only)
        auto *editFile = new QLineEdit(fileName);
        ui->tableWidget->setCellWidget(i, COL_FILE, editFile);

        // Date
        auto *dateEdit = new QDateEdit(info.date.isValid() ? info.date : QDate::currentDate());
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        ui->tableWidget->setCellWidget(i, COL_DATE, dateEdit);

        // Account
        auto *editAccount = new QLineEdit(info.account);
        ui->tableWidget->setCellWidget(i, COL_ACCOUNT, editAccount);

        // Label
        auto *editLabel = new QLineEdit(info.label);
        ui->tableWidget->setCellWidget(i, COL_LABEL, editLabel);

        // Supplier
        auto *editSupplier = new QLineEdit(info.accountSupplier);
        ui->tableWidget->setCellWidget(i, COL_SUPPLIER, editSupplier);

        // Amount
        auto *editAmount = new QLineEdit(info.rawTotalAmount);
        ui->tableWidget->setCellWidget(i, COL_AMOUNT, editAmount);

        // Currency
        auto *comboCurrency = _makeCurrencyCombo(info.currency);
        ui->tableWidget->setCellWidget(i, COL_CURRENCY, comboCurrency);

        // VAT Amount (source/invoice currency)
        auto *editVatAmount = new QLineEdit(info.rawVatAmount);
        ui->tableWidget->setCellWidget(i, COL_VAT_AMOUNT, editVatAmount);

        // VAT Amount in company currency (for dual-amount tokens only)
        QString rawVatAmountCompany;
        for (const auto &rateMap : std::as_const(info.country_vatRate_vatCompany)) {
            for (const double amt : std::as_const(rateMap)) {
                rawVatAmountCompany = QString::number(
                    rawVatAmountCompany.toDouble() + amt);
            }
        }
        auto *editVatAmountCompany = new QLineEdit(rawVatAmountCompany);
        editVatAmountCompany->setPlaceholderText(
            tr("Company amt (%1)").arg(m_companyCurrency));
        editVatAmountCompany->setToolTip(
            tr("VAT amount in company currency (%1). Fill when VAT currency "
               "differs from company currency to store exact exchange rate.")
                .arg(m_companyCurrency));
        ui->tableWidget->setCellWidget(i, COL_VAT_AMOUNT_COMPANY, editVatAmountCompany);

        // VAT Rate (extracted from the first token, e.g. "20" from "FR-TVA20-13.6EUR")
        const QString vatRate = info.vatTokens.isEmpty()
                                ? QString()
                                : extractVatRateFromToken(info.vatTokens.first());
        auto *editVatRate = new QLineEdit(vatRate);
        editVatRate->setPlaceholderText(tr("e.g. 20"));
        editVatRate->setToolTip(tr("VAT rate in % (e.g. 20 or 5.5)"));
        ui->tableWidget->setCellWidget(i, COL_VAT_RATE, editVatRate);

        // VAT Currency
        auto *comboVatCurrency = _makeCurrencyCombo(info.vatCurrency);
        ui->tableWidget->setCellWidget(i, COL_VAT_CURRENCY, comboVatCurrency);

        // VAT Country
        auto *comboVatCountry = _makeVatCountryCombo(info.vatCountry);
        ui->tableWidget->setCellWidget(i, COL_VAT_COUNTRY, comboVatCountry);

        // Country From / Country To (route for self-VAT)
        auto *comboCountryFrom = _makeCountryCodeCombo(info.countryCodeFrom);
        ui->tableWidget->setCellWidget(i, COL_COUNTRY_FROM, comboCountryFrom);

        auto *comboCountryTo = _makeCountryCodeCombo(info.countryCodeTo);
        ui->tableWidget->setCellWidget(i, COL_COUNTRY_TO, comboCountryTo);

        // Inventory checkbox (centered)
        auto *checkInventory = new QCheckBox;
        checkInventory->setChecked(info.isInventory);
        ui->tableWidget->setCellWidget(i, COL_INVENTORY, _makeCenteredCheckbox(checkInventory));

        // DDP checkbox (centered)
        auto *checkDdp = new QCheckBox;
        checkDdp->setChecked(info.isDDP);
        ui->tableWidget->setCellWidget(i, COL_DDP, _makeCenteredCheckbox(checkDdp));

        // Status
        auto *itemStatus = new QTableWidgetItem;
        itemStatus->setFlags(itemStatus->flags() & ~Qt::ItemIsEditable);
        if (!decodeError.isEmpty()) {
            itemStatus->setText(decodeError);
            itemStatus->setForeground(QBrush(Qt::red));
        } else {
            itemStatus->setText(tr("OK"));
            itemStatus->setForeground(QBrush(Qt::darkGreen));
        }
        ui->tableWidget->setItem(i, COL_STATUS, itemStatus);

        m_rows.append({info, dateEdit, editAccount, editLabel, editSupplier,
                       editAmount, comboCurrency, editVatAmount, editVatAmountCompany,
                       editVatRate, comboVatCurrency, comboVatCountry,
                       comboCountryFrom, comboCountryTo,
                       checkInventory, checkDdp});
    }

    ui->tableWidget->resizeColumnsToContents();
    // Give the status column some minimum width
    ui->tableWidget->setColumnWidth(COL_STATUS,
        qMax(ui->tableWidget->columnWidth(COL_STATUS), 140));

    if (!multiVatFiles.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Multiple VAT Rates"),
            tr("The following file(s) contain multiple VAT rates. "
               "This dialog can only display and edit a single VAT rate per file. "
               "The VAT Amount column will show the summed total, and saving will "
               "collapse the rates into one token. Use the single-file edit dialog "
               "to manage multiple VAT rates:\n\n%1")
                .arg(multiVatFiles.join(QStringLiteral("\n"))));
    }
}

QComboBox *DialogEditPurchases::_makeCurrencyCombo(const QString &invoiceCurrency) const
{
    QStringList currencies(CountriesEu::getCurrenciesWorld().begin(),
                           CountriesEu::getCurrenciesWorld().end());
    std::sort(currencies.begin(), currencies.end());
    currencies.removeAll(m_companyCurrency);
    currencies.prepend(m_companyCurrency);

    if (!invoiceCurrency.isEmpty() && !currencies.contains(invoiceCurrency)) {
        currencies.append(invoiceCurrency);
    }

    auto *combo = new QComboBox;
    combo->addItems(currencies);

    const QString toSelect = invoiceCurrency.isEmpty() ? m_companyCurrency : invoiceCurrency;
    const int idx = currencies.indexOf(toSelect);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }
    return combo;
}

QComboBox *DialogEditPurchases::_makeVatCountryCombo(const QString &vatCountry) const
{
    auto *combo = new QComboBox;
    combo->addItem(QString()); // empty = no country
    combo->addItems(CountriesEu::getCountries());

    const int idx = vatCountry.isEmpty() ? 0 : combo->findText(vatCountry);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    return combo;
}

QComboBox *DialogEditPurchases::_makeCountryCodeCombo(const QString &selected) const
{
    auto *combo = new QComboBox;
    combo->addItem(QString()); // empty = not set
    QStringList countries = CountriesEu::getCountries();
    // Ensure the current value is always selectable even if not in the standard list
    if (!selected.isEmpty() && !countries.contains(selected))
        countries.append(selected);
    combo->addItems(countries);
    const int idx = selected.isEmpty() ? 0 : combo->findText(selected);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    return combo;
}

QWidget *DialogEditPurchases::_makeCenteredCheckbox(QCheckBox *cb)
{
    auto *container = new QWidget;
    auto *layout = new QHBoxLayout(container);
    layout->addWidget(cb);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);
    return container;
}

// ── validation ─────────────────────────────────────────────────────────────────

bool DialogEditPurchases::_validateAll()
{
    bool allValid = true;

    for (int i = 0; i < m_rows.size(); ++i) {
        const RowData &row = m_rows[i];
        QStringList errors;

        if (!row.dateEdit->date().isValid()) {
            errors << tr("invalid date");
        }
        if (row.editAccount->text().trimmed().isEmpty()) {
            errors << tr("account is empty");
        }
        if (row.editLabel->text().trimmed().isEmpty()) {
            errors << tr("label is empty");
        }
        if (row.editSupplier->text().trimmed().isEmpty()) {
            errors << tr("supplier is empty");
        }
        if (qAbs(row.editAmount->text().trimmed().toDouble()) == 0.0) {
            errors << tr("amount is zero");
        }

        // Validate VAT rate against the purchase account table if available
        const QString vatRateStr         = row.editVatRate->text().trimmed();
        const QString vatAmountStr       = row.editVatAmount->text().trimmed();
        const QString vatAmountCompanyStr= row.editVatAmountCompany->text().trimmed();
        const QString vatCountryStr      = row.comboVatCountry->currentText().trimmed();
        if (!vatCountryStr.isEmpty() && !vatAmountStr.isEmpty() && m_purchaseTable) {
            QString rateStr = vatRateStr;
            const QString vatCurrencyRow = row.comboVatCurrency->currentText();
            const QString totalCurrency  = row.comboCurrency->currentText();
            const double  totalAmountAbs = qAbs(row.editAmount->text().trimmed().toDouble());
            const double  vatAmountAbs   = qAbs(vatAmountStr.toDouble());

            // When company VAT amount is provided, derive the rate from source amounts
            // (same currency as the invoice total) — no CurrencyRateManager needed.
            const bool hasDualAmount = !vatAmountCompanyStr.isEmpty()
                                       && vatCurrencyRow != m_companyCurrency;

            if (rateStr.isEmpty()) {
                if (hasDualAmount) {
                    // Source amounts are in the same currency as the total: exact rate.
                    const double netSource = totalAmountAbs - vatAmountAbs;
                    if (qAbs(netSource) > 1e-9) {
                        rateStr = QString::number((vatAmountAbs / netSource) * 100.0, 'g', 6);
                    }
                } else {
                    // Convert to a common currency via CurrencyRateManager.
                    const QDate invoiceDate = row.dateEdit->date();
                    double vatNorm   = vatAmountAbs;
                    double totalNorm = totalAmountAbs;
                    bool conversionOk = true;

                    if (m_currencyRateManager && vatCurrencyRow != totalCurrency) {
                        try {
                            const double rVat = (vatCurrencyRow == m_companyCurrency)
                                                ? 1.0
                                                : m_currencyRateManager->rate(vatCurrencyRow, m_companyCurrency, invoiceDate);
                            const double rTotal = (totalCurrency == m_companyCurrency)
                                                  ? 1.0
                                                  : m_currencyRateManager->rate(totalCurrency, m_companyCurrency, invoiceDate);
                            vatNorm   = vatAmountAbs  * rVat;
                            totalNorm = totalAmountAbs * rTotal;
                        } catch (...) {
                            errors << tr("currency rate unavailable for %1/%2 on %3 — cannot validate VAT rate")
                                      .arg(vatCurrencyRow, totalCurrency, invoiceDate.toString(Qt::ISODate));
                            conversionOk = false;
                        }
                    }

                    if (conversionOk) {
                        const double netNorm = totalNorm - vatNorm;
                        if (qAbs(netNorm) > 1e-9) {
                            rateStr = QString::number((vatNorm / netNorm) * 100.0, 'g', 6);
                        }
                    }
                }
            }

            if (rateStr.isEmpty()) {
                errors << tr("VAT rate is required when VAT country is set");
            } else {
                // Use 4 decimal precision in the rate (= 2 decimal places in %)
                const double rate = qRound(rateStr.toDouble() * 100.0) / 100.0 / 100.0;
                try {
                    const auto result = m_purchaseTable->getAccountsDebit6Closest(
                        vatCountryStr, rate, 0.9);
                    if (vatRateStr.isEmpty()) {
                        row.editVatRate->setText(
                            QString::number(result.matchedRate * 100.0, 'f', 2));
                    }
                } catch (const ExceptionWithTitleText &) {
                    errors << tr("unknown VAT rate %1% for %2").arg(QString::number(rate), vatCountryStr);
                } catch (...) {
                    errors << tr("unknown VAT rate %1% for %2").arg(QString::number(rate), vatCountryStr);
                }
            }
        }

        auto *statusItem = ui->tableWidget->item(i, COL_STATUS);
        if (!errors.isEmpty()) {
            allValid = false;
            statusItem->setText(errors.join(QStringLiteral(", ")));
            statusItem->setForeground(QBrush(Qt::red));
        } else {
            const QDate twoMonthsAgo = QDate::currentDate().addMonths(-2);
            if (row.dateEdit->date() < twoMonthsAgo) {
                statusItem->setText(tr("Old date"));
                statusItem->setForeground(QBrush(QColor(200, 100, 0)));
            } else {
                statusItem->setText(tr("OK"));
                statusItem->setForeground(QBrush(Qt::darkGreen));
            }
        }
    }

    return allValid;
}

void DialogEditPurchases::_onAccepted()
{
    if (!_validateAll()) {
        QMessageBox::warning(this, tr("Validation Errors"),
                             tr("Some rows have errors (shown in the Status column). "
                                "Please fix them before proceeding."));
        return;
    }

    // Single confirmation for any old dates
    const QDate twoMonthsAgo = QDate::currentDate().addMonths(-2);
    bool anyOldDate = false;
    for (const RowData &row : std::as_const(m_rows)) {
        if (row.dateEdit->date() < twoMonthsAgo) {
            anyOldDate = true;
            break;
        }
    }

    if (anyOldDate) {
        const auto answer = QMessageBox::question(
            this,
            tr("Old Invoice Dates"),
            tr("Some invoices have dates more than 2 months old. "
               "Are you sure these dates are correct?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    accept();
}

// ── result ─────────────────────────────────────────────────────────────────────

QList<PurchaseInformation> DialogEditPurchases::getInfos() const
{
    QList<PurchaseInformation> result;
    result.reserve(m_rows.size());

    for (const RowData &row : m_rows) {
        PurchaseInformation info = row.originalInfo;
        info.date           = row.dateEdit->date();
        info.account        = row.editAccount->text().trimmed();
        info.label          = row.editLabel->text().trimmed();
        info.accountSupplier = row.editSupplier->text().trimmed();
        info.rawTotalAmount = row.editAmount->text().trimmed();
        info.totalAmount    = info.rawTotalAmount.toDouble();
        info.currency       = row.comboCurrency->currentText();
        // Preserve sign: negative VAT is valid for refund invoices
        {
            const QString rawVat = row.editVatAmount->text().trimmed();
            if (!rawVat.isEmpty()) {
                const double vatVal = rawVat.toDouble();
                info.rawVatAmount = (qAbs(vatVal) > 0.0) ? rawVat : QString();
            } else {
                info.rawVatAmount.clear();
            }
        }
        info.vatCurrency    = row.comboVatCurrency->currentText();
        info.vatCountry     = row.comboVatCountry->currentText();
        info.vatTokens.clear();
        info.country_vatRate_vat.clear();
        info.country_vatRate_vatCompany.clear();
        if (!info.rawVatAmount.isEmpty()) {
            const QString rateStr    = row.editVatRate->text().trimmed();
            const QString companyStr = row.editVatAmountCompany->text().trimmed();
            const bool hasDualAmount = !companyStr.isEmpty()
                                       && info.vatCurrency != m_companyCurrency;
            const QString vatLabel = rateStr.isEmpty()
                                     ? QStringLiteral("TVA")
                                     : QStringLiteral("TVA") + rateStr;
            const double  sourceAmtAbs  = qAbs(info.rawVatAmount.toDouble());
            const double  companyAmtAbs = hasDualAmount ? qAbs(companyStr.toDouble()) : 0.0;
            if (!info.vatCountry.isEmpty()) {
                if (hasDualAmount) {
                    // Dual-amount token: company amount first, source amount second
                    // Preserve sign so that negative refunds encode correctly (double-dash)
                    const double companyAmtVal = companyStr.toDouble();
                    info.vatTokens << QString("%1-%2-%3%4_%5%6")
                                          .arg(info.vatCountry, vatLabel,
                                               QString::number(companyAmtVal),
                                               m_companyCurrency,
                                               info.rawVatAmount,
                                               info.vatCurrency);
                    if (!rateStr.isEmpty()) {
                        const QString rateKey = QString::number(rateStr.toDouble(), 'f', 2);
                        info.country_vatRate_vat[info.vatCountry][rateKey]        += sourceAmtAbs;
                        info.country_vatRate_vatCompany[info.vatCountry][rateKey] += companyAmtAbs;
                    }
                } else {
                    info.vatTokens << QString("%1-%2-%3%4")
                                          .arg(info.vatCountry, vatLabel,
                                               info.rawVatAmount, info.vatCurrency);
                    if (!rateStr.isEmpty()) {
                        const QString rateKey = QString::number(rateStr.toDouble(), 'f', 2);
                        info.country_vatRate_vat[info.vatCountry][rateKey] +=
                            info.rawVatAmount.toDouble();
                    }
                }
            } else {
                info.vatTokens << QString("%1-%2%3")
                                      .arg(vatLabel, info.rawVatAmount, info.vatCurrency);
            }
        }
        info.countryCodeFrom = row.comboCountryFrom->currentText();
        info.countryCodeTo   = row.comboCountryTo->currentText();
        info.isInventory    = row.checkInventory->isChecked();
        info.isDDP          = row.checkDdp->isChecked();
        // filePath is already in originalInfo
        result.append(info);
    }

    return result;
}
