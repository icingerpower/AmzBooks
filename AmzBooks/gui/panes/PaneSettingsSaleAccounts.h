#ifndef PANESETTINGSSALEACCOUNTS_H
#define PANESETTINGSSALEACCOUNTS_H

#include <QWidget>

namespace Ui {
class PaneSettingsSaleAccounts;
}

class PaneSettingsSaleAccounts : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsSaleAccounts(QWidget *parent = nullptr);
    ~PaneSettingsSaleAccounts();

public slots:
    void addRate();
    void removeRate();

private:
    Ui::PaneSettingsSaleAccounts *ui;
    void _connectSlots();
};

#endif // PANESETTINGSSALEACCOUNTS_H
