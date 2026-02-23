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

#include "CountriesEu.h"
#include "ExceptionWithTitleText.h"

DialogEditPurchases::DialogEditPurchases(const BookAccountPurchaseTable *purchaseTable,
                                         const QStringList &filePaths,
                                         const QString &companyCurrency,
                                         QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditPurchases),
    m_purchaseTable(purchaseTable),
    m_filePaths(filePaths),
    m_companyCurrency(companyCurrency)
{
    ui->setupUi(this);
    _setupTable();
    _populateTable();

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
    ui->tableWidget->setColumnCount(13);
    ui->tableWidget->setHorizontalHeaderLabels({
        tr("File"), tr("Date"), tr("Account"), tr("Label"),
        tr("Supplier"), tr("Amount"), tr("Currency"),
        tr("VAT Amount"), tr("VAT Currency"), tr("VAT Country"),
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
        auto *itemFile = new QTableWidgetItem(fileName);
        itemFile->setFlags(itemFile->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(i, COL_FILE, itemFile);

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

        // VAT Amount
        auto *editVatAmount = new QLineEdit(info.rawVatAmount);
        ui->tableWidget->setCellWidget(i, COL_VAT_AMOUNT, editVatAmount);

        // VAT Currency
        auto *comboVatCurrency = _makeCurrencyCombo(info.vatCurrency);
        ui->tableWidget->setCellWidget(i, COL_VAT_CURRENCY, comboVatCurrency);

        // VAT Country
        auto *comboVatCountry = _makeVatCountryCombo(info.vatCountry);
        ui->tableWidget->setCellWidget(i, COL_VAT_COUNTRY, comboVatCountry);

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
                       editAmount, comboCurrency, editVatAmount, comboVatCurrency,
                       comboVatCountry, checkInventory, checkDdp});
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
    QStringList currencies = CountriesEu::getCurrenciesWorld();

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
        info.rawVatAmount   = row.editVatAmount->text().trimmed();
        info.vatCurrency    = row.comboVatCurrency->currentText();
        info.vatCountry     = row.comboVatCountry->currentText();
        info.vatTokens.clear();
        info.country_vatRate_vat.clear();
        if (!info.rawVatAmount.isEmpty()) {
            if (!info.vatCountry.isEmpty()) {
                info.vatTokens << QString("%1-TVA-%2%3").arg(info.vatCountry, info.rawVatAmount, info.vatCurrency);
            } else {
                info.vatTokens << QString("TVA-%1%2").arg(info.rawVatAmount, info.vatCurrency);
            }
        }
        info.isInventory    = row.checkInventory->isChecked();
        info.isDDP          = row.checkDdp->isChecked();
        // filePath is already in originalInfo
        result.append(info);
    }

    return result;
}
