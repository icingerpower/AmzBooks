#include "JournalEntryFactory.h"
#include "CurrencyRateManager.h"
#include "CompanyInfosTable.h"
#include "SaleBookAccountsTable.h"
#include "PurchaseBookAccountsTable.h"
#include "JournalTable.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/Activity.h"

JournalEntryFactory::JournalEntryFactory(
    const CurrencyRateManager *currencyRateManager,
    const CompanyInfosTable *companyInfos,
    const SaleBookAccountsTable *saleBookAccounts,
    const PurchaseBookAccountsTable *purchaseBookAccounts,
    const JournalTable *journalTable)
    : m_currencyRateManager(currencyRateManager)
    , m_companyInfos(companyInfos)
    , m_saleBookAccounts(saleBookAccounts)
    , m_purchaseBookAccounts(purchaseBookAccounts)
    , m_journalTable(journalTable)
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
    
    QString commonTitle = QString("%1%2%3 %4 - %5%6")
                          .arg(prefix,
                               flagDDP,
                               countries,
                               purchaseInformation.supplier,
                               purchaseInformation.label,
                               currencyInfo);
    
    // Main expense entry line (Class 6 account)
    JournalEntry::EntryLine expenseLine;
    expenseLine.title = commonTitle;
    expenseLine.account = purchaseInformation.account;
    expenseLine.currency_amount[purchaseInformation.currency] = totalHT;
    
    // Add expense to appropriate side
    if (isRefund) {
        entry->addCreditRight(expenseLine, purchaseInformation.currency, currencyRate);
    } else {
        entry->addDebitLeft(expenseLine, purchaseInformation.currency, currencyRate);
    }
    
    // VAT entries for each country/rate combination
    for (auto itCountry = purchaseInformation.country_vatRate_vat.constBegin();
         itCountry != purchaseInformation.country_vatRate_vat.constEnd(); ++itCountry) {
        
        const QString &country = itCountry.key();
        const auto &rateMap = itCountry.value();
        
        for (auto itRate = rateMap.constBegin(); itRate != rateMap.constEnd(); ++itRate) {
            const QString &rateKey = itRate.key();
            double vatAmount = itRate.value();
            
            // Get VAT accounts from PurchaseBookAccountsTable
            QString vatDebit6 = m_purchaseBookAccounts->getAccountsDebit6(country);
            QString vatCredit4 = m_purchaseBookAccounts->getAccountsCredit4(country);
            
            // Check if double VAT entry is needed (autoliquidation)
            QString companyCountry = m_companyInfos->getCompanyCountryCode();
            bool needsDoubleVat = m_purchaseBookAccounts->isDoubleVatEntryNeeded(
                purchaseInformation.countryCodeFrom,
                purchaseInformation.countryCodeTo.isEmpty() ? companyCountry : purchaseInformation.countryCodeTo
            );
            
            if (needsDoubleVat) {
                // Autoliquidation: TVA déductible (Debit) and TVA à payer (Credit)
                JournalEntry::EntryLine vatDeductibleLine;
                vatDeductibleLine.title = commonTitle;
                vatDeductibleLine.account = vatDebit6;
                vatDeductibleLine.currency_amount[purchaseInformation.currency] = vatAmount;
                
                JournalEntry::EntryLine vatPayableLine;
                vatPayableLine.title = commonTitle;
                vatPayableLine.account = vatCredit4;
                vatPayableLine.currency_amount[purchaseInformation.currency] = vatAmount;
                
                if (isRefund) {
                    entry->addCreditRight(vatDeductibleLine, purchaseInformation.currency, currencyRate);
                    entry->addDebitLeft(vatPayableLine, purchaseInformation.currency, currencyRate);
                } else {
                    entry->addDebitLeft(vatDeductibleLine, purchaseInformation.currency, currencyRate);
                    entry->addCreditRight(vatPayableLine, purchaseInformation.currency, currencyRate);
                }
            } else {
                // Normal purchase VAT (déductible only)
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
    }
    
    // Supplier account (Class 4 - Fournisseurs)
    JournalEntry::EntryLine supplierLine;
    supplierLine.title = commonTitle;
    supplierLine.account = purchaseInformation.supplier; // Generic supplier account
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
    QString customerAccount = m_journalTable->getCustomerAccount(source);
    
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
    QMap<QString, double> totalByCurrency;
    
    // Process all shipments
    for (const auto &shipment : shipmentAndRefunds.values()) {
        const QList<Activity> &activities = shipment->getActivities();
        
        for (const Activity &activity : activities) {
            VatKey key;
            key.scheme = activity.getTaxScheme();
            key.countryFrom = activity.getCountryCodeFrom();
            key.countryTo = activity.getCountryCodeTo();
            key.vatRate = activity.getVatRate() * 100.0;
            key.currency = activity.getCurrency();
            
            double amountUntaxed = activity.getAmountUntaxed();
            double amountTaxes = activity.getAmountTaxes();
            double amountTotal = activity.getAmountTaxed();
            
            revenueByVat[key] += amountUntaxed;
            vatByVat[key] += amountTaxes;
            totalByCurrency[key.currency] += amountTotal;
        }
    }
    
    // Common title for all lines in this entry
    // "Vente <Channel> <Subchannel> - <JournalCode>"
    QString commonTitle = QString("Vente %1 %2 - %3")
                          .arg(source->channel, source->subchannel, journalCode);

    QMap<QString, QMap<QString, double>> revenueByAccount;
    QMap<QString, QMap<QString, double>> vatByAccount;

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
        
        SaleBookAccountsTable::Accounts accounts = co_await m_saleBookAccounts->getAccounts(vc, key.vatRate, callbackAddIfMissing);
        
        revenueByAccount[accounts.saleAccount][key.currency] += revenueAmount;
        vatByAccount[accounts.vatAccount][key.currency] += vatAmount;
    }
    
    // Helper to add lines
    auto addLines = [&](const QMap<QString, QMap<QString, double>> &map, bool checkThreshold) {
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QString &account = it.key();
            const auto &currencyMap = it.value();
            
            for (auto itCurr = currencyMap.constBegin(); itCurr != currencyMap.constEnd(); ++itCurr) {
                const QString &currency = itCurr.key();
                double amount = itCurr.value();
                
                if (checkThreshold && qAbs(amount) <= 0.01) continue;
                
                double currencyRate = 1.0;
                if (currency != companyCurrency) {
                    currencyRate = m_currencyRateManager->rate(currency, companyCurrency, entryDate);
                }
                
                JournalEntry::EntryLine line;
                line.title = commonTitle;
                line.account = account;
                line.currency_amount[currency] = amount;
                
                entry->addCreditRight(line, currency, currencyRate);
            }
        }
    };
    
    addLines(revenueByAccount, false);
    addLines(vatByAccount, true);
    
    // Customer receivable entry (Debit - Class 4)
    for (auto it = totalByCurrency.constBegin(); it != totalByCurrency.constEnd(); ++it) {
        const QString &currency = it.key();
        double totalAmount = it.value();
        
        double currencyRate = 1.0;
        if (currency != companyCurrency) {
            currencyRate = m_currencyRateManager->rate(currency, companyCurrency, entryDate);
        }
        
        JournalEntry::EntryLine customerLine;
        customerLine.title = commonTitle;
        customerLine.account = customerAccount.isEmpty() ? "411000" : customerAccount; // Fallback if empty, though getCustomerAccount should return something
        customerLine.currency_amount[currency] = totalAmount;
        
        entry->addDebitLeft(customerLine, currency, currencyRate);
    }
    
    co_return entry;
}
