#include "BookSaverFull.h"
#include "JournalEntry.h"
#include <QTextStream>
#include <QDate>
#include <QDebug>
#include "ExceptionWithTitleText.h"

DECLARE_BOOK_SAVER(BookSaverFull)

BookSaverFull::BookSaverFull()
{
}

void BookSaverFull::save(
        const QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> &journal_date_entries
        , const QDir &outDir)
{
    // Group by Year / Month
    // Structure: QHash<Year, QHash<Month, QHash<original_journal_id, QList<EntryLines>>>> ??
    // The Input is organized by JournalID -> Date -> JournalEntry
    
    // We need to iterate everything and regroup.
    
    struct CsvLine {
        QDate date;
        QString journal;
        QString account;
        double debit = 0.0;
        double credit = 0.0;
        QString label;
    };
    
    // Key: Year -> Month -> Journal -> List of lines
    QHash<int, QHash<int, QHash<QString, QList<CsvLine>>>> organizedData;

    for (auto itJournal = journal_date_entries.constBegin(); itJournal != journal_date_entries.constEnd(); ++itJournal) {
        QString journalId = itJournal.key();
        const auto &dateMap = itJournal.value();
        
        for (auto itDate = dateMap.constBegin(); itDate != dateMap.constEnd(); ++itDate) {
            QDate date = itDate.key();
            int year = date.year();
            int month = date.month();
            
            QSharedPointer<JournalEntry> entry = itDate.value();
            if (!entry) continue;
            
            const QString &targetCurrency = entry->getCurrency();

            auto amountInTargetCurrency = [&](const JournalEntry::EntryLine &line) -> double {
                if (line.currency_amount.contains(targetCurrency))
                    return line.currency_amount[targetCurrency];
                if (!line.currency_amount.isEmpty())
                    return line.currency_amount.constBegin().value();
                return 0.0;
            };

            // Debits — negative amount flips to credit side
            for (const auto &line : entry->getDebits()) {
                CsvLine csvLine;
                csvLine.date = date;
                csvLine.journal = journalId;
                csvLine.account = line.account;
                const double amount = amountInTargetCurrency(line);
                if (amount >= 0.0) {
                    csvLine.debit  = amount;
                    csvLine.credit = 0.0;
                } else {
                    csvLine.debit  = 0.0;
                    csvLine.credit = -amount;
                }
                csvLine.label = line.title;
                organizedData[year][month][journalId].append(csvLine);
            }

            // Credits — negative amount flips to debit side
            for (const auto &line : entry->getCredits()) {
                CsvLine csvLine;
                csvLine.date = date;
                csvLine.journal = journalId;
                csvLine.account = line.account;
                const double amount = amountInTargetCurrency(line);
                if (amount >= 0.0) {
                    csvLine.debit  = 0.0;
                    csvLine.credit = amount;
                } else {
                    csvLine.debit  = -amount;
                    csvLine.credit = 0.0;
                }
                csvLine.label = line.title;
                organizedData[year][month][journalId].append(csvLine);
            }
        }
    }
    
    // Helper to write CSV
    auto writeCsv = [](const QFileInfo &fileInfo, const QList<CsvLine> &lines, bool latin1) {
        QDir().mkpath(fileInfo.absolutePath());
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            ExceptionWithTitleText exception("File open failed", QString("Could not open file for writing: %1").arg(fileInfo.absoluteFilePath()));
            exception.raise();
        }
        
        QTextStream out(&file);
        if (latin1) {
             out.setEncoding(QStringConverter::Latin1);
        } else {
             out.setEncoding(QStringConverter::Utf8);
        }
        
        // Header
        // "date / journal / account / debit / credit / label"
        // Prompt says "; as column separator"
        // out << "date;journal;account;debit;credit;label\n";
        
        for (const auto &line : lines) {
            out << line.date.toString("dd/MM/yyyy") << ";"
                << line.journal << ";"
                << line.account << ";"
                << QString::number(line.debit, 'f', 2) << ";"
                << QString::number(line.credit, 'f', 2) << ";"
                << line.label << "\n";
        }
    };

    const bool saveLatin1 = QStringConverter::encodingForName("latin1").has_value();

    // Iterate and Save
    for (auto itYear = organizedData.constBegin(); itYear != organizedData.constEnd(); ++itYear) {
        int year = itYear.key();
        QString yearStr = QString::number(year);
        
        for (auto itMonth = itYear->constBegin(); itMonth != itYear->constEnd(); ++itMonth) {
            int month = itMonth.key();
            QString monthStr = QString("%1").arg(month, 2, 10, QChar('0'));
            
            QDir monthDir = outDir;
            if (!monthDir.mkpath(QString("%1/%2").arg(yearStr, monthStr))) {
                ExceptionWithTitleText exception("Directory creation failed", QString("Could not create directory for %1-%2").arg(yearStr, monthStr));
                exception.raise();
            }
            monthDir.cd(yearStr);
            monthDir.cd(monthStr);

            QList<CsvLine> allLines;
            
            // Individual Journals
            for (auto itJ = itMonth->constBegin(); itJ != itMonth->constEnd(); ++itJ) {
                QString journal = itJ.key();
                const QList<CsvLine> &lines = itJ.value();
                
                allLines.append(lines);
                
                // <year_dir>/<month_dir>/<journal_dir>/<journal_dir>_<year_dir>_<month_dir>.csv
                QString fileName = QString("%1_%2_%3.csv").arg(journal, yearStr, monthStr);

                writeCsv(QFileInfo(monthDir.filePath(journal + "/" + fileName)), lines, false);
                if (saveLatin1) {
                    QString fileNameLatin1 = QString("%1_%2_%3-latin1.csv").arg(journal, yearStr, monthStr);
                    writeCsv(QFileInfo(monthDir.filePath(journal + "/" + fileNameLatin1)), lines, true);
                }
            }
            
            // All aggregated
            // <year_dir>/<month_dir>/all/all_<year_dir>_<month_dir>.csv
            QString allFileName = QString("all_%1_%2.csv").arg(yearStr, monthStr);

            writeCsv(QFileInfo(monthDir.filePath("all/" + allFileName)), allLines, false);
            if (saveLatin1) {
                QString allFileNameLatin1 = QString("all_%1_%2-latin1.csv").arg(yearStr, monthStr);
                writeCsv(QFileInfo(monthDir.filePath("all/" + allFileNameLatin1)), allLines, true);
            }
        }
    }
}
