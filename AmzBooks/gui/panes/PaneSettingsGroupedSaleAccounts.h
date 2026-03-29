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

private:
    Ui::PaneSettingsGroupedSaleAccounts *ui;
};

#endif // PANESETTINGSGROUPEDSALEACCOUNTS_H
