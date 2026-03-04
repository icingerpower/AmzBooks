#ifndef REPORTGENERATOR_H
#define REPORTGENERATOR_H

#include <QString>
#include <QStringList>
#include <QPageSize>

class ReportGenerator
{
public:
    ReportGenerator();
    virtual ~ReportGenerator() = default;

    void addTitle(const QString &title);
    void addSubtitle(const QString &subtitle);
    void startTable(const QStringList &headerLabels);
    void addRow(const QStringList &rowElements);
    void addRowTotal(const QStringList &rowElements); // bold total row with distinct background
    void endTable();

    void setLandscape(bool landscape);
    void setPageSize(QPageSize::PageSizeId pageSize);
    void setFontScale(double scale); // e.g. 1.5 for 50% bigger

    QString getHtml() const;
    void save(const QString &pdfFilePath) const;

private:
    QString m_html;
    bool m_inTable;
    bool m_landscape;
    int m_currentTableColumnsCount;
    QPageSize::PageSizeId m_pageSize;
    double m_fontScale;
};

#endif // REPORTGENERATOR_H
