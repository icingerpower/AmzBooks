#ifndef DIALOGADDFBACENTER_H
#define DIALOGADDFBACENTER_H

#include <QDialog>

namespace Ui {
class DialogAddFbaCenter;
}

class DialogAddFbaCenter : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddFbaCenter(QWidget *parent = nullptr);
    ~DialogAddFbaCenter();

    QString getCenterId() const;
    QString getCountryCode() const;
    QString getPostalCode() const;
    QString getCity() const;

private:
    Ui::DialogAddFbaCenter *ui;
    void _setupCountries();
};

#endif // DIALOGADDFBACENTER_H
