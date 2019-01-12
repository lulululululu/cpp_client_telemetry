set(CMAKE_PROJECT_NAME "aria-sdk")
set(CPACK_SYSTEM_NAME "${OS_RELEASE_ID}-${OS_RELEASE_VERSION_ID}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
set(CPACK_GENERATOR "RPM")

set(CPACK_RPM_PACKAGE_DESCRIPTION "Aria SDK for Linux")
set(CPACK_RPM_PACKAGE_DESCRIPTION_SUMMARY "Aria SDK for events ingestion from Linux hosts")
set(CPACK_RPM_PACKAGE_CONTACT "ariaesdks@microsoft.com")

set(MAJOR_VERSION "3")
set(MINOR_VERSION "1")
string(TIMESTAMP DAYNUMBER "%j")
set(PATCH_VERSION "${DAYNUMBER}")

set(CPACK_PACKAGE_VERSION ${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION})
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "aria_sdk")
set(CPACK_PACKAGE_RELEASE "0")
set(CPACK_PACKAGE_VENDOR "Microsoft")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")
message("-- Package name: ${CPACK_RPM_PACKAGE_FILE_NAME}.rpm")

#configure_file("${CMAKE_CURRENT_SOURCE_DIR}/aria-sdk.spec.in" "${CMAKE_CURRENT_BINARY_DIR}/arka-sdk.spec" @ONLY IMMEDIATE)
#set(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_BINARY_DIR}/arka-sdk.spec")

include(CPack)
