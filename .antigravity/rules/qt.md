# Qt Best Practices

- **QString::arg**: Always use the multi-argument overload `QString("%1 %2").arg(a, b)` instead of chained calls `QString("%1 %2").arg(a).arg(b)` to avoid multiple allocations and potential issues.

## Exception Handling
- **Inheritance**: Exceptions should ideally inherit from `QException`.
- **Cross-Thread Safety**: They must implement `raise()` and `clone()` methods to be compatible with Qt's cross-thread exception transport.
- **Detail**: Always provide a title and detailed error text.

## QAbstractTableModel Implementations
- **Robust Loading**:
    - Loading logic **MUST** be robust to schema changes. It should handle added, deleted, or re-ordered columns gracefully.
    - **Header Mapping**: Always read the header line to map column names to indices dynamically. Do not rely on fixed column indices.
- **Technical IDs**: Use hidden, stable technical IDs ("Hidden ID") for logic, which are stored in the file but not displayed in `columnCount`/`data`.
- **Localization**:
    - Use `tr()` for all user-facing strings (headers, parameter names).
    - **Exception**: Do **NOT** use `tr()` for hidden internal IDs or technical column headers in the saved file.
