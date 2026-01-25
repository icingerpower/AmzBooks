#ifndef RESULT_H
#define RESULT_H

#include <QString>
#include <QList>
#include <optional>

struct ValidationError {
    QString field;
    QString message;
};

template<class T>
struct Result {
  std::optional<T> value;
  QList<ValidationError> errors;
  bool ok() const noexcept {
      return value.has_value() && errors.isEmpty();
  }
};

#endif // RESULT_H
