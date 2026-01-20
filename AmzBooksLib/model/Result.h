#ifndef RESULT_H
#define RESULT_H

#include <QString>
#include <QList>

struct ValidationError {
    QString field;
    QString message;
};

template<class T>
struct Result {
  std::optional<T> value;
  QList<ValidationError> errors;
  bool ok() const noexcept;
};

#endif // RESULT_H
