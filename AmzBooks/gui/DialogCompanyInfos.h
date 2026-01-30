#ifndef DIALOGCOMPANYINFOS_H
#define DIALOGCOMPANYINFOS_H

#include <QDialog>

namespace Ui {
class DialogCompanyInfos;
}

class DialogCompanyInfos : public QDialog
{
    Q_OBJECT

public:
    explicit DialogCompanyInfos(QWidget *parent = nullptr);
    ~DialogCompanyInfos();

public slots:
    void addAddress();
    void removeAddress();
    void accept() override;

private:
    Ui::DialogCompanyInfos *ui;
    void _connectSlots();
};

#endif // DIALOGCOMPANYINFOS_H
