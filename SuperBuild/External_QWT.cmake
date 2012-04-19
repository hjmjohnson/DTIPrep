ExternalProject_add(QWT
  SVN_REPOSITORY https://qwt.svn.sourceforge.net/svnroot/qwt/branches/qwt-6.0
  SOURCE_DIR QWT
  BINARY_DIR QWT-build
  PATCH_COMMAND ${CMAKE_COMMAND} -E
  copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.qwt
  <SOURCE_DIR>/CMakeLists.txt
  CMAKE_ARGS ${COMMON_EXTERNAL_PROJECT_ARGS}
  INSTALL_COMMAND ""
)
set(QWT_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/QWT-build/libqwt.a)
set(QWT_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/QWT/src)
