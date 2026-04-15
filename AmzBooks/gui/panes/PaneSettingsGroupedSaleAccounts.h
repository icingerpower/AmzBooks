#ifndef PANESETTINGSGROUPEDSALEACCOUNTS_H
#define PANESETTINGSGROUPEDSALEACCOUNTS_H

#include <QWidget>

namespace Ui {
class PaneSettingsGroupedSaleAccounts;
}

class PaneSettingsGroupedSaleAccounts : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsGroupedSaleAccounts(QWidget *parent = nullptr);
    ~PaneSettingsGroupedSaleAccounts();

private slots:
    void addSaleControlEntry();
    void removeSaleControlEntry();

private:
    Ui::PaneSettingsGroupedSaleAccounts *ui;
};

#endif // PANESETTINGSGROUPEDSALEACCOUNTS_H
