// SPDX-License-Identifier: Apache-2.0
#include "daemon.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wearoutd"));
    Daemon daemon;
    if (!daemon.start())
        return 1;
    return app.exec();
}
