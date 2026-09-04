#
# Audacity: A Digital Audio Editor
#

set(MODULE_TEST mcp_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/audacitycommandsregister_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/audacitycommandscontroller_tests.cpp

    ${CMAKE_CURRENT_LIST_DIR}/mocks/commanddispatchermock.h
)

set(MODULE_TEST_LINK
    mcp

    muse::ui
)

set(MODULE_TEST_INCLUDE
    ${MUSE_FRAMEWORK_PATH}/framework/actions
    ${MUSE_FRAMEWORK_PATH}/framework/rcommand
    ${MUSE_FRAMEWORK_PATH}/framework/global
)

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

include(SetupGTest)
