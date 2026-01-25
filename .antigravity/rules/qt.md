# Qt Best Practices

- **QString::arg**: Always use the multi-argument overload `QString("%1 %2").arg(a, b)` instead of chained calls `QString("%1 %2").arg(a).arg(b)` to avoid multiple allocations and potential issues.
