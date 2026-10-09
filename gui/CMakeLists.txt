add_executable(llama-gui WIN32
    main.cpp
    chat.cpp
    markdown.cpp
    thirdParty/md4c/md4c.c
)

target_include_directories(llama-gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/md4c"
)


target_link_libraries(llama-gui PRIVATE llama)

target_compile_features(llama-gui PRIVATE cxx_std_17)
