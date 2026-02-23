#include "JournalEntryFactory.h"
#include "CurrencyRateManager.h"
#include "CompanyInfosTable.h"
#include "BooksAccountsSalesTable.h"
#include "BookAccountPurchaseTable.h"
#include "BookAccountSelfVatTable.h"
#include "JournalTable.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/Activity.h"
#include "ExceptionWithTitleText.h"
#include "books/AbstractBooksTableBank.h"
#include "banks/AbstractBankStatement.h"

JournalEntryFactory::JournalEntryFactory(
    const CurrencyRateManager *currencyRateManager,
    const CompanyInfosTable *companyInfos,
    const BooksAccountsSalesTable *saleBookAccounts,
    const BookAccountPurchaseTable *purchaseBookAccounts,
    const JournalTable *journalTable,
    const BookAccountSelfVatTable *selfVatBookAccounts)
    : m_currencyRateManager(currencyRateManager)
    , m_companyInfos(companyInfos)
    , m_saleBookAccounts(saleBookAccounts)
    , m_purchaseBookAccounts(purchaseBookAccounts)
    , m_journalTable(journalTable)
    , m_selfVatBookAccounts(selfVatBookAccounts)
{
}

QSharedPointer<JournalEntry> JournalEntryFactory::createEntry(PurchaseInformation purchaseInformation)
{
    QString companyCurrency = m_companyInfos->getCurrency();
    auto entry = QSharedPointer<JournalEntry>::create(purchaseInformation.date, companyCurrency);
    
    bool isRefund = purchaseInformation.totalAmount < 0;
    double totalAmountAbs = qAbs(purchaseInformation.totalAmount);
    
    // Get currency conversion rate if needed
    double currencyRate = 1.0;
    if (purchaseInformation.currency != companyCurrency) {
        currencyRate = m_currencyRateManager->rate(
            purchaseInformation.currency,
            companyCurrency,
            purchaseInformation.date
        );
    }
    double totalVat = 0.0;
    
    // Sum all VAT amounts
    for (const auto &country : purchaseInformation.country_vatRate_vat.keys()) {
        const auto &rateMap = purchaseInformation.country_vatRate_vat[country];
        for (const auto &vatAmount : rateMap.values()) {
            totalVat += vatAmount;
        }
    }
    
    double totalHT = totalAmountAbs - totalVat; // Hors Taxes (before tax)
    
    // Common title for all lines
    QString prefix = purchaseInformation.isInventory ? "Stock" : "Achat";
    QString flagDDP = purchaseInformation.isDDP ? " DDP" : "";
    QString countries = "";
    if (!purchaseInformation.countryCodeFrom.isEmpty() && !purchaseInformation.countryCodeTo.isEmpty()) {
        countries = QString(" %1->%2").arg(purchaseInformation.countryCodeFrom, purchaseInformation.countryCodeTo);
    }
    
    QString currencyInfo = "";
    if (purchaseInformation.currency != companyCurrency) {
        currencyInfo = QString(" (%1 %2)")
                       .arg(QString::number(purchaseInformation.totalAmount, 'f', 2),
                            purchaseInformation.currency);
    }
    
    QString labelWithSpaces = purchaseInformation.label;
    labelWithSpaces.replace("-", " ");
    labelWithSpaces[0] = labelWithSpaces[0].toUpper();
    QString head = prefix + flagDDP + countries;
    if (!purchaseInformation.accountSupplier.isEmpty())
        head += " " + purchaseInformation.accountSupplier;
    QString commonTitle = head + " - " + labelWithSpaces + currencyInfo;
    
    // Subtract extra amounts (subUntaxedAmount) from main expense
    double totalExtra = 0.0;
    for (double amt : std::as_const(purchaseInformation.subUntaxedAmount)) {
        totalExtra += amt;
    }
    double mainHT = totalHT - totalExtra;
    
    // Main expense entry line (Class 6 account)
    JournalEntry::EntryLine expenseLine;
    expenseLine.title = commonTitle;
    expenseLine.account = purchaseInformation.account;
    expenseLine.currency_amount[purchaseInformation.currency] = mainHT;
    
    // Add main expense to appropriate side
    if (isRefund) {
        entry->addCreditRight(expenseLine, purchaseInformation.currency, currencyRate);
    } else {
        entry->addDebitLeft(expenseLine, purchaseInformation.currency, currencyRate);
    }

    // Add entries for extra untaxed amounts
    for (auto it = purchaseInformation.subUntaxedAmount.constBegin(); it != purchaseInformation.subUntaxedAmount.constEnd(); ++it) {
        QString extraAccount = it.key();
        double extraAmount = it.value();
        
        JournalEntry::EntryLine extraLine;
        extraLine.title = commonTitle; // Use same title? Or append info? Requirement says "addDebitLeft per extra line".
        extraLine.account = extraAccount;
        extraLine.currency_amount[purchaseInformation.currency] = extraAmount;
        
        if (isRefund) {
            entry->addCreditRight(extraLine, purchaseInformation.currency, currencyRate);
        } else {
            entry->addDebitLeft(extraLine, purchaseInformation.currency, currencyRate);
        }
    }
    
    // VAT entries for each country/rate combination
    for (auto itCountry = purchaseInformation.country_vatRate_vat.constBegin();
         itCountry != purchaseInformation.country_vatRate_vat.constEnd(); ++itCountry) {
        
        const QString &country = itCountry.key();
        const auto &rateMap = itCountry.value();
        
        for (auto itRate = rateMap.constBegin(); itRate != rateMap.constEnd(); ++itRate) {
            double vatRate = itRate.key().toDouble();
            double vatAmount = itRate.value();
            
            // Fallback empty fields to company country for VAT lookup
            QString companyCountry = m_companyInfos->getCompanyCountryCode();
            QString countryCode = purchaseInformation.countryCodeTo.isEmpty() ? companyCountry : purchaseInformation.countryCodeTo;
            
            QString vatDebit6 = m_purchaseBookAccounts->getAccountsDebit6(purchaseInformation.vatCountry, vatRate);
            QString vatCredit4 = m_purchaseBookAccounts->getAccountsCredit4(purchaseInformation.vatCountry, vatRate);
            
            // Normal purchase VAT
            JournalEntry::EntryLine vatLine;
            vatLine.title = commonTitle;
            vatLine.account = vatDebit6;
            vatLine.currency_amount[purchaseInformation.currency] = vatAmount;
            
            if (isRefund) {
                entry->addCreditRight(vatLine, purchaseInformation.currency, currencyRate);
            } else {
                entry->addDebitLeft(vatLine, purchaseInformation.currency, currencyRate);
            }
        }
    }
    
    // Auto-liquidation (reverse charge / self-VAT)
    // Applies only when the invoice carries no VAT at all and the purchase route
    // is intracom (EU supplier → company) or extracom (non-EU → company).
    if (m_selfVatBookAccounts && purchaseInformation.country_vatRate_vat.isEmpty()) {
        const QString selfVatDeductible = m_selfVatBookAccounts->getAccountVatDeductible(
            purchaseInformation.countryCodeFrom, purchaseInformation.countryCodeTo);
        const QString selfVatDue = m_selfVatBookAccounts->getAccountVatDue(
            purchaseInformation.countryCodeFrom, purchaseInformation.countryCodeTo);

        if (!selfVatDeductible.isEmpty() && !selfVatDue.isEmpty()) {
            double selfVatAmount = purchaseInformation.rawVatAmount.toDouble();
            // When rawVatAmount is not provided at all (empty = not in the invoice filename),
            // apply the standard French TVA rate of 20% on the HT total.
            // An explicit "0" means the accountant chose no self-VAT; leave it at 0.
            if (selfVatAmount <= 0.0 && purchaseInformation.rawVatAmount.isEmpty())
                selfVatAmount = totalAmountAbs * 0.20;
            if (selfVatAmount > 0.0) {
                const QString selfCurrency = purchaseInformation.vatCurrency.isEmpty()
                                             ? purchaseInformation.currency
                                             : purchaseInformation.vatCurrency;
                double selfCurrencyRate = 1.0;
                if (selfCurrency != companyCurrency) {
                    selfCurrencyRate = m_currencyRateManager->rate(
                        selfCurrency, companyCurrency, purchaseInformation.date);
                }

                // Débit : TVA déductible sur achats (auto-liquidation)
                JournalEntry::EntryLine deductibleLine;
                deductibleLine.title = commonTitle;
                deductibleLine.account = selfVatDeductible;
                deductibleLine.currency_amount[selfCurrency] = selfVatAmount;
                if (isRefund) {
                    entry->addCreditRight(deductibleLine, selfCurrency, selfCurrencyRate);
                } else {
                    entry->addDebitLeft(deductibleLine, selfCurrency, selfCurrencyRate);
                }

                // Crédit : TVA due à l'état (auto-liquidation)
                JournalEntry::EntryLine dueLine;
                dueLine.title = commonTitle;
                dueLine.account = selfVatDue;
                dueLine.currency_amount[selfCurrency] = selfVatAmount;
                if (isRefund) {
                    entry->addDebitLeft(dueLine, selfCurrency, selfCurrencyRate);
                } else {
                    entry->addCreditRight(dueLine, selfCurrency, selfCurrencyRate);
                }
            }
        }
    }

    // Supplier account (Class 4 - Fournisseurs)
    JournalEntry::EntryLine supplierLine;
    supplierLine.title = commonTitle;
    supplierLine.account = purchaseInformation.accountSupplier; // Generic supplier account
    supplierLine.currency_amount[purchaseInformation.currency] = totalAmountAbs;
    
    if (isRefund) {
        entry->addDebitLeft(supplierLine, purchaseInformation.currency, currencyRate);
    } else {
        entry->addCreditRight(supplierLine, purchaseInformation.currency, currencyRate);
    }
    
    return entry;
}

QCoro::Task<QSharedPointer<JournalEntry>> JournalEntryFactory::createEntry(
    ActivitySource *source,
    const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing)
{
    if (shipmentAndRefunds.isEmpty()) {
        co_return nullptr;
    }
    
    QString companyCurrency = m_companyInfos->getCurrency();
    QString companyCountry = m_companyInfos->getCompanyCountryCode();
    QDate entryDate = shipmentAndRefunds.firstKey().date();
    
    auto entry = QSharedPointer<JournalEntry>::create(entryDate, companyCurrency);
    
    // Get journal code for this activity source
    QString journalCode = m_journalTable->getJournal(source);
    // QString customerAccount = m_journalTable->getCustomerAccount(source); // Removed
    
    // Aggregate all activities by TaxScheme, Country Routes, VAT rate and Currency
    struct VatKey {
        TaxScheme scheme;
        QString countryFrom;
        QString countryTo;
        double vatRate;
        QString currency;
        
        bool operator<(const VatKey &other) const {
            if (scheme != other.scheme) return scheme < other.scheme;
            if (countryFrom != other.countryFrom) return countryFrom < other.countryFrom;
            if (countryTo != other.countryTo) return countryTo < other.countryTo;
            if (qAbs(vatRate - other.vatRate) > 0.001) return vatRate < other.vatRate;
            return currency < other.currency;
        }
    };
    
    QMap<VatKey, double> revenueByVat;
    QMap<VatKey, double> vatByVat;
    // QMap<QString, double> totalByCurrency; // Removed, now per Account
    
    // Process all shipments
    for (const auto &shipment : shipmentAndRefunds.values()) {
        const QList<Activity> &activities = shipment->getActivities();
        
        for (const Activity &activity : activities) {
            VatKey key;
            key.scheme = activity.getTaxScheme();
            key.countryFrom = activity.getCountryCodeFrom();
            key.countryTo = activity.getCountryCodeTo();
            key.vatRate = activity.getVatRate() * 100.0; // Use percentage
            key.currency = activity.getCurrency();
            
            double amountUntaxed = activity.getAmountUntaxed();
            double amountTaxes = activity.getAmountTaxes();
            // double amountTotal = activity.getAmountTaxed();
            
            revenueByVat[key] += amountUntaxed;
            vatByVat[key] += amountTaxes;
            // totalByCurrency[key.currency] += amountTotal;
        }
    }
    
    // Common title for all lines in this entry
    // "Vente <Channel> <Subchannel> - <JournalCode>"
    QString commonTitle = QString("Vente %1 %2 - %3")
                          .arg(source->channel, source->subchannel, journalCode);

    QMap<QString, QMap<QString, double>> revenueByAccount;
    QMap<QString, QMap<QString, double>> vatByAccount;
    // Customer Account (Receivable) Aggregation: Account -> Currency -> Amount
    QMap<QString, QMap<QString, double>> receivableByAccount;


    // Resolve accounts and aggregate by Account ID
    for (auto it = revenueByVat.constBegin(); it != revenueByVat.constEnd(); ++it) {
        const VatKey &key = it.key();
        double revenueAmount = it.value();
        double vatAmount = vatByVat[key];
        
        // Resolve Accounts
        VatCountries vc = m_saleBookAccounts->resolveVatCountries(
            key.scheme, 
            companyCountry, 
            key.countryFrom, 
            key.countryTo
        );
        
        BooksAccountsSalesTable::Accounts accounts = co_await m_saleBookAccounts->getAccounts(vc, key.vatRate, callbackAddIfMissing);
        
        revenueByAccount[accounts.saleAccount][key.currency] += revenueAmount;
        vatByAccount[accounts.vatAccount][key.currency] += vatAmount;
        
        // Add total (Revenue + VAT) to Customer Account
        QString custAcc = accounts.customerAccount;
        if (custAcc.isEmpty()) {
            ExceptionWithTitleText exception(QObject::tr("Missing Customer Account"), 
                QObject::tr("No customer account found for sales entry (VAT scheme: %1, Rate: %2)")
                .arg(taxSchemeToString(key.scheme)).arg(key.vatRate));
            exception.raise();
        }
        
        receivableByAccount[custAcc][key.currency] += (revenueAmount + vatAmount);
    }
    
    // Helper to add lines
    auto addLines = [&](const QMap<QString, QMap<QString, double>> &map, bool isCredit) {
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QString &account = it.key();
            const auto &currencyMap = it.value();
            
            for (auto itCurr = currencyMap.constBegin(); itCurr != currencyMap.constEnd(); ++itCurr) {
                const QString &currency = itCurr.key();
                double amount = itCurr.value();
                
                if (qAbs(amount) <= 0.01) continue;
                
                double currencyRate = 1.0;
                if (currency != companyCurrency) {
                    currencyRate = m_currencyRateManager->rate(currency, companyCurrency, entryDate);
                }
                
                JournalEntry::EntryLine line;
                line.title = commonTitle;
                line.account = account;
                line.currency_amount[currency] = amount;
                
                if (isCredit) {
                     entry->addCreditRight(line, currency, currencyRate);
                } else {
                     entry->addDebitLeft(line, currency, currencyRate);
                }
            }
        }
    };
    
    addLines(revenueByAccount, true); // Credit Revenue
    addLines(vatByAccount, true);     // Credit VAT
    addLines(receivableByAccount, false); // Debit Customer
    
    co_return entry;
}

QCoro::Task<QSharedPointer<JournalEntry>> JournalEntryFactory::createEntry(
        QSharedPointer<Shipment> shipmentOrRefund
        , std::function<QCoro::Task<bool> (const QString &, const QString &)> callbackAddIfMissing)
{
    if (!shipmentOrRefund) {
        co_return nullptr;
    }
    
    const QList<Activity> &activities = shipmentOrRefund->getActivities();
    if (activities.isEmpty()) {
        co_return nullptr;
    }
    
    QString companyCurrency = m_companyInfos->getCurrency();
    QString companyCountry = m_companyInfos->getCompanyCountryCode();
    QDate entryDate = activities.first().getDateTime().date();
    QString shipmentId = shipmentOrRefund->getId();
    
    auto entry = QSharedPointer<JournalEntry>::create(entryDate, companyCurrency);
    
    // Aggregate by account
    QMap<QString, QMap<QString, double>> revenueByAccount;  // account -> currency -> amount
    QMap<QString, QMap<QString, double>> vatByAccount;
    QMap<QString, QMap<QString, double>> receivableByAccount;
    
    for (const Activity &activity : activities) {
        QString currency = activity.getCurrency();
        double amountUntaxed = activity.getAmountUntaxed();
        double amountTaxes = activity.getAmountTaxes();
        double vatRate = activity.getVatRate() * 100.0;
        
        // Resolve accounts
        VatCountries vc = m_saleBookAccounts->resolveVatCountries(
            activity.getTaxScheme(),
            companyCountry,
            activity.getCountryCodeFrom(),
            activity.getCountryCodeTo()
        );
        
        BooksAccountsSalesTable::Accounts accounts = co_await m_saleBookAccounts->getAccounts(vc, vatRate, callbackAddIfMissing);
        
        revenueByAccount[accounts.saleAccount][currency] += amountUntaxed;
        vatByAccount[accounts.vatAccount][currency] += amountTaxes;
        
        QString custAcc = accounts.customerAccount.isEmpty() ? "411000" : accounts.customerAccount;
        receivableByAccount[custAcc][currency] += (amountUntaxed + amountTaxes);
    }
    
    // Common title with shipment ID
    QString commonTitle = QString("Vente Service %1").arg(shipmentId);
    
    // Helper to add lines
    auto addLines = [&](const QMap<QString, QMap<QString, double>> &map, bool isCredit) {
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QString &account = it.key();
            const auto &currencyMap = it.value();
            
            for (auto itCurr = currencyMap.constBegin(); itCurr != currencyMap.constEnd(); ++itCurr) {
                const QString &currency = itCurr.key();
                double amount = itCurr.value();
                
                if (qAbs(amount) <= 0.01) continue;
                
                double currencyRate = 1.0;
                if (currency != companyCurrency) {
                    currencyRate = m_currencyRateManager->rate(currency, companyCurrency, entryDate);
                }
                
                JournalEntry::EntryLine line;
                line.title = commonTitle;
                line.account = account;
                line.currency_amount[currency] = amount;
                
                if (isCredit) {
                     entry->addCreditRight(line, currency, currencyRate);
                } else {
                     entry->addDebitLeft(line, currency, currencyRate);
                }
            }
        }
    };
    
    addLines(revenueByAccount, true);    // Credit Revenue
    addLines(vatByAccount, true);        // Credit VAT
    addLines(receivableByAccount, false); // Debit Customer
    
    co_return entry;
}

QSharedPointer<JournalEntry> JournalEntryFactory::createEntry(
        const AbstractBooksTableBank *bankTable
        , const QString &nonBankAccount
        , int row)
{
    if (!bankTable || row < 0) {
        return nullptr;
    }
    
    // Get data from bank table using helper methods
    QDate date = bankTable->getDate(row);
    double amount = bankTable->getAmount(row);
    QString currency = bankTable->getCurrency(row);
    QString label = bankTable->getLabel(row);
    
    if (!date.isValid() || qAbs(amount) < 0.001) {
        return nullptr;
    }
    
    QString companyCurrency = m_companyInfos->getCurrency();
    auto entry = QSharedPointer<JournalEntry>::create(date, companyCurrency);
    
    // Get bank account from bank statement
    const AbstractBankStatement *bankStatement = bankTable->getBankStatement();
    QString bankAccount = bankStatement ? bankStatement->defaultAccount() : "512000";
    QString journalCode = bankStatement ? bankStatement->defaultJournal() : "BQ";
    
    // Get currency conversion rate if needed
    double currencyRate = 1.0;
    if (currency != companyCurrency) {
        currencyRate = m_currencyRateManager->rate(currency, companyCurrency, date);
    }
    
    double amountAbs = qAbs(amount);
    bool isCredit = amount > 0; // Positive = money coming in (credit to bank)
    
    // Build common title
    QString currencyInfo = "";
    if (currency != companyCurrency) {
        currencyInfo = QString(" (%1 %2)")
                       .arg(QString::number(amount, 'f', 2), currency);
    }
    QString commonTitle = QString("%1 - %2%3").arg(journalCode, label, currencyInfo);
    
    // Bank account line
    JournalEntry::EntryLine bankLine;
    bankLine.title = commonTitle;
    bankLine.account = bankAccount;
    bankLine.currency_amount[currency] = amountAbs;
    
    // Non-bank account line (counterpart)
    JournalEntry::EntryLine nonBankLine;
    nonBankLine.title = commonTitle;
    nonBankLine.account = nonBankAccount;
    nonBankLine.currency_amount[currency] = amountAbs;
    
    if (isCredit) {
        // Money coming in: Debit Bank, Credit Non-Bank (e.g., Revenue)
        entry->addDebitLeft(bankLine, currency, currencyRate);
        entry->addCreditRight(nonBankLine, currency, currencyRate);
    } else {
        // Money going out: Credit Bank, Debit Non-Bank (e.g., Expense)
        entry->addCreditRight(bankLine, currency, currencyRate);
        entry->addDebitLeft(nonBankLine, currency, currencyRate);
    }
    
    return entry;
}
