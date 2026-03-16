#include "books/JournalEntryFactory.h"
#include "books/VatResolver.h"
#include "CurrencyRateManager.h"
#include "CompanyInfosTable.h"
#include "BooksAccountsSalesTable.h"
#include "BookAccountPurchaseTable.h"
#include "BookAccountSelfVatTable.h"
#include "JournalTable.h"
#include "AmzPaymentSettings.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/Activity.h"
#include "ExceptionWithTitleText.h"
#include "books/AbstractBooksTableBank.h"
#include "banks/AbstractBankStatement.h"
#include "inventory/InventoryMoveTree.h"

JournalEntryFactory::JournalEntryFactory(
    const CurrencyRateManager *currencyRateManager,
    const CompanyInfosTable *companyInfos,
    const BooksAccountsSalesTable *saleBookAccounts,
    const BookAccountPurchaseTable *purchaseBookAccounts,
    const JournalTable *journalTable,
    const BookAccountSelfVatTable *selfVatBookAccounts,
    const AmzPaymentSettings *amzPaymentSettings)
    : m_currencyRateManager(currencyRateManager)
    , m_companyInfos(companyInfos)
    , m_saleBookAccounts(saleBookAccounts)
    , m_purchaseBookAccounts(purchaseBookAccounts)
    , m_journalTable(journalTable)
    , m_selfVatBookAccounts(selfVatBookAccounts)
    , m_amzPaymentSettings(amzPaymentSettings)
{
}


QSharedPointer<JournalEntry> JournalEntryFactory::createEntry(
    const PurchaseInformation &purchaseInformation) const
{
    QString companyCurrency = m_companyInfos->getCurrency();
    auto entry = QSharedPointer<JournalEntry>::create(purchaseInformation.date, companyCurrency);
    
    bool isRefund = purchaseInformation.totalAmount < 0;
    double totalAmountAbs = qAbs(purchaseInformation.totalAmount);
    
    // Get currency conversion rate if needed (total currency → company currency)
    double currencyRate = 1.0;
    if (purchaseInformation.currency != companyCurrency) {
        currencyRate = m_currencyRateManager->rate(
            purchaseInformation.currency,
            companyCurrency,
            purchaseInformation.date
        );
    }

    // When dual-amount VAT tokens are present the company/source ratio gives the
    // exact exchange rate that the supplier used for this specific invoice — more
    // precise than the generic CRM daily rate.  Override currencyRate so that the
    // expense and supplier lines (computed below) use the same rate as the VAT line.
    if (purchaseInformation.currency != companyCurrency
            && !purchaseInformation.country_vatRate_vatCompany.isEmpty()) {
        double totalSourceVat  = 0.0;
        double totalCompanyVat = 0.0;
        for (auto itC = purchaseInformation.country_vatRate_vatCompany.constBegin();
             itC != purchaseInformation.country_vatRate_vatCompany.constEnd(); ++itC) {
            const QString &cKey = itC.key();
            for (auto itR = itC.value().constBegin(); itR != itC.value().constEnd(); ++itR) {
                const QString &rKey = itR.key();
                if (purchaseInformation.country_vatRate_vat.contains(cKey)
                        && purchaseInformation.country_vatRate_vat[cKey].contains(rKey)) {
                    totalSourceVat  += qAbs(purchaseInformation.country_vatRate_vat[cKey][rKey]);
                    totalCompanyVat += qAbs(itR.value());
                }
            }
        }
        if (totalSourceVat > 1e-9) {
            currencyRate = totalCompanyVat / totalSourceVat;
        }
    }

    // Get VAT currency conversion rate — may differ from the total currency
    QString vatCurrency = purchaseInformation.vatCurrency.isEmpty()
                          ? purchaseInformation.currency
                          : purchaseInformation.vatCurrency;
    double vatCurrencyRate = 1.0;
    if (vatCurrency != companyCurrency) {
        vatCurrencyRate = m_currencyRateManager->rate(
            vatCurrency, companyCurrency, purchaseInformation.date);
    }

    double totalVat = 0.0;

    // Sum all VAT amounts converted to the total currency for HT computation.
    // When vatCurrency differs from the invoice currency we convert via the
    // company currency (EUR): vatAmountInTotalCurrency = vatAmount * vatCurrencyRate / currencyRate
    for (const auto &country : purchaseInformation.country_vatRate_vat.keys()) {
        const auto &rateMap = purchaseInformation.country_vatRate_vat[country];
        for (const auto &vatAmount : rateMap.values()) {
            if (vatCurrency == purchaseInformation.currency || currencyRate == 0.0) {
                totalVat += vatAmount;
            } else {
                totalVat += vatAmount * vatCurrencyRate / currencyRate;
            }
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
            double vatAmount = itRate.value();

            // Fallback empty fields to company country for VAT lookup
            QString companyCountry = m_companyInfos->getCompanyCountryCode();
            QString countryCode = purchaseInformation.countryCodeTo.isEmpty() ? companyCountry : purchaseInformation.countryCodeTo;

            // When vatCurrency matches the invoice currency the rate stored in the map
            // key is already correct (explicit or computed from same-currency amounts).
            // When they differ the stored rate was computed by dividing amounts in
            // different currencies and may be wrong; recompute it here using the CRM.
            // Rule: if one side is already in companyCurrency keep it and convert the
            // other; otherwise convert vatAmount to invoice currency via companyCurrency.
            double vatRate;
            // untaxedNormInCompany is totalHT expressed in companyCurrency — used below
            // to back-calculate the true vatCurrencyRate from the matched VAT rate.
            double untaxedNormInCompany = totalHT * currencyRate;
            if (vatCurrency == purchaseInformation.currency) {
                vatRate = itRate.key().toDouble();
            } else {
                double vatAmountNorm, untaxedNorm;
                if (vatCurrency == companyCurrency) {
                    vatAmountNorm = vatAmount;
                    untaxedNorm   = totalHT * currencyRate;
                } else if (purchaseInformation.currency == companyCurrency) {
                    vatAmountNorm     = vatAmount * vatCurrencyRate;
                    untaxedNorm       = totalHT;
                    untaxedNormInCompany = totalHT; // invoice == company
                } else {
                    vatAmountNorm = (currencyRate > 0.0) ? vatAmount * vatCurrencyRate / currencyRate : vatAmount;
                    untaxedNorm   = totalHT;
                    // untaxedNormInCompany = totalHT * currencyRate (default above is correct)
                }
                vatRate = (untaxedNorm > 0.001)
                          ? qRound(vatAmountNorm / untaxedNorm * 10000.0) / 10000.0
                          : itRate.key().toDouble();
            }

            QString vatDebit6;
            QString vatCredit4;
            double effectiveVatCurrencyRate = vatCurrencyRate;

            // Check for dual-amount token: company-currency VAT is available for this entry.
            // When present, the exact exchange rate is vatAmountCompany / vatAmountSource,
            // and the VAT rate key from the map is already accurate (computed from
            // same-currency amounts). No Closest lookup needed.
            const auto &vatCompanyMap = purchaseInformation.country_vatRate_vatCompany;
            const bool hasDualAmount = vatCompanyMap.contains(country)
                                       && vatCompanyMap[country].contains(itRate.key());
            if (hasDualAmount) {
                // Dual-amount token: conversion rate is exact (company / source).
                // Account lookup still uses Closest because small amounts round the
                // rate key imprecisely (e.g. 0.15 / 0.72 = 20.8% instead of 20%).
                vatRate = itRate.key().toDouble();
                const double vatAmountCompany = vatCompanyMap[country][itRate.key()];
                const double toleranceDual    = (vatAmountCompany < 2.0) ? 0.99 : 0.4;
                const auto debit6Result  = m_purchaseBookAccounts->getAccountsDebit6Closest(purchaseInformation.vatCountry, vatRate, toleranceDual);
                const auto credit4Result = m_purchaseBookAccounts->getAccountsCredit4Closest(purchaseInformation.vatCountry, vatRate, toleranceDual);
                vatDebit6  = debit6Result.account;
                vatCredit4 = credit4Result.account;
                if (vatAmount > 1e-9) {
                    effectiveVatCurrencyRate = vatAmountCompany / vatAmount;
                }
            } else if (vatCurrency != purchaseInformation.currency) {
                // Currencies differ but no dual amounts: use Closest lookup (legacy files).
                // Tolerance: 0.4% normally; widen to 0.99% for small VAT amounts (< 2
                // company-currency units) where rounding causes proportionally larger drift.
                const double vatAmountInCompany = (vatCurrency == companyCurrency)
                                                  ? vatAmount
                                                  : vatAmount * vatCurrencyRate;
                const double tolerance = (vatAmountInCompany < 2.0) ? 0.99 : 0.4;
                // Back-calculate the implied conversion rate from the matched stored VAT rate.
                const auto debit6Result  = m_purchaseBookAccounts->getAccountsDebit6Closest(purchaseInformation.vatCountry, vatRate, tolerance);
                const auto credit4Result = m_purchaseBookAccounts->getAccountsCredit4Closest(purchaseInformation.vatCountry, vatRate, tolerance);
                vatDebit6  = debit6Result.account;
                vatCredit4 = credit4Result.account;
                if (vatCurrency != companyCurrency && vatAmount > 1e-9) {
                    effectiveVatCurrencyRate = debit6Result.matchedRate * untaxedNormInCompany / vatAmount;
                }
            } else {
                vatDebit6  = m_purchaseBookAccounts->getAccountsDebit6(purchaseInformation.vatCountry, vatRate);
                vatCredit4 = m_purchaseBookAccounts->getAccountsCredit4(purchaseInformation.vatCountry, vatRate);
            }
            
            // Normal purchase VAT — use vatCurrency (may differ from total currency).
            // effectiveVatCurrencyRate is derived from the matched stored VAT rate when
            // currencies differ, giving a more precise conversion than CurrencyRateManager.
            JournalEntry::EntryLine vatLine;
            vatLine.title = commonTitle;
            vatLine.account = vatDebit6;
            vatLine.currency_amount[vatCurrency] = vatAmount;

            if (isRefund) {
                entry->addCreditRight(vatLine, vatCurrency, effectiveVatCurrencyRate);
            } else {
                entry->addDebitLeft(vatLine, vatCurrency, effectiveVatCurrencyRate);
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

QSharedPointer<JournalEntry> JournalEntryFactory::createEntry(
    const AmzPaymentInfo &paymentInfo) const
{
    if (!m_amzPaymentSettings)
        return nullptr;

    QString companyCurrency = m_companyInfos->getCurrency();
    QDate date = paymentInfo.dateTo;

    auto entry = QSharedPointer<JournalEntry>::create(date, companyCurrency);

    QString debitAccount  = m_amzPaymentSettings->getAccountDebit();
    QString amazonAccount = m_amzPaymentSettings->getAmazonAccount();

    // Label: "Paiement amazon.{countryCode} {paid} {paidCurrency}"
    // When paidCurrency != companyCurrency, JournalEntry auto-appends the
    // conversion rate "(Conv: {amount} {currency} @ {rate})" to each
    // converted line, satisfying the requirement of showing the rate.
    QString commonTitle = QString("Paiement amazon.%1 %2 %3")
                          .arg(paymentInfo.countryCode,
                               QString::number(paymentInfo.paid, 'f', 2),
                               paymentInfo.paidCurrency);

    auto getRate = [&](const QString &currency) -> double {
        if (currency == companyCurrency || currency.isEmpty())
            return 1.0;
        return m_currencyRateManager->rate(currency, companyCurrency, date);
    };

    auto makeDebit = [&](const QString &account, const QString &currency, double amount) {
        JournalEntry::EntryLine line;
        line.title   = commonTitle;
        line.account = account;
        line.currency_amount[currency] = amount;
        entry->addDebitLeft(line, currency, getRate(currency));
    };

    auto makeCredit = [&](const QString &account, const QString &currency, double amount) {
        JournalEntry::EntryLine line;
        line.title   = commonTitle;
        line.account = account;
        line.currency_amount[currency] = amount;
        entry->addCreditRight(line, currency, getRate(currency));
    };

    // Balance lines (both must be present together)
    if (paymentInfo.hasBalanceStart && paymentInfo.hasBalanceEnd) {
        makeDebit (debitAccount, paymentInfo.balanceStartCurrency, paymentInfo.balanceStart);
        makeCredit(debitAccount, paymentInfo.balanceEndCurrency,   paymentInfo.balanceEnd);
    }

    // Expenses deducted by Amazon → debit
    if (paymentInfo.hasExpenses && paymentInfo.expenses > 0.0)
        makeDebit(debitAccount, paymentInfo.expensesCurrency, paymentInfo.expenses);

    // Refunded expenses → credit
    if (paymentInfo.hasRefundedExpenses && paymentInfo.refundedExpenses > 0.0)
        makeCredit(debitAccount, paymentInfo.refundedExpensesCurrency, paymentInfo.refundedExpenses);

    // Actual payment received from Amazon → debit Amazon account
    if (paymentInfo.paid > 0.0)
        makeDebit(amazonAccount, paymentInfo.paidCurrency, paymentInfo.paid);

    return entry;
}

QList<JournalEntryFactory::GroupedShipmentData> JournalEntryFactory::computeGrouping(
    ActivitySource *source,
    const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
    const QDate &entryDate,
    const QHash<QString, QString> &orderId_store)
{
    if (shipmentAndRefunds.isEmpty())
        return {};

    VatResolver vatResolver(QDir(), nullptr, false);

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

    QMap<VatKey, GroupedShipmentData> dataByKey;

    for (auto it = shipmentAndRefunds.cbegin(); it != shipmentAndRefunds.cend(); ++it) {
        const QDate date = it.key().date();
        const auto &shipment = it.value();

        for (const Activity &activity : shipment->getActivities()) {
            VatKey key;
            key.scheme      = activity.getTaxScheme();
            key.countryFrom = activity.getCountryCodeFrom();
            key.countryTo   = activity.getCountryCodeTo();
            key.vatRate     = activity.getVatRate() * 100.0;

            // Snap to theoretical rate when within 1 percentage point
            const double theoreticalRate = vatResolver.getRate(
                entryDate,
                activity.getCountryCodeTo(),
                activity.getSaleType(),
                QString{},
                activity.getVatTerritoryTo()
            ) * 100.0;
            if (theoreticalRate > 0 && qAbs(key.vatRate - theoreticalRate) < 1.0)
                key.vatRate = theoreticalRate;

            key.currency = activity.getCurrency();

            if (!dataByKey.contains(key)) {
                GroupedShipmentData g;
                g.taxScheme    = key.scheme;
                g.countryFrom  = key.countryFrom;
                g.countryTo    = key.countryTo;
                g.vatRatePct   = key.vatRate;
                g.currency     = key.currency;
                g.sampleEventId = activity.getEventId();
                dataByKey[key] = g;
            }

            GroupedShipmentData &g = dataByKey[key];
            g.totalRevenue += activity.getAmountUntaxed();
            g.totalVat     += activity.getAmountTaxes();

            const QString orderId = activity.getEventId();
            ShipmentReportInfo info;
            info.store            = orderId_store.value(orderId,
                                        source ? source->subchannel : QString{});
            info.date             = date;
            info.orderId          = orderId;
            info.shipmentRefundId = activity.getActivityId();
            info.isRefund         = activity.getAmountTaxed() < 0.0;
            info.untaxedAmount    = activity.getAmountUntaxed();
            info.taxes            = activity.getAmountTaxes();
            info.taxedAmount      = activity.getAmountTaxed();
            info.currency         = activity.getCurrency();
            info.origTaxedAmount  = activity.getAmountTaxed();
            info.origCurrency     = activity.getCurrency();
            info.vatRatePct       = key.vatRate;
            info.taxScheme        = key.scheme;
            info.countryFrom      = key.countryFrom;
            info.countryTo        = key.countryTo;
            info.isCompany        = activity.getIsCompany();
            g.shipments.append(info);
        }
    }

    return dataByKey.values();
}

QCoro::Task<QList<QSharedPointer<JournalEntry>>> JournalEntryFactory::createEntryGrouped(
    ActivitySource *source,
    const QMultiMap<QDateTime, QSharedPointer<Shipment>> &shipmentAndRefunds,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) const
{
    if (shipmentAndRefunds.isEmpty()) {
        co_return {};
    }

    QString companyCurrency = m_companyInfos->getCurrency();
    QString companyCountry = m_companyInfos->getCompanyCountryCode();
    QDate firstDate = shipmentAndRefunds.firstKey().date();
    QDate entryDate = QDate(firstDate.year(), firstDate.month(), firstDate.daysInMonth());

    // Get journal code for this activity source
    QString journalCode = m_journalTable->getJournal(source);

    // Compute per-VatKey groupings (reusable for reporting)
    const QList<GroupedShipmentData> vatGroups = computeGrouping(source, shipmentAndRefunds, entryDate);

    // Base title: "Vente <Channel> <Subchannel> - <JournalCode>"
    QString baseTitle = QString("Vente %1 %2 - %3")
                        .arg(source->channel, source->subchannel, journalCode);

    // Short label for each TaxScheme used in per-entry titles.
    auto schemeShort = [](TaxScheme s) -> QString {
        switch (s) {
        case TaxScheme::DomesticVat:              return "DOM";
        case TaxScheme::EuOssUnion:               return "OSS";
        case TaxScheme::EuOssNonUnion:            return "OSS-NU";
        case TaxScheme::EuIoss:                   return "IOSS";
        case TaxScheme::ImportVat:                return "IMP";
        case TaxScheme::ReverseChargeImport:      return "RCI";
        case TaxScheme::ReverseChargeDomestic:    return "RCD";
        case TaxScheme::MarketplaceDeemedSupplier:return "MDS";
        case TaxScheme::Exempt:                   return "EXP";
        case TaxScheme::OutOfScope:               return "HRS";
        default:                                  return "?";
        }
    };

    // Groups keep the 3 line types (revenue, VAT, customer) adjacent in the entry.
    // Entries that share the same (saleAccount, vatAccount, custAccount, currency)
    // are merged into one group so the journal stays compact.
    struct JournalGroup {
        QString saleAccount;
        QString vatAccount;
        QString custAccount;
        QString currency;
        double  revenue = 0.0;
        double  vat     = 0.0;
        // (int(TaxScheme), round(vatRate×10)) → unique destination countries
        QMap<QPair<int,int>, QSet<QString>> vatDetails;
    };
    QList<JournalGroup> groups;
    QMap<QString, int>  groupKeyToIndex;

    // Resolve accounts and aggregate into journal groups
    for (const GroupedShipmentData &vg : vatGroups) {
        VatCountries vc = m_saleBookAccounts->resolveVatCountries(
            vg.taxScheme,
            companyCountry,
            vg.countryFrom,
            vg.countryTo
        );

        BooksAccountsSalesTable::Accounts accounts;
        try {
            accounts = co_await m_saleBookAccounts->getAccounts(vc, vg.vatRatePct, callbackAddIfMissing);
        } catch (const ExceptionWithTitleText &e) {
            QString newText = e.errorText() + QString("\n(Sample order ID for this VAT config: %1)").arg(vg.sampleEventId);
            ExceptionWithTitleText newEx(e.errorTitle(), newText);
            newEx.raise();
        }

        // Customer account
        QString custAcc = accounts.customerAccount;
        while (custAcc.isEmpty() && callbackAddIfMissing) {
            bool shouldRetry = co_await callbackAddIfMissing(
                QObject::tr("Missing Customer Account"),
                QObject::tr("No customer account found for sales entry (VAT scheme: %1, Rate: %2)")
                    .arg(taxSchemeToString(vg.taxScheme)).arg(vg.vatRatePct));
            if (!shouldRetry)
                break;
            accounts = co_await m_saleBookAccounts->getAccounts(vc, vg.vatRatePct, callbackAddIfMissing);
            custAcc = accounts.customerAccount;
        }
        if (custAcc.isEmpty()) {
            ExceptionWithTitleText exception(QObject::tr("Missing Customer Account"),
                QObject::tr("No customer account found for sales entry (VAT scheme: %1, Rate: %2)")
                .arg(taxSchemeToString(vg.taxScheme)).arg(vg.vatRatePct));
            exception.raise();
        }

        // Merge into existing group or create a new one.
        // For DomesticVat, countryTo is part of the fiscal identity.
        const QString countryQualifier = (vg.taxScheme == TaxScheme::DomesticVat)
                                         ? "|" + vg.countryTo : QString();
        const QString groupKey = accounts.saleAccount + "|" + accounts.vatAccount
                               + "|" + custAcc + "|" + vg.currency + countryQualifier;
        if (!groupKeyToIndex.contains(groupKey)) {
            groupKeyToIndex[groupKey] = groups.size();
            groups.push_back({accounts.saleAccount, accounts.vatAccount,
                              custAcc, vg.currency, 0.0, 0.0, {}});
        }
        JournalGroup &g = groups[groupKeyToIndex[groupKey]];
        g.revenue += vg.totalRevenue;
        g.vat     += vg.totalVat;
        g.vatDetails[{static_cast<int>(vg.taxScheme), qRound(vg.vatRatePct * 10)}]
            .insert(vg.countryTo);
    }

    // Create one JournalEntry per group, each with exactly 3 lines
    QList<QSharedPointer<JournalEntry>> result;
    for (const JournalGroup &g : std::as_const(groups)) {
        auto entry = QSharedPointer<JournalEntry>::create(entryDate, companyCurrency);

        QStringList vatParts;
        for (auto dit = g.vatDetails.constBegin(); dit != g.vatDetails.constEnd(); ++dit) {
            const TaxScheme s = static_cast<TaxScheme>(dit.key().first);
            const double r    = dit.key().second / 10.0;
            QStringList countries(dit.value().constBegin(), dit.value().constEnd());
            countries.sort();
            QString part = schemeShort(s) + " " + countries.join("/");
            if (r > 0.01)
                part += " " + QString::number(r, 'f', 0) + "%";
            vatParts.append(part);
        }
        const QString groupTitle = baseTitle + " | " + vatParts.join(", ");

        auto addLine = [&](const QString &account, double amount, bool isCredit) {
            if (qAbs(amount) <= 0.01) return;
            double cr = 1.0;
            if (g.currency != companyCurrency)
                cr = m_currencyRateManager->rate(g.currency, companyCurrency, entryDate);
            JournalEntry::EntryLine line;
            line.title   = groupTitle;
            line.account = account;
            line.currency_amount[g.currency] = amount;
            if (isCredit) entry->addCreditRight(line, g.currency, cr);
            else          entry->addDebitLeft  (line, g.currency, cr);
        };

        addLine(g.saleAccount, g.revenue,         true);  // Credit Revenue
        addLine(g.vatAccount,  g.vat,              true);  // Credit VAT
        addLine(g.custAccount, g.revenue + g.vat, false);  // Debit Customer

        result.append(entry);
    }

    co_return result;
}

QCoro::Task<QSharedPointer<JournalEntry>> JournalEntryFactory::createEntry(
    QSharedPointer<Shipment> shipmentOrRefund
    , const QString &customerAccount
    , std::function<QCoro::Task<bool> (const QString &, const QString &)> callbackAddIfMissing) const
{
    if (!shipmentOrRefund) {
        co_return nullptr;
    }
    
    const QList<Activity> &activities = shipmentOrRefund->getActivities();
    if (activities.isEmpty()) {
        co_return nullptr;
    }
    
    const QString &companyCurrency = m_companyInfos->getCurrency();
    const QString &companyCountry = m_companyInfos->getCompanyCountryCode();
    const QDate &entryDate = activities.first().getDateTime().date();
    const QString &shipmentId = shipmentOrRefund->getId();
    
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
        
        
        QString custAcc;
        if (!customerAccount.isEmpty()) {
            custAcc = customerAccount;
        } else {
            custAcc = accounts.customerAccount;
            while (custAcc.isEmpty() && callbackAddIfMissing) {
                bool shouldRetry = co_await callbackAddIfMissing(
                    QObject::tr("Missing Customer Account"),
                    QObject::tr("No customer account found for sales entry (VAT scheme: %1, Rate: %2)")
                        .arg(taxSchemeToString(activity.getTaxScheme())).arg(vatRate));
                if (!shouldRetry)
                    break;
                accounts = co_await m_saleBookAccounts->getAccounts(vc, vatRate, callbackAddIfMissing);
                custAcc = accounts.customerAccount;
            }
            if (custAcc.isEmpty()) {
                ExceptionWithTitleText exception(
                    QObject::tr("Missing Customer Account"),
                    QObject::tr("No customer account found for sales entry (VAT scheme: %1, Rate: %2)")
                        .arg(taxSchemeToString(activity.getTaxScheme())).arg(vatRate));
                exception.raise();
            }
        }

        revenueByAccount[accounts.saleAccount][currency] += amountUntaxed;
        vatByAccount[accounts.vatAccount][currency] += amountTaxes;

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
    , int row) const
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

QSharedPointer<JournalEntry> JournalEntryFactory::createEntry(
    const InventoryMoveTree *inventoryMoveTree,
    const QString &countryCodeCompany) const
{
    if (!inventoryMoveTree || !m_selfVatBookAccounts)
        return nullptr;

    const QString companyCurrency = m_companyInfos->getCurrency();
    const QDate date = QDate::currentDate();
    auto entry = QSharedPointer<JournalEntry>::create(date, companyCurrency);

    const int parentCount = inventoryMoveTree->rowCount();
    for (int row = 0; row < parentCount; ++row) {
        const QString countryFrom = inventoryMoveTree->data(
            inventoryMoveTree->index(row, InventoryMoveTree::COL_FROM)).toString();
        const QString countryTo = inventoryMoveTree->data(
            inventoryMoveTree->index(row, InventoryMoveTree::COL_TO)).toString();
        if (countryCodeCompany == countryFrom || countryCodeCompany == countryTo)
        {

            const QString &acctSale7 = m_selfVatBookAccounts->getAccountSale7(countryFrom, countryTo);
            const QString &acctStock4    = m_selfVatBookAccounts->getAccountStock4(countryFrom, countryTo);
            const QString &acctPurchase7 = m_selfVatBookAccounts->getAccountPurchase7(countryFrom, countryTo);
            const QString &acctVatDue    = m_selfVatBookAccounts->getAccountVatDue(countryFrom, countryTo);
            const QString &acctVatDed    = m_selfVatBookAccounts->getAccountVatDeductible(countryFrom, countryTo);

            // If ALL accounts are empty, it's likely the route is not configured (e.g. not an EU->Company move), so skip.
            // If SOME accounts are empty, it's a misconfiguration, so throw.
            if (acctSale7.isEmpty() && acctStock4.isEmpty() && acctPurchase7.isEmpty() &&
                acctVatDue.isEmpty() && acctVatDed.isEmpty()) {
                continue;
            }

            if (acctSale7.isEmpty() || acctStock4.isEmpty() || acctPurchase7.isEmpty() ||
                acctVatDue.isEmpty() || acctVatDed.isEmpty()) {
                ExceptionWithTitleText exception(
                    QObject::tr("Missing Self-VAT Accounts"),
                    QObject::tr("Missing account(s) for Intracom stock move (%1 -> %2). Please check the Self-VAT settings.")
                        .arg(countryFrom, countryTo)
                    );
                exception.raise();
            }

            const double totalHT = inventoryMoveTree->data(
                                                        inventoryMoveTree->index(row, InventoryMoveTree::COL_TOTAL_PRICE)).toDouble();
            if (totalHT <= 0.0) {
                continue;
            }

            // Standard 20 % self-assessment VAT on intracom stock acquisition.
            const double vatAmount = qRound(totalHT * 0.20 * 100.0) / 100.0;
            const double totalTTC  = totalHT + vatAmount;

            QString currency = inventoryMoveTree->data(
                                                    inventoryMoveTree->index(row, InventoryMoveTree::COL_CURRENCY)).toString();
            if (currency.isEmpty()) {
                currency = companyCurrency;
            }

            double currencyRate = 1.0;
            if (currency != companyCurrency) {
                currencyRate = m_currencyRateManager->rate(currency, companyCurrency, date);
            }

            const QString route = QString("%1 > %2").arg(countryFrom, countryTo);
            const QString titleSaleRoute   = QObject::tr("Stock déporté depuis l'UE (Vente intracom) – %1").arg(route);
            const QString titleSale        = QObject::tr("Stock déporté depuis l'UE (Vente intracom)");
            const QString titleAcquis      = QObject::tr("Stock déporté depuis l'UE (Acquisition intracom)");
            const QString titleAcquisRoute = QObject::tr("Stock déporté depuis l'UE (Acquisition intracom) – %1").arg(route);

            auto makeLine = [&](const QString &title, const QString &account, double amount) {
                JournalEntry::EntryLine line;
                line.title   = title;
                line.account = account;
                line.currency_amount[currency] = amount;
                return line;
            };

            // 1. CREDIT Sale7    — totalTTC — Vente intracom – route
            entry->addCreditRight(makeLine(titleSaleRoute,   acctSale7,    totalTTC), currency, currencyRate);
            // 2. DEBIT  Stock4   — totalHT  — Vente intracom
            entry->addDebitLeft  (makeLine(titleSale,        acctStock4,   totalHT),  currency, currencyRate);
            // 3. CREDIT Stock4   — totalHT  — Acquisition intracom
            entry->addCreditRight(makeLine(titleAcquis,      acctStock4,   totalHT),  currency, currencyRate);
            // 4. CREDIT VatDue   — vatAmount — Acquisition intracom
            entry->addCreditRight(makeLine(titleAcquis,      acctVatDue,   vatAmount), currency, currencyRate);
            // 5. DEBIT  VatDed   — vatAmount — Acquisition intracom
            entry->addDebitLeft  (makeLine(titleAcquis,      acctVatDed,   vatAmount), currency, currencyRate);
            // 6. DEBIT  Purchase7 — totalTTC — Acquisition intracom – route
            entry->addDebitLeft  (makeLine(titleAcquisRoute, acctPurchase7, totalTTC), currency, currencyRate);
        }
    }

    if (entry->getDebits().isEmpty() && entry->getCredits().isEmpty()) {
        return nullptr;
    }

    return entry;
}
