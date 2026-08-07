#include "cmainwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	QApplication app{ argc, argv };
	QApplication::setApplicationName(QStringLiteral("Resize comparison"));

	CMainWindow window;
	window.show();

	return QApplication::exec();
}
