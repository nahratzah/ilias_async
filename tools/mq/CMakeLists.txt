project (IliasMqDesigner)

find_package (Qt4 QtCore QtGui)
include (${QT_USE_FILE})

add_executable(ilias_mq
	mq.cc
	)
