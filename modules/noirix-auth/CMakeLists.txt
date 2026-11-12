if(CONFIG_NOIRIX_AUTH)
  zephyr_include_directories(include)

  file(GLOB_RECURSE C_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
  target_sources(app PRIVATE ${C_FILES})

  list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
  include(nanopb)
  set(NANOPB_GENERATE_CPP_APPEND_PATH TRUE)
  set(NANOPB_GENERATE_CPP_STANDALONE OFF)

  # Adding sources directly to `app` can cause build issues, so build a library.
  zephyr_library()
  file(GLOB_RECURSE PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto)
  nanopb_generate_cpp(proto_srcs proto_hdrs RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILES})
  target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${proto_srcs} ${proto_hdrs})
  target_include_directories(app PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)
  add_dependencies(app ${ZEPHYR_CURRENT_LIBRARY})
endif()
