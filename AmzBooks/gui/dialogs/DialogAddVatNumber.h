#ifndef DIALOGADDVATNUMBER_H
#define DIALOGADDVATNUMBER_H

#include <QDialog>

namespace Ui {
class DialogAddVatNumber;
}

class DialogAddVatNumber : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddVatNumber(QWidget *parent = nullptr);
    ~DialogAddVatNumber();

    QString getSelectedCountry() const;
    QString getVatNumber() const;

private slots:
    void onCountryChanged(int index);
    void validateInput();

private:
    Ui::DialogAddVatNumber *ui;
    void _populateCountries();
};

#endif // DIALOGADDVATNUMBER_H
