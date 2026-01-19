#ifndef AMOUNT_H
#define AMOUNT_H

class Amount final
{
public:
    Amount(double amountTaxed, double taxes);

    double getAmountUntaxed() const noexcept;
    double getAmountTaxed() const noexcept;
    double getTaxes() const noexcept;

private:
    double m_amountTaxed;
    double m_taxes;
};

#endif // AMOUNT_H
