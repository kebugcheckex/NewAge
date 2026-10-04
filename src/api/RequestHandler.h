#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "api/DataService.h"

namespace newage {

// Maps an error code to the CLI exit status (docs/cli.md, section 6.2).
// An empty code is success. A code that isn't in the table is usage.
int exitCodeFor(const QString &code);

// One command's JSON result. `body` is what the CLI prints on stdout.
// `warnings` are not part of `body`; the CLI prints each as a JSON line on
// stderr. A warning does not change `exitCode`.
struct HandlerResult
{
    QJsonObject body;
    QStringList warnings;
    int exitCode = 0;
};

// Turns one JSON request into one JSON result. Every call reloads the data.
// `request` is an op object (`{"op": "info"}` or `{"op": "schema", "kind": "unit"}`).
// Source options stay on `source`, not in the request, because a batch
// shares one open. Empty game, dataset, mod and mods-folder fields are filled
// from the environment before opening.
//
// Phase 1 reads start here. Unsupported ops are `usage` and do not open data.
class RequestHandler
{
public:
    HandlerResult handle(const DataSource &source, const QJsonObject &request);
};

} // namespace newage
