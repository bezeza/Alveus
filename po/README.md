# Alveus translations

English is the source language: the original strings passed to KDE's `i18n()` and `i18np()` functions are English, so an `en.po` file would only duplicate the source text. KDE automatically displays these strings when the system locale is English or when no translation is available.

- `uk.po` — Ukrainian
- `ru.po` — Russian

CMake compiles every `.po` file in this directory and installs the resulting catalog for the Plasma applet. Run `python3 tests/check_translations.py` and `msgfmt --check -o /dev/null po/ru.po` to validate translation parity, Qt placeholders, markup, and PO syntax.
