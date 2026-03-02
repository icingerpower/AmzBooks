#ifndef REPORTGENERATOR_H
#define REPORTGENERATOR_H

#include <QString>
#include <QStringList>

class ReportGenerator
{
public:
    ReportGenerator();
    virtual ~ReportGenerator() = default;

    void addTitle(const QString &title);
    void addSubtitle(const QString &subtitle);
    void startTable(const QStringList &headerLabels);
    void addRow(const QStringList &rowElements);
    void endTable();

    void setLandscape(bool landscape);

    QString getHtml() const;
    void save(const QString &pdfFilePath) const;

private:
    QString m_html;
    bool m_inTable;
    bool m_landscape;
    int m_currentTableColumnsCount;
};

#endif // REPORTGENERATOR_H
