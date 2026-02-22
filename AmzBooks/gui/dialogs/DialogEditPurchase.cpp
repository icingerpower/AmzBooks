#include "DialogEditPurchase.h"
#include "ui_DialogEditPurchase.h"

#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QtMath>
#include "CountriesEu.h"

// ── helpers ────────────────────────────────────────────────────────────────────

// Parse a VAT token (e.g. "FR-TVA20-13.6EUR" or "TVA-13.6EUR") into components.
static void parseVatToken(const QString &token,
                           QString &country, QString &rate,
                           QString &amount,  QString &currency)
{
    country.clear(); rate.clear(); amount.clear(); currency.clear();

    static QRegularExpression regexAmt(QStringLiteral("([0-9.]+)([A-Z]*)"));
    static QRegularExpression regexRate(QStringLiteral("[0-9.]+"));

    const QStringList parts = token.split(QLatin1Char('-'));
    if (parts.size() >= 3) {
        country = parts[0];
        QRegularExpressionMatch m = regexRate.match(parts[1]); // label e.g. "TVA20"
        if (m.hasMatch()) rate = m.captured(0);
        QRegularExpressionMatch mAmt = regexAmt.match(parts.last());
        if (mAmt.hasMatch()) { amount = mAmt.captured(1); currency = mAmt.captured(2); }
    } else if (parts.size() == 2) {
        // "TVA-13.6EUR" — no country
        QRegularExpressionMatch mAmt = regexAmt.match(parts.last());
        if (mAmt.hasMatch()) { amount = mAmt.captured(1); currency = mAmt.captured(2); }
    }
}

// ── constructor / destructor ───────────────────────────────────────────────────

DialogEditPurchase::DialogEditPurchase(const PurchaseInformation &info,
                                       const QString &companyCurrency,
                                       QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditPurchase),
    m_info(info),
    m_companyCurrency(companyCurrency)
{
    ui->setupUi(this);

    ui->dateEdit->setDate(info.date);
    ui->editAccount->setText(info.account);
    ui->editLabel->setText(info.label);
    ui->editSupplier->setText(info.accountSupplier);
    ui->editAmount->setText(info.rawTotalAmount);
    ui->checkInventory->setChecked(info.isInventory);
    ui->checkDdp->setChecked(info.isDDP);

    _setupCurrencies(companyCurrency, info.currency);

    // Build dynamic VAT layout on the placeholder widget from the .ui
    m_vatLayout = new QVBoxLayout(ui->vatEntriesWidget);
    m_vatLayout->setContentsMargins(0, 0, 0, 0);
    m_vatLayout->setSpacing(4);

    // Populate one row per VAT token; fall back to one empty row
    if (info.vatTokens.isEmpty()) {
        const QString defaultCurrency = info.vatCurrency.isEmpty() ? companyCurrency : info.vatCurrency;
        _addVatRow({}, {}, info.rawVatAmount, defaultCurrency);
    } else {
        for (const QString &token : info.vatTokens) {
            QString c, r, a, cur;
            parseVatToken(token, c, r, a, cur);
            if (cur.isEmpty())
                cur = info.vatCurrency.isEmpty() ? companyCurrency : info.vatCurrency;
            _addVatRow(c, r, a, cur);
        }
    }

    connect(ui->btnAddVat, &QPushButton::clicked, this, [this]() {
        _addVatRow({}, {}, {}, m_companyCurrency);
    });
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogEditPurchase::_onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogEditPurchase::~DialogEditPurchase()
{
    delete ui;
}

// ── currency / country combo helpers ──────────────────────────────────────────

void DialogEditPurchase::_setupCurrencies(const QString &companyCurrency, const QString &invoiceCurrency)
{
    QStringList currencies = CountriesEu::getCurrenciesWorld();
    currencies.removeAll(companyCurrency);
    currencies.prepend(companyCurrency);
    if (!invoiceCurrency.isEmpty() && !currencies.contains(invoiceCurrency))
        currencies.append(invoiceCurrency);

    ui->comboCurrency->addItems(currencies);

    const QString toSelect = invoiceCurrency.isEmpty() ? companyCurrency : invoiceCurrency;
    const int idx = currencies.indexOf(toSelect);
    if (idx >= 0) ui->comboCurrency->setCurrentIndex(idx);
}

QComboBox *DialogEditPurchase::_makeCountryCombo(const QString &selected) const
{
    auto *combo = new QComboBox;
    combo->addItem(QString()); // empty = no country
    combo->addItems(CountriesEu::getCountries());
    const int idx = selected.isEmpty() ? 0 : combo->findText(selected);
    if (idx >= 0) combo->setCurrentIndex(idx);
    return combo;
}

QComboBox *DialogEditPurchase::_makeVatCurrencyCombo(const QString &selected) const
{
    QStringList currencies = CountriesEu::getCurrenciesWorld();
    currencies.removeAll(m_companyCurrency);
    currencies.prepend(m_companyCurrency);
    if (!selected.isEmpty() && !currencies.contains(selected))
        currencies.append(selected);

    auto *combo = new QComboBox;
    combo->addItems(currencies);
    const QString toSelect = selected.isEmpty() ? m_companyCurrency : selected;
    const int idx = currencies.indexOf(toSelect);
    if (idx >= 0) combo->setCurrentIndex(idx);
    return combo;
}

// ── dynamic VAT rows ───────────────────────────────────────────────────────────

void DialogEditPurchase::_addVatRow(const QString &country, const QString &rate,
                                     const QString &amount,  const QString &currency)
{
    VatRow row;
    row.rowWidget = new QWidget;
    auto *hLayout = new QHBoxLayout(row.rowWidget);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(4);

    row.comboCountry = _makeCountryCombo(country);
    row.comboCountry->setFixedWidth(80);
    row.comboCountry->setToolTip(tr("Country (optional)"));

    row.editRate = new QLineEdit(rate);
    row.editRate->setPlaceholderText(tr("Rate %"));
    row.editRate->setFixedWidth(65);
    row.editRate->setToolTip(tr("VAT rate in % (e.g. 20 or 5.5), optional"));

    row.editAmount = new QLineEdit(amount);
    row.editAmount->setPlaceholderText(tr("Amount"));
    row.editAmount->setToolTip(tr("VAT amount"));

    row.comboCurrency = _makeVatCurrencyCombo(currency);
    row.comboCurrency->setFixedWidth(90);

    row.btnRemove = new QPushButton(tr("×"));
    row.btnRemove->setFixedWidth(28);
    row.btnRemove->setToolTip(tr("Remove this VAT entry"));

    hLayout->addWidget(row.comboCountry);
    hLayout->addWidget(row.editRate);
    hLayout->addWidget(row.editAmount);
    hLayout->addWidget(row.comboCurrency);
    hLayout->addWidget(row.btnRemove);

    m_vatRows.append(row);
    m_vatLayout->addWidget(row.rowWidget);

    connect(row.btnRemove, &QPushButton::clicked, this,
            [this, rowWidget = row.rowWidget]() { _removeVatRow(rowWidget); });
}

void DialogEditPurchase::_removeVatRow(QWidget *rowWidget)
{
    for (int i = 0; i < m_vatRows.size(); ++i) {
        if (m_vatRows[i].rowWidget == rowWidget) {
            m_vatRows.removeAt(i);
            m_vatLayout->removeWidget(rowWidget);
            rowWidget->deleteLater();
            break;
        }
    }
}

// ── result ─────────────────────────────────────────────────────────────────────

PurchaseInformation DialogEditPurchase::getInfo() const
{
    PurchaseInformation info = m_info;
    info.date            = ui->dateEdit->date();
    info.account         = ui->editAccount->text().trimmed();
    info.label           = ui->editLabel->text().trimmed();
    info.accountSupplier = ui->editSupplier->text().trimmed();
    info.rawTotalAmount  = ui->editAmount->text().trimmed();
    info.totalAmount     = info.rawTotalAmount.toDouble();
    info.currency        = ui->comboCurrency->currentText();
    info.isInventory     = ui->checkInventory->isChecked();
    info.isDDP           = ui->checkDdp->isChecked();

    // Rebuild VAT data from dynamic rows
    info.vatTokens.clear();
    info.country_vatRate_vat.clear();

    static QRegularExpression regexAmt(QStringLiteral("([0-9.]+)([A-Z]*)"));

    for (const VatRow &row : m_vatRows) {
        const QString country  = row.comboCountry->currentText();
        const QString rate     = row.editRate->text().trimmed();
        const QString amount   = row.editAmount->text().trimmed();
        const QString currency = row.comboCurrency->currentText();

        if (amount.isEmpty()) continue;

        const QString vatLabel = rate.isEmpty() ? QStringLiteral("TVA")
                                                 : QStringLiteral("TVA") + rate;
        QString token;
        if (country.isEmpty()) {
            token = QStringLiteral("%1-%2%3").arg(vatLabel, amount, currency);
        } else {
            token = QStringLiteral("%1-%2-%3%4").arg(country, vatLabel, amount, currency);
            const QString rateKey = rate.isEmpty()
                                    ? QString()
                                    : QString::number(rate.toDouble(), 'f', 2);
            info.country_vatRate_vat[country][rateKey] += amount.toDouble();
        }
        info.vatTokens << token;
    }

    // Derive simple aggregated fields for backwards compatibility
    if (!info.vatTokens.isEmpty()) {
        double  totalVat      = 0.0;
        QString firstCurrency;
        QString firstCountry;

        for (const QString &token : std::as_const(info.vatTokens)) {
            QRegularExpressionMatch m = regexAmt.match(token.split(QLatin1Char('-')).last());
            if (m.hasMatch()) {
                totalVat += m.captured(1).toDouble();
                if (firstCurrency.isEmpty()) firstCurrency = m.captured(2);
            }
        }
        const QStringList firstParts = info.vatTokens.first().split(QLatin1Char('-'));
        if (firstParts.size() >= 3) firstCountry = firstParts[0];

        info.rawVatAmount = totalVat > 0.0 ? QString::number(totalVat) : QString();
        info.vatCurrency  = firstCurrency;
        info.vatCountry   = firstCountry;
    } else {
        info.rawVatAmount.clear();
        info.vatCurrency.clear();
        info.vatCountry.clear();
    }

    return info;
}

// ── validation ─────────────────────────────────────────────────────────────────

void DialogEditPurchase::_onAccepted()
{
    if (!ui->dateEdit->date().isValid()) {
        QMessageBox::warning(this, tr("Invalid Date"),
                             tr("Please enter a valid date."));
        return;
    }
    if (ui->editAccount->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Account must not be empty."));
        return;
    }
    if (ui->editLabel->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Label must not be empty."));
        return;
    }
    if (ui->editSupplier->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Supplier must not be empty."));
        return;
    }
    if (qAbs(ui->editAmount->text().trimmed().toDouble()) == 0.0) {
        QMessageBox::warning(this, tr("Invalid Amount"),
                             tr("Amount must not be zero."));
        return;
    }
    accept();
}
