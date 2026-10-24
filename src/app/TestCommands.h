#pragma once

#include <QString>

class AppController;
class QObject;

// The commands of the test scripts (--steps) that need no window, shared by
// both frontends' test drivers:
//   state | open <browseId> | sh <command>
//   call <App property or .> <method> [argument: number, text or key=value,key=value]
//   set <App property> <property> <value>
// False for a command it does not know (the frontend's own: click, type, ...).
bool runTestCommand(AppController *app, const QString &line, QObject *context);
