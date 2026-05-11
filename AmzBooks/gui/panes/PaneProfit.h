#ifndef PANEPROFIT_H
#define PANEPROFIT_H

#include <QWidget>

namespace Ui {
class PaneProfit;
}

class ProductFilterTable;
class ProfitTree;
class CompanyInfosTable;
class CurrencyRateManager;

class PaneProfit : public QWidget
{
    Q_OBJECT

public:
    explicit PaneProfit(QWidget *parent = nullptr);
    ~PaneProfit();

public slots:
    void browseEconomicFolder();
    void computeProfit();
    void editFilters();
    void filter();
    void filterReset();
    void filterByAsin();
    void filterByAsinReset();

private:
    Ui::PaneProfit *ui;
    static const QString SETTINGS_KEY_ECONOMIC_FOLDER;
    ProductFilterTable *m_productFilterTable;
    ProfitTree *m_profitTree;
    CompanyInfosTable *m_companyInfos;
    CurrencyRateManager *m_currRateManager;
    void _connectSlots();
    void _setFilterButtonsEnabled(bool enable);
    
private slots:
    void saveSettings();
};

#endif // PANEPROFIT_H
