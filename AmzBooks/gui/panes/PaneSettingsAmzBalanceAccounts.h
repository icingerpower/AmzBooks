#ifndef PANESETTINGSAMZBALANCEACCOUNTS_H
#define PANESETTINGSAMZBALANCEACCOUNTS_H

#include <QWidget>

namespace Ui {
class PaneSettingsAmzBalanceAccounts;
}

class PaneSettingsAmzBalanceAccounts : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsAmzBalanceAccounts(QWidget *parent = nullptr);
    ~PaneSettingsAmzBalanceAccounts();

private:
    Ui::PaneSettingsAmzBalanceAccounts *ui;
};

#endif // PANESETTINGSAMZBALANCEACCOUNTS_H
