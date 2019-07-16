cmake_minimum_required(VERSION 2.6)

PROJECT(udrone C)
INCLUDE(GNUInstallDirs)
ADD_DEFINITIONS(-Os -ggdb -Wall -Werror --std=gnu99 -Wmissing-declarations -pedantic)

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

SET(SOURCES udrone.c cmd_stdsys.c cmd_system.c cmd_ubus.c cmd_uci.c)
SET(LIBS json-c ubox blobmsg_json ubus uci)

ADD_EXECUTABLE(udrone ${SOURCES})
TARGET_LINK_LIBRARIES(udrone ${LIBS})
INSTALL(TARGETS udrone
	RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)
