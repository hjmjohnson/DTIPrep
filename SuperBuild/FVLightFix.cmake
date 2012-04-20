file(READ ${fixfile} code)
string(REPLACE "TARGET_LINK_LIBRARIES(FiberViewerLight.*)"
  "TARGET_LINK_LIBRARIES(\${VTK_LIBRARIES}
  \${ITK_LIBRARIES} \${QT_LIBRARIES} \${QWT_LIBRARIES})"
  code "${code}")

file(WRITE ${fixfile} "${code}")
