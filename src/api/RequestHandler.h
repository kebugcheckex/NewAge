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

// Turns one JSON request into one JSON result. Every call reloads the data;
// a batch reloads it once for all its requests.
// `request` is an op object: `{"op": "info"}`, `{"op": "schema", "kind": "unit"}`,
// `{"op": "lookup", "table": "unit", "text": "archer", "civ": 1}` or
// `{"op": "list", "kind": "tech", "civ": 1, "ownerCiv": 9, "all": true,
// "offset": 0, "limit": 50}` or `{"op": "get", "kind": "unit", "ids": [4, 5],
// "civ": 1, "fields": ["hit_points", "cost*"], "compact": true}` or
// `{"op": "batch", "requests": [...]}`, whose requests are read ops. A batch
// result is `{"results": [...], "failed": N}`: each request's body in its
// place, errors included, and how many failed. It exits 0 unless the batch
// itself is malformed or the data can't be opened.
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
