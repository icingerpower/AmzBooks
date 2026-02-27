#include <iostream>
#include <QCoreApplication>
#include <QString>
#include <QDate>
#include "PurchaseAmzPaymentsManager.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);
    AmzPaymentInfo info;
    info.countryCode          = "com";
    info.dateFrom             = QDate(2026, 1, 7);
    info.dateTo               = QDate(2026, 1, 21);
    info.balanceStart         = 1311.19;
    info.balanceStartCurrency = "USD";
    info.balanceEnd           = 1135.55;
    info.balanceEndCurrency   = "USD";
    info.hasExpenses          = true;
    info.expenses             = 2627.38;
    info.expensesCurrency     = "USD";
    info.hasRefundedExpenses  = true;
    info.refundedExpenses     = 153.17;
    info.refundedExpensesCurrency = "USD";
    info.paid                 = 177.90;
    info.paidCurrency         = "USD";
    info.hasBalanceStart = true;
    info.hasBalanceEnd = true;

    QString encoded = PurchaseAmzPaymentsManager::encode(info);
    std::cout << encoded.toStdString() << std::endl;
    return 0;
}
